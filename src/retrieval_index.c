#include "retrieval_index.h"
#include "markdown_sections.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define ATM_RETRIEVAL_SCHEMA_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/retrieval-index-v1.sql"

GQuark
atm_retrieval_index_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-index-error-quark"
    );
}

static gboolean
repository_id_is_valid (const char *repository_id)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static gboolean
lower_hex_is_valid (const char *value, gsize length)
{
    if (value == NULL || strlen (value) != length) {
        return FALSE;
    }

    for (gsize i = 0; i < length; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (value[i] >= 'A' && value[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
sqlite_exec_checked (
    sqlite3 *db,
    const char *sql,
    GError **error
)
{
    char *message = NULL;
    int rc = sqlite3_exec (db, sql, NULL, NULL, &message);

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "SQLite statement failed (%d): %s",
            rc,
            message != NULL ? message : sqlite3_errmsg (db)
        );
        sqlite3_free (message);
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_integrity_writable (sqlite3 *db, GError **error)
{
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA integrity_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        goto sqlite_failure;
    }

    if (sqlite3_step (statement) != SQLITE_ROW ||
        g_strcmp0 (
            (const char *) sqlite3_column_text (statement, 0),
            "ok"
        ) != 0 ||
        sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        statement = NULL;
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "PRAGMA integrity_check did not return exactly one 'ok' row."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA foreign_key_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        goto sqlite_failure;
    }

    if (sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        statement = NULL;
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "PRAGMA foreign_key_check reported a violation."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (!sqlite_exec_checked (
            db,
            "INSERT INTO search_fts(search_fts) "
            "VALUES('integrity-check');",
            error
        )) {
        return FALSE;
    }

    ok = TRUE;
    return ok;

sqlite_failure:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }

    g_set_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
        "SQLite integrity query could not be prepared: %s",
        sqlite3_errmsg (db)
    );
    return FALSE;
}

static gboolean
insert_metadata (
    sqlite3 *db,
    const AtmRetrievalIndexMetadata *metadata,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc;

    rc = sqlite3_prepare_v2 (
        db,
        "INSERT INTO snapshot_metadata("
        "id, repository_id, repository_version, snapshot_sha, "
        "manifest_schema_version, manifest_sha256, created_at_utc"
        ") VALUES(1, ?1, ?2, ?3, ?4, ?5, ?6);",
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare retrieval-index metadata insert: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    sqlite3_bind_text (
        statement,
        1,
        metadata->repository_id,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        2,
        metadata->repository_version,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        3,
        metadata->snapshot_sha,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_int (
        statement,
        4,
        metadata->manifest_schema_version
    );
    sqlite3_bind_text (
        statement,
        5,
        metadata->manifest_sha256,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        6,
        metadata->created_at_utc,
        -1,
        SQLITE_STATIC
    );

    rc = sqlite3_step (statement);
    sqlite3_finalize (statement);

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not insert retrieval-index metadata: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
insert_source_catalog (
    sqlite3 *db,
    const char *repository_id,
    const AtmSourceCatalog *catalog,
    GError **error
)
{
    static const struct {
        AtmSourceRole role;
        const char *name;
    } roles[] = {
        { ATM_SOURCE_ROLE_CANONICAL, "canonical" },
        { ATM_SOURCE_ROLE_STRUCTURAL, "structural" },
        { ATM_SOURCE_ROLE_EVIDENCE, "evidence" },
        { ATM_SOURCE_ROLE_TABULAR, "tabular" },
        { ATM_SOURCE_ROLE_IMPLEMENTATION, "implementation" }
    };
    sqlite3_stmt *source_statement = NULL;
    sqlite3_stmt *role_statement = NULL;
    gboolean ok = FALSE;

    if (sqlite3_prepare_v2 (
            db,
            "INSERT INTO source_files("
            "path, sha256, byte_size, media_type, logical_source_id"
            ") VALUES(?1, ?2, ?3, ?4, ?5);",
            -1,
            &source_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO source_roles(source_id, role) "
            "VALUES(?1, ?2);",
            -1,
            &role_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare source-catalog inserts: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    for (guint i = 0; i < catalog->files->len; i++) {
        const AtmSourceRecord *record = g_ptr_array_index (
            catalog->files,
            i
        );
        char *logical_source_id = g_strdup_printf (
            "%s:file:%s",
            repository_id,
            record->path
        );
        sqlite3_int64 source_id;

        sqlite3_reset (source_statement);
        sqlite3_clear_bindings (source_statement);

        sqlite3_bind_text (
            source_statement,
            1,
            record->path,
            -1,
            SQLITE_STATIC
        );
        sqlite3_bind_text (
            source_statement,
            2,
            record->sha256,
            -1,
            SQLITE_STATIC
        );
        sqlite3_bind_int64 (
            source_statement,
            3,
            (sqlite3_int64) record->byte_size
        );
        sqlite3_bind_text (
            source_statement,
            4,
            record->media_type,
            -1,
            SQLITE_STATIC
        );
        sqlite3_bind_text (
            source_statement,
            5,
            logical_source_id,
            -1,
            SQLITE_TRANSIENT
        );

        if (sqlite3_step (source_statement) != SQLITE_DONE) {
            g_set_error (
                error,
                ATM_RETRIEVAL_INDEX_ERROR,
                ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                "Could not insert source file '%s': %s",
                record->path,
                sqlite3_errmsg (db)
            );
            g_free (logical_source_id);
            goto out;
        }

        source_id = sqlite3_last_insert_rowid (db);

        for (gsize role_index = 0;
             role_index < G_N_ELEMENTS (roles);
             role_index++) {
            if ((record->roles & roles[role_index].role) == 0) {
                continue;
            }

            sqlite3_reset (role_statement);
            sqlite3_clear_bindings (role_statement);
            sqlite3_bind_int64 (
                role_statement,
                1,
                source_id
            );
            sqlite3_bind_text (
                role_statement,
                2,
                roles[role_index].name,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (role_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert source role '%s' for '%s': %s",
                    roles[role_index].name,
                    record->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_source_id);
                goto out;
            }
        }

        g_free (logical_source_id);
    }

    ok = TRUE;

out:
    if (role_statement != NULL) {
        sqlite3_finalize (role_statement);
    }

    if (source_statement != NULL) {
        sqlite3_finalize (source_statement);
    }

    return ok;
}

static gboolean
insert_document_sections (
    sqlite3 *db,
    const char *snapshot_root,
    const char *repository_id,
    const AtmSourceCatalog *catalog,
    GError **error
)
{
    sqlite3_stmt *source_statement = NULL;
    sqlite3_stmt *section_statement = NULL;
    sqlite3_stmt *fts_statement = NULL;
    gboolean ok = FALSE;

    if (sqlite3_prepare_v2 (
            db,
            "SELECT id FROM source_files WHERE path = ?1;",
            -1,
            &source_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO document_sections("
            "source_id, ordinal, heading_path, locator, "
            "logical_source_id, title, body"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);",
            -1,
            &section_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO search_fts("
            "evidence_kind, evidence_id, logical_source_id, "
            "title, body"
            ") VALUES('section', ?1, ?2, ?3, ?4);",
            -1,
            &fts_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare document-index inserts: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    for (guint i = 0; i < catalog->files->len; i++) {
        const AtmSourceRecord *source = g_ptr_array_index (
            catalog->files,
            i
        );

        if (g_strcmp0 (
                source->media_type,
                "text/markdown"
            ) != 0) {
            continue;
        }

        char *absolute_path = g_build_filename (
            snapshot_root,
            source->path,
            NULL
        );
        GPtrArray *sections = NULL;

        if (!atm_markdown_extract_sections (
                absolute_path,
                &sections,
                error
            )) {
            g_free (absolute_path);
            goto out;
        }

        g_free (absolute_path);

        sqlite3_reset (source_statement);
        sqlite3_clear_bindings (source_statement);
        sqlite3_bind_text (
            source_statement,
            1,
            source->path,
            -1,
            SQLITE_STATIC
        );

        if (sqlite3_step (source_statement) != SQLITE_ROW) {
            g_set_error (
                error,
                ATM_RETRIEVAL_INDEX_ERROR,
                ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
                "Markdown source '%s' is missing from source_files.",
                source->path
            );
            g_ptr_array_unref (sections);
            goto out;
        }

        sqlite3_int64 source_id = sqlite3_column_int64 (
            source_statement,
            0
        );

        for (guint section_index = 0;
             section_index < sections->len;
             section_index++) {
            const AtmDocumentSection *section = g_ptr_array_index (
                sections,
                section_index
            );
            char *locator = g_strdup_printf (
                "lines:%u-%u",
                section->start_line,
                section->end_line
            );
            char *logical_source_id = g_strdup_printf (
                "%s:section:%s:%s",
                repository_id,
                source->path,
                locator
            );

            sqlite3_reset (section_statement);
            sqlite3_clear_bindings (section_statement);
            sqlite3_bind_int64 (
                section_statement,
                1,
                source_id
            );
            sqlite3_bind_int (
                section_statement,
                2,
                (int) section->ordinal
            );

            if (section->heading_path != NULL &&
                section->heading_path[0] != '\0') {
                sqlite3_bind_text (
                    section_statement,
                    3,
                    section->heading_path,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (section_statement, 3);
            }

            sqlite3_bind_text (
                section_statement,
                4,
                locator,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                section_statement,
                5,
                logical_source_id,
                -1,
                SQLITE_TRANSIENT
            );

            if (section->title != NULL) {
                sqlite3_bind_text (
                    section_statement,
                    6,
                    section->title,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (section_statement, 6);
            }

            sqlite3_bind_text (
                section_statement,
                7,
                section->body,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (section_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert Markdown section for '%s': %s",
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_source_id);
                g_free (locator);
                g_ptr_array_unref (sections);
                goto out;
            }

            sqlite3_int64 section_id = sqlite3_last_insert_rowid (db);

            sqlite3_reset (fts_statement);
            sqlite3_clear_bindings (fts_statement);
            sqlite3_bind_int64 (
                fts_statement,
                1,
                section_id
            );
            sqlite3_bind_text (
                fts_statement,
                2,
                logical_source_id,
                -1,
                SQLITE_TRANSIENT
            );

            if (section->title != NULL) {
                sqlite3_bind_text (
                    fts_statement,
                    3,
                    section->title,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (fts_statement, 3);
            }

            sqlite3_bind_text (
                fts_statement,
                4,
                section->body,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (fts_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert FTS section for '%s': %s",
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_source_id);
                g_free (locator);
                g_ptr_array_unref (sections);
                goto out;
            }

            g_free (logical_source_id);
            g_free (locator);
        }

        g_ptr_array_unref (sections);
    }

    ok = TRUE;

out:
    if (fts_statement != NULL) {
        sqlite3_finalize (fts_statement);
    }

    if (section_statement != NULL) {
        sqlite3_finalize (section_statement);
    }

    if (source_statement != NULL) {
        sqlite3_finalize (source_statement);
    }

    return ok;
}

char *
atm_retrieval_index_path (
    const char *cache_root,
    const char *repository_id,
    const char *sha
)
{
    char *filename;

    g_return_val_if_fail (cache_root != NULL, NULL);
    g_return_val_if_fail (repository_id != NULL, NULL);
    g_return_val_if_fail (sha != NULL, NULL);

    filename = g_strdup_printf ("%s.sqlite", sha);

    char *path = g_build_filename (
        cache_root,
        "retrieval",
        repository_id,
        filename,
        NULL
    );

    g_free (filename);
    return path;
}

char *
atm_retrieval_index_staging_path (
    const char *cache_root,
    const char *repository_id,
    const char *sha
)
{
    char *filename;

    g_return_val_if_fail (cache_root != NULL, NULL);
    g_return_val_if_fail (repository_id != NULL, NULL);
    g_return_val_if_fail (sha != NULL, NULL);

    filename = g_strdup_printf ("%s.sqlite.part", sha);

    char *path = g_build_filename (
        cache_root,
        "retrieval",
        repository_id,
        filename,
        NULL
    );

    g_free (filename);
    return path;
}

static gboolean
retrieval_index_create_internal (
    const char *cache_root,
    const char *snapshot_root,
    const AtmRetrievalIndexMetadata *metadata,
    const AtmSourceCatalog *source_catalog,
    char **out_index_path,
    GError **error
)
{
    char *final_path = NULL;
    char *staging_path = NULL;
    char *parent = NULL;
    GBytes *schema_bytes = NULL;
    gconstpointer schema_data;
    gsize schema_size = 0;
    char *schema_sql = NULL;
    sqlite3 *db = NULL;
    GStatBuf stat_buffer;
    gboolean transaction_open = FALSE;
    gboolean ok = FALSE;
    int rc;

    g_return_val_if_fail (cache_root != NULL, FALSE);
    g_return_val_if_fail (metadata != NULL, FALSE);
    g_return_val_if_fail (out_index_path != NULL, FALSE);
    g_return_val_if_fail (*out_index_path == NULL, FALSE);

    if (!repository_id_is_valid (metadata->repository_id)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INVALID_ID,
            "Repository ID is not part of the v1 catalog."
        );
        goto out;
    }

    if (!lower_hex_is_valid (metadata->snapshot_sha, 40)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INVALID_SHA,
            "Snapshot SHA must be 40 lowercase hexadecimal characters."
        );
        goto out;
    }

    if (!lower_hex_is_valid (metadata->manifest_sha256, 64)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INVALID_MANIFEST_HASH,
            "Manifest SHA-256 must be 64 lowercase hexadecimal characters."
        );
        goto out;
    }

    if (source_catalog != NULL &&
        g_strcmp0 (
            source_catalog->manifest_sha256,
            metadata->manifest_sha256
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Source catalog manifest hash does not match index metadata."
        );
        goto out;
    }

    if (metadata->repository_version == NULL ||
        metadata->repository_version[0] == '\0' ||
        metadata->manifest_schema_version != 1 ||
        metadata->created_at_utc == NULL ||
        metadata->created_at_utc[0] == '\0') {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Retrieval-index metadata is incomplete or unsupported."
        );
        goto out;
    }

    final_path = atm_retrieval_index_path (
        cache_root,
        metadata->repository_id,
        metadata->snapshot_sha
    );
    staging_path = atm_retrieval_index_staging_path (
        cache_root,
        metadata->repository_id,
        metadata->snapshot_sha
    );

    if (g_lstat (final_path, &stat_buffer) == 0) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_EXISTS,
            "A retrieval index for this snapshot already exists."
        );
        goto out;
    }

    if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_IO,
            "Could not inspect final retrieval-index path: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (g_lstat (staging_path, &stat_buffer) == 0) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_STAGING_EXISTS,
            "Retrieval-index staging already exists."
        );
        goto out;
    }

    if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_IO,
            "Could not inspect retrieval-index staging: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    parent = g_path_get_dirname (staging_path);
    if (g_mkdir_with_parents (parent, 0700) != 0) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_IO,
            "Could not create retrieval-index directory: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    schema_bytes = g_resources_lookup_data (
        ATM_RETRIEVAL_SCHEMA_RESOURCE,
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        error
    );

    if (schema_bytes == NULL) {
        goto out;
    }

    schema_data = g_bytes_get_data (schema_bytes, &schema_size);
    schema_sql = g_strndup (schema_data, schema_size);

    rc = sqlite3_open_v2 (
        staging_path,
        &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not create retrieval-index staging database: %s",
            db != NULL ? sqlite3_errmsg (db) : "unknown SQLite error"
        );
        goto out;
    }

    if (!sqlite_exec_checked (
            db,
            "PRAGMA foreign_keys = ON;"
            "PRAGMA trusted_schema = OFF;"
            "BEGIN IMMEDIATE;",
            error
        )) {
        goto out;
    }

    transaction_open = TRUE;

    if (!sqlite_exec_checked (db, schema_sql, error) ||
        !insert_metadata (db, metadata, error) ||
        (source_catalog != NULL &&
         !insert_source_catalog (
             db,
             metadata->repository_id,
             source_catalog,
             error
         )) ||
        (snapshot_root != NULL &&
         source_catalog != NULL &&
         !insert_document_sections (
             db,
             snapshot_root,
             metadata->repository_id,
             source_catalog,
             error
         )) ||
        !sqlite_exec_checked (db, "COMMIT;", error)) {
        goto out;
    }

    transaction_open = FALSE;

    if (!validate_integrity_writable (db, error)) {
        goto out;
    }

    if (sqlite3_close (db) != SQLITE_OK) {
        db = NULL;
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not close validated retrieval index."
        );
        goto out;
    }

    db = NULL;

    if (g_rename (staging_path, final_path) != 0) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_IO,
            "Could not atomically promote retrieval index: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    *out_index_path = g_steal_pointer (&final_path);
    ok = TRUE;

out:
    if (db != NULL) {
        if (transaction_open) {
            sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
        }
        sqlite3_close (db);
    }

    if (!ok && staging_path != NULL) {
        g_remove (staging_path);
    }

    g_clear_pointer (&schema_sql, g_free);
    g_clear_pointer (&schema_bytes, g_bytes_unref);
    g_clear_pointer (&parent, g_free);
    g_clear_pointer (&staging_path, g_free);
    g_clear_pointer (&final_path, g_free);
    return ok;
}

gboolean
atm_retrieval_index_create_empty (
    const char *cache_root,
    const AtmRetrievalIndexMetadata *metadata,
    char **out_index_path,
    GError **error
)
{
    return retrieval_index_create_internal (
        cache_root,
        NULL,
        metadata,
        NULL,
        out_index_path,
        error
    );
}

gboolean
atm_retrieval_index_create_with_sources (
    const char *cache_root,
    const AtmRetrievalIndexMetadata *metadata,
    const AtmSourceCatalog *source_catalog,
    char **out_index_path,
    GError **error
)
{
    g_return_val_if_fail (source_catalog != NULL, FALSE);

    return retrieval_index_create_internal (
        cache_root,
        NULL,
        metadata,
        source_catalog,
        out_index_path,
        error
    );
}

gboolean
atm_retrieval_index_create_with_documents (
    const char *cache_root,
    const char *snapshot_root,
    const AtmRetrievalIndexMetadata *metadata,
    const AtmSourceCatalog *source_catalog,
    char **out_index_path,
    GError **error
)
{
    g_return_val_if_fail (snapshot_root != NULL, FALSE);
    g_return_val_if_fail (source_catalog != NULL, FALSE);

    return retrieval_index_create_internal (
        cache_root,
        snapshot_root,
        metadata,
        source_catalog,
        out_index_path,
        error
    );
}

gboolean
atm_retrieval_index_validate_identity (
    const char *index_path,
    const char *expected_repository_id,
    const char *expected_snapshot_sha,
    GError **error
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;
    int rc;

    g_return_val_if_fail (index_path != NULL, FALSE);
    g_return_val_if_fail (expected_repository_id != NULL, FALSE);
    g_return_val_if_fail (expected_snapshot_sha != NULL, FALSE);

    rc = sqlite3_open_v2 (
        index_path,
        &db,
        SQLITE_OPEN_READONLY,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not open retrieval index read-only: %s",
            db != NULL ? sqlite3_errmsg (db) : "unknown SQLite error"
        );
        goto out;
    }

    if (!sqlite_exec_checked (
            db,
            "PRAGMA trusted_schema = OFF;"
            "PRAGMA query_only = ON;"
            "PRAGMA foreign_keys = ON;",
            error
        )) {
        goto out;
    }

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA user_version;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_step (statement) != SQLITE_ROW ||
        sqlite3_column_int (statement, 0) !=
            ATM_RETRIEVAL_INDEX_SCHEMA_VERSION) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Retrieval-index schema version is unsupported."
        );
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "SELECT repository_id, snapshot_sha "
            "FROM snapshot_metadata WHERE id = 1;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_step (statement) != SQLITE_ROW) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Retrieval-index snapshot metadata is missing."
        );
        goto out;
    }

    if (g_strcmp0 (
            (const char *) sqlite3_column_text (statement, 0),
            expected_repository_id
        ) != 0 ||
        g_strcmp0 (
            (const char *) sqlite3_column_text (statement, 1),
            expected_snapshot_sha
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Retrieval index does not match the expected snapshot identity."
        );
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA integrity_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_step (statement) != SQLITE_ROW ||
        g_strcmp0 (
            (const char *) sqlite3_column_text (statement, 0),
            "ok"
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Read-only retrieval-index integrity check failed."
        );
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA foreign_key_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_step (statement) != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Read-only retrieval-index foreign-key check failed."
        );
        goto out;
    }

    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }

    if (db != NULL) {
        sqlite3_close (db);
    }

    return ok;
}
