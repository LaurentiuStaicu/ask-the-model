#include "retrieval_index.h"
#include "markdown_sections.h"
#include "csv_table.h"
#include "structured_json.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
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

static char *
csv_metadata_json (const AtmCsvTable *table)
{
    JsonBuilder *builder = json_builder_new ();
    JsonGenerator *generator = json_generator_new ();
    JsonNode *root;
    char *json;

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "columns");
    json_builder_begin_array (builder);

    for (guint i = 0; i < table->headers->len; i++) {
        json_builder_add_string_value (
            builder,
            g_ptr_array_index (table->headers, i)
        );
    }

    json_builder_end_array (builder);
    json_builder_set_member_name (builder, "row_count");
    json_builder_add_int_value (
        builder,
        (gint64) table->rows->len
    );
    json_builder_set_member_name (builder, "delimiter");
    json_builder_add_string_value (builder, ",");
    json_builder_end_object (builder);

    root = json_builder_get_root (builder);
    json_generator_set_root (generator, root);
    json = json_generator_to_data (generator, NULL);

    json_node_free (root);
    g_object_unref (generator);
    g_object_unref (builder);
    return json;
}

static char *
csv_row_payload_json (
    const AtmCsvTable *table,
    const AtmCsvRow *row
)
{
    JsonBuilder *builder = json_builder_new ();
    JsonGenerator *generator = json_generator_new ();
    JsonNode *root;
    char *json;

    json_builder_begin_object (builder);

    for (guint i = 0; i < table->headers->len; i++) {
        json_builder_set_member_name (
            builder,
            g_ptr_array_index (table->headers, i)
        );
        json_builder_add_string_value (
            builder,
            g_ptr_array_index (row->fields, i)
        );
    }

    json_builder_end_object (builder);

    root = json_builder_get_root (builder);
    json_generator_set_root (generator, root);
    json = json_generator_to_data (generator, NULL);

    json_node_free (root);
    g_object_unref (generator);
    g_object_unref (builder);
    return json;
}

static char *
csv_row_search_text (
    const AtmCsvTable *table,
    const AtmCsvRow *row
)
{
    GString *search = g_string_new (NULL);

    for (guint i = 0; i < table->headers->len; i++) {
        const char *header = g_ptr_array_index (
            table->headers,
            i
        );
        const char *value = g_ptr_array_index (
            row->fields,
            i
        );

        if (search->len > 0) {
            g_string_append_c (search, '\n');
        }

        g_string_append (search, header);
        g_string_append_c (search, '=');
        g_string_append (search, value);
    }

    return g_string_free (search, FALSE);
}

static gboolean
insert_tabular_datasets (
    sqlite3 *db,
    const char *snapshot_root,
    const char *repository_id,
    const AtmSourceCatalog *catalog,
    GError **error
)
{
    sqlite3_stmt *source_statement = NULL;
    sqlite3_stmt *dataset_statement = NULL;
    sqlite3_stmt *row_statement = NULL;
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
            "INSERT INTO datasets("
            "source_id, native_id, logical_source_id, locator, "
            "title, metadata_json"
            ") VALUES(?1, NULL, ?2, ?3, ?4, ?5);",
            -1,
            &dataset_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO dataset_rows("
            "dataset_id, ordinal, row_key, locator, "
            "payload_json, search_text"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6);",
            -1,
            &row_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO search_fts("
            "evidence_kind, evidence_id, logical_source_id, "
            "title, body"
            ") VALUES('dataset_row', ?1, ?2, ?3, ?4);",
            -1,
            &fts_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare dataset-index inserts: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    for (guint i = 0; i < catalog->files->len; i++) {
        const AtmSourceRecord *source = g_ptr_array_index (
            catalog->files,
            i
        );

        if ((source->roles & ATM_SOURCE_ROLE_TABULAR) == 0 ||
            g_strcmp0 (source->media_type, "text/csv") != 0) {
            continue;
        }

        char *absolute_path = g_build_filename (
            snapshot_root,
            source->path,
            NULL
        );
        AtmCsvTable *table = NULL;

        if (!atm_csv_parse_file (
                absolute_path,
                &table,
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
                "CSV source '%s' is missing from source_files.",
                source->path
            );
            atm_csv_table_free (table);
            goto out;
        }

        sqlite3_int64 source_id = sqlite3_column_int64 (
            source_statement,
            0
        );
        char *dataset_id = g_strdup_printf (
            "%s:dataset:%s",
            repository_id,
            source->path
        );
        char *dataset_locator = g_strdup_printf (
            "file:%s",
            source->path
        );
        char *metadata_json = csv_metadata_json (table);

        sqlite3_reset (dataset_statement);
        sqlite3_clear_bindings (dataset_statement);
        sqlite3_bind_int64 (
            dataset_statement,
            1,
            source_id
        );
        sqlite3_bind_text (
            dataset_statement,
            2,
            dataset_id,
            -1,
            SQLITE_TRANSIENT
        );
        sqlite3_bind_text (
            dataset_statement,
            3,
            dataset_locator,
            -1,
            SQLITE_TRANSIENT
        );
        sqlite3_bind_text (
            dataset_statement,
            4,
            source->path,
            -1,
            SQLITE_STATIC
        );
        sqlite3_bind_text (
            dataset_statement,
            5,
            metadata_json,
            -1,
            SQLITE_TRANSIENT
        );

        if (sqlite3_step (dataset_statement) != SQLITE_DONE) {
            g_set_error (
                error,
                ATM_RETRIEVAL_INDEX_ERROR,
                ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                "Could not insert dataset for '%s': %s",
                source->path,
                sqlite3_errmsg (db)
            );
            g_free (metadata_json);
            g_free (dataset_locator);
            g_free (dataset_id);
            atm_csv_table_free (table);
            goto out;
        }

        sqlite3_int64 sqlite_dataset_id =
            sqlite3_last_insert_rowid (db);

        for (guint row_index = 0;
             row_index < table->rows->len;
             row_index++) {
            const AtmCsvRow *row = g_ptr_array_index (
                table->rows,
                row_index
            );
            const char *first_value = row->fields->len > 0
                ? g_ptr_array_index (row->fields, 0)
                : NULL;
            char *locator = g_strdup_printf (
                "lines:%u-%u",
                row->start_line,
                row->end_line
            );
            char *payload_json = csv_row_payload_json (
                table,
                row
            );
            char *search_text = csv_row_search_text (
                table,
                row
            );
            char *logical_row_id = g_strdup_printf (
                "%s:dataset-row:%s:%u",
                repository_id,
                source->path,
                row->ordinal
            );

            sqlite3_reset (row_statement);
            sqlite3_clear_bindings (row_statement);
            sqlite3_bind_int64 (
                row_statement,
                1,
                sqlite_dataset_id
            );
            sqlite3_bind_int (
                row_statement,
                2,
                (int) row->ordinal
            );

            if (first_value != NULL && first_value[0] != '\0') {
                sqlite3_bind_text (
                    row_statement,
                    3,
                    first_value,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (row_statement, 3);
            }

            sqlite3_bind_text (
                row_statement,
                4,
                locator,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                row_statement,
                5,
                payload_json,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                row_statement,
                6,
                search_text,
                -1,
                SQLITE_TRANSIENT
            );

            if (sqlite3_step (row_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert dataset row for '%s': %s",
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_row_id);
                g_free (search_text);
                g_free (payload_json);
                g_free (locator);
                g_free (metadata_json);
                g_free (dataset_locator);
                g_free (dataset_id);
                atm_csv_table_free (table);
                goto out;
            }

            sqlite3_int64 sqlite_row_id =
                sqlite3_last_insert_rowid (db);

            sqlite3_reset (fts_statement);
            sqlite3_clear_bindings (fts_statement);
            sqlite3_bind_int64 (
                fts_statement,
                1,
                sqlite_row_id
            );
            sqlite3_bind_text (
                fts_statement,
                2,
                logical_row_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                fts_statement,
                3,
                source->path,
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_text (
                fts_statement,
                4,
                search_text,
                -1,
                SQLITE_TRANSIENT
            );

            if (sqlite3_step (fts_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert FTS dataset row for '%s': %s",
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_row_id);
                g_free (search_text);
                g_free (payload_json);
                g_free (locator);
                g_free (metadata_json);
                g_free (dataset_locator);
                g_free (dataset_id);
                atm_csv_table_free (table);
                goto out;
            }

            g_free (logical_row_id);
            g_free (search_text);
            g_free (payload_json);
            g_free (locator);
        }

        g_free (metadata_json);
        g_free (dataset_locator);
        g_free (dataset_id);
        atm_csv_table_free (table);
    }

    ok = TRUE;

out:
    if (fts_statement != NULL) {
        sqlite3_finalize (fts_statement);
    }

    if (row_statement != NULL) {
        sqlite3_finalize (row_statement);
    }

    if (dataset_statement != NULL) {
        sqlite3_finalize (dataset_statement);
    }

    if (source_statement != NULL) {
        sqlite3_finalize (source_statement);
    }

    return ok;
}

static gboolean
source_is_structured_json (const AtmSourceRecord *source)
{
    return g_strcmp0 (
        source->media_type,
        "application/json"
    ) == 0;
}

static gboolean
lookup_source_id (
    sqlite3_stmt *statement,
    const char *path,
    sqlite3_int64 *out_source_id,
    sqlite3 *db,
    GError **error
)
{
    sqlite3_reset (statement);
    sqlite3_clear_bindings (statement);
    sqlite3_bind_text (
        statement,
        1,
        path,
        -1,
        SQLITE_STATIC
    );

    if (sqlite3_step (statement) != SQLITE_ROW) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Structured JSON source '%s' is missing from source_files: %s",
            path,
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    *out_source_id = sqlite3_column_int64 (statement, 0);
    return TRUE;
}

static gboolean
collect_native_entity_counts (
    const char *snapshot_root,
    const AtmSourceCatalog *catalog,
    GHashTable *counts,
    GError **error
)
{
    for (guint i = 0; i < catalog->files->len; i++) {
        const AtmSourceRecord *source = g_ptr_array_index (
            catalog->files,
            i
        );

        if (!source_is_structured_json (source)) {
            continue;
        }

        char *absolute_path = g_build_filename (
            snapshot_root,
            source->path,
            NULL
        );
        AtmStructuredJsonRecords *records = NULL;

        if (!atm_structured_json_extract (
                absolute_path,
                source->path,
                &records,
                error
            )) {
            g_free (absolute_path);
            return FALSE;
        }

        g_free (absolute_path);

        for (guint entity_index = 0;
             entity_index < records->entities->len;
             entity_index++) {
            const AtmStructuredEntity *entity = g_ptr_array_index (
                records->entities,
                entity_index
            );
            guint count = GPOINTER_TO_UINT (
                g_hash_table_lookup (
                    counts,
                    entity->native_id
                )
            );

            g_hash_table_replace (
                counts,
                g_strdup (entity->native_id),
                GUINT_TO_POINTER (count + 1)
            );
        }

        atm_structured_json_records_free (records);
    }

    return TRUE;
}

static char *
structured_entity_logical_id (
    const char *repository_id,
    const AtmSourceRecord *source,
    const AtmStructuredEntity *entity,
    gboolean native_id_is_unique
)
{
    if (native_id_is_unique) {
        return g_strdup_printf (
            "%s:entity:%s:%s",
            repository_id,
            entity->entity_type,
            entity->native_id
        );
    }

    return g_strdup_printf (
        "%s:entity:%s:%s:%s",
        repository_id,
        entity->entity_type,
        source->path,
        entity->locator
    );
}

static char *
structured_relation_logical_id (
    const char *repository_id,
    const AtmSourceRecord *source,
    const AtmStructuredRelation *relation
)
{
    if (relation->native_id != NULL &&
        relation->native_id[0] != '\0') {
        return g_strdup_printf (
            "%s:relation:%s:%s:%s",
            repository_id,
            relation->relation_type,
            relation->native_id,
            source->path
        );
    }

    return g_strdup_printf (
        "%s:relation:%s:%s",
        repository_id,
        source->path,
        relation->locator
    );
}

static gboolean
insert_structured_json_content (
    sqlite3 *db,
    const char *snapshot_root,
    const char *repository_id,
    const AtmSourceCatalog *catalog,
    GError **error
)
{
    sqlite3_stmt *source_statement = NULL;
    sqlite3_stmt *entity_statement = NULL;
    sqlite3_stmt *relation_statement = NULL;
    sqlite3_stmt *fts_statement = NULL;
    GHashTable *native_counts = NULL;
    GHashTable *native_to_logical = NULL;
    gboolean ok = FALSE;

    native_counts = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );
    native_to_logical = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        g_free
    );

    if (!collect_native_entity_counts (
            snapshot_root,
            catalog,
            native_counts,
            error
        )) {
        goto out;
    }

    if (sqlite3_prepare_v2 (
            db,
            "SELECT id FROM source_files WHERE path = ?1;",
            -1,
            &source_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO structured_entities("
            "source_id, entity_type, native_id, logical_source_id, "
            "locator, label, payload_json"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);",
            -1,
            &entity_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO structured_relations("
            "source_id, relation_type, native_id, logical_source_id, "
            "locator, from_logical_source_id, to_logical_source_id, "
            "payload_json"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);",
            -1,
            &relation_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "INSERT INTO search_fts("
            "evidence_kind, evidence_id, logical_source_id, title, body"
            ") VALUES(?1, ?2, ?3, ?4, ?5);",
            -1,
            &fts_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare structured-content inserts: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    for (guint i = 0; i < catalog->files->len; i++) {
        const AtmSourceRecord *source = g_ptr_array_index (
            catalog->files,
            i
        );

        if (!source_is_structured_json (source)) {
            continue;
        }

        char *absolute_path = g_build_filename (
            snapshot_root,
            source->path,
            NULL
        );
        AtmStructuredJsonRecords *records = NULL;
        sqlite3_int64 source_id = 0;

        if (!atm_structured_json_extract (
                absolute_path,
                source->path,
                &records,
                error
            )) {
            g_free (absolute_path);
            goto out;
        }

        g_free (absolute_path);

        if (!lookup_source_id (
                source_statement,
                source->path,
                &source_id,
                db,
                error
            )) {
            atm_structured_json_records_free (records);
            goto out;
        }

        for (guint entity_index = 0;
             entity_index < records->entities->len;
             entity_index++) {
            const AtmStructuredEntity *entity = g_ptr_array_index (
                records->entities,
                entity_index
            );
            guint native_count = GPOINTER_TO_UINT (
                g_hash_table_lookup (
                    native_counts,
                    entity->native_id
                )
            );
            gboolean native_unique = native_count == 1;
            char *logical_id = structured_entity_logical_id (
                repository_id,
                source,
                entity,
                native_unique
            );

            sqlite3_reset (entity_statement);
            sqlite3_clear_bindings (entity_statement);
            sqlite3_bind_int64 (
                entity_statement,
                1,
                source_id
            );
            sqlite3_bind_text (
                entity_statement,
                2,
                entity->entity_type,
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_text (
                entity_statement,
                3,
                entity->native_id,
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_text (
                entity_statement,
                4,
                logical_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                entity_statement,
                5,
                entity->locator,
                -1,
                SQLITE_STATIC
            );

            if (entity->label != NULL) {
                sqlite3_bind_text (
                    entity_statement,
                    6,
                    entity->label,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (entity_statement, 6);
            }

            sqlite3_bind_text (
                entity_statement,
                7,
                entity->payload_json,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (entity_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert structured entity '%s' from '%s': %s",
                    entity->native_id,
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_id);
                atm_structured_json_records_free (records);
                goto out;
            }

            sqlite3_int64 entity_id = sqlite3_last_insert_rowid (db);

            if (native_unique) {
                g_hash_table_replace (
                    native_to_logical,
                    g_strdup (entity->native_id),
                    g_strdup (logical_id)
                );
            }

            sqlite3_reset (fts_statement);
            sqlite3_clear_bindings (fts_statement);
            sqlite3_bind_text (
                fts_statement,
                1,
                "entity",
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_int64 (
                fts_statement,
                2,
                entity_id
            );
            sqlite3_bind_text (
                fts_statement,
                3,
                logical_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                fts_statement,
                4,
                entity->label != NULL
                    ? entity->label
                    : entity->native_id,
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_text (
                fts_statement,
                5,
                entity->payload_json,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (fts_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert FTS entity '%s' from '%s': %s",
                    entity->native_id,
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_id);
                atm_structured_json_records_free (records);
                goto out;
            }

            g_free (logical_id);
        }

        atm_structured_json_records_free (records);
    }

    for (guint i = 0; i < catalog->files->len; i++) {
        const AtmSourceRecord *source = g_ptr_array_index (
            catalog->files,
            i
        );

        if (!source_is_structured_json (source)) {
            continue;
        }

        char *absolute_path = g_build_filename (
            snapshot_root,
            source->path,
            NULL
        );
        AtmStructuredJsonRecords *records = NULL;
        sqlite3_int64 source_id = 0;

        if (!atm_structured_json_extract (
                absolute_path,
                source->path,
                &records,
                error
            )) {
            g_free (absolute_path);
            goto out;
        }

        g_free (absolute_path);

        if (!lookup_source_id (
                source_statement,
                source->path,
                &source_id,
                db,
                error
            )) {
            atm_structured_json_records_free (records);
            goto out;
        }

        for (guint relation_index = 0;
             relation_index < records->relations->len;
             relation_index++) {
            const AtmStructuredRelation *relation = g_ptr_array_index (
                records->relations,
                relation_index
            );
            const char *from_logical = relation->from_native_id != NULL
                ? g_hash_table_lookup (
                    native_to_logical,
                    relation->from_native_id
                )
                : NULL;
            const char *to_logical = relation->to_native_id != NULL
                ? g_hash_table_lookup (
                    native_to_logical,
                    relation->to_native_id
                )
                : NULL;
            char *logical_id = structured_relation_logical_id (
                repository_id,
                source,
                relation
            );

            sqlite3_reset (relation_statement);
            sqlite3_clear_bindings (relation_statement);
            sqlite3_bind_int64 (
                relation_statement,
                1,
                source_id
            );
            sqlite3_bind_text (
                relation_statement,
                2,
                relation->relation_type,
                -1,
                SQLITE_STATIC
            );

            if (relation->native_id != NULL) {
                sqlite3_bind_text (
                    relation_statement,
                    3,
                    relation->native_id,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (relation_statement, 3);
            }

            sqlite3_bind_text (
                relation_statement,
                4,
                logical_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                relation_statement,
                5,
                relation->locator,
                -1,
                SQLITE_STATIC
            );

            if (from_logical != NULL) {
                sqlite3_bind_text (
                    relation_statement,
                    6,
                    from_logical,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (relation_statement, 6);
            }

            if (to_logical != NULL) {
                sqlite3_bind_text (
                    relation_statement,
                    7,
                    to_logical,
                    -1,
                    SQLITE_STATIC
                );
            } else {
                sqlite3_bind_null (relation_statement, 7);
            }

            sqlite3_bind_text (
                relation_statement,
                8,
                relation->payload_json,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (relation_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert structured relation from '%s': %s",
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_id);
                atm_structured_json_records_free (records);
                goto out;
            }

            sqlite3_int64 relation_id = sqlite3_last_insert_rowid (db);

            sqlite3_reset (fts_statement);
            sqlite3_clear_bindings (fts_statement);
            sqlite3_bind_text (
                fts_statement,
                1,
                "relation",
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_int64 (
                fts_statement,
                2,
                relation_id
            );
            sqlite3_bind_text (
                fts_statement,
                3,
                logical_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                fts_statement,
                4,
                relation->native_id != NULL
                    ? relation->native_id
                    : relation->relation_type,
                -1,
                SQLITE_STATIC
            );
            sqlite3_bind_text (
                fts_statement,
                5,
                relation->payload_json,
                -1,
                SQLITE_STATIC
            );

            if (sqlite3_step (fts_statement) != SQLITE_DONE) {
                g_set_error (
                    error,
                    ATM_RETRIEVAL_INDEX_ERROR,
                    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                    "Could not insert FTS relation from '%s': %s",
                    source->path,
                    sqlite3_errmsg (db)
                );
                g_free (logical_id);
                atm_structured_json_records_free (records);
                goto out;
            }

            g_free (logical_id);
        }

        atm_structured_json_records_free (records);
    }

    ok = TRUE;

out:
    g_clear_pointer (&native_to_logical, g_hash_table_unref);
    g_clear_pointer (&native_counts, g_hash_table_unref);

    if (fts_statement != NULL) {
        sqlite3_finalize (fts_statement);
    }

    if (relation_statement != NULL) {
        sqlite3_finalize (relation_statement);
    }

    if (entity_statement != NULL) {
        sqlite3_finalize (entity_statement);
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
    gboolean include_content,
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
        (include_content &&
         snapshot_root != NULL &&
         source_catalog != NULL &&
         !insert_tabular_datasets (
             db,
             snapshot_root,
             metadata->repository_id,
             source_catalog,
             error
         )) ||
        (include_content &&
         snapshot_root != NULL &&
         source_catalog != NULL &&
         !insert_structured_json_content (
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
        FALSE,
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
        FALSE,
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
        FALSE,
        out_index_path,
        error
    );
}

gboolean
atm_retrieval_index_create_with_content (
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
        TRUE,
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
