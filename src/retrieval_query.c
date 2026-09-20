#include "retrieval_query.h"

#include <sqlite3.h>

#include <string.h>

GQuark
atm_retrieval_query_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-query-error-quark"
    );
}

void
atm_evidence_record_free (AtmEvidenceRecord *record)
{
    if (record == NULL) {
        return;
    }

    g_free (record->evidence_kind);
    g_free (record->logical_source_id);
    g_free (record->source_path);
    g_free (record->locator);
    g_free (record->title);
    g_free (record->body);
    g_free (record);
}

static guint
source_role_bit_from_name (const char *role)
{
    if (g_strcmp0 (role, "canonical") == 0) {
        return ATM_SOURCE_ROLE_CANONICAL;
    }

    if (g_strcmp0 (role, "structural") == 0) {
        return ATM_SOURCE_ROLE_STRUCTURAL;
    }

    if (g_strcmp0 (role, "evidence") == 0) {
        return ATM_SOURCE_ROLE_EVIDENCE;
    }

    if (g_strcmp0 (role, "tabular") == 0) {
        return ATM_SOURCE_ROLE_TABULAR;
    }

    if (g_strcmp0 (role, "implementation") == 0) {
        return ATM_SOURCE_ROLE_IMPLEMENTATION;
    }

    return 0;
}

static gboolean
load_source_roles (
    sqlite3 *db,
    sqlite3_stmt *role_statement,
    sqlite3_int64 source_id,
    guint *out_roles,
    GError **error
)
{
    guint roles = 0;
    int rc;

    sqlite3_reset (role_statement);
    sqlite3_clear_bindings (role_statement);
    sqlite3_bind_int64 (
        role_statement,
        1,
        source_id
    );

    while ((rc = sqlite3_step (role_statement)) == SQLITE_ROW) {
        const char *name = (const char *) sqlite3_column_text (
            role_statement,
            0
        );
        guint bit = source_role_bit_from_name (name);

        if (bit == 0) {
            g_set_error (
                error,
                ATM_RETRIEVAL_QUERY_ERROR,
                ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
                "Retrieval index contains unknown source role '%s'.",
                name != NULL ? name : "(null)"
            );
            return FALSE;
        }

        roles |= bit;
    }

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not read source roles: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    *out_roles = roles;
    return TRUE;
}

static gboolean
append_exact_rows (
    sqlite3 *db,
    sqlite3_stmt *role_statement,
    const char *sql,
    const char *identifier,
    const char *evidence_kind,
    guint maximum_rows,
    GPtrArray *results,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc;

    if (maximum_rows == 0) {
        return TRUE;
    }

    rc = sqlite3_prepare_v2 (
        db,
        sql,
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare exact retrieval query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    sqlite3_bind_text (
        statement,
        1,
        identifier,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_int (
        statement,
        2,
        (int) maximum_rows
    );

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        AtmEvidenceRecord *record = g_new0 (
            AtmEvidenceRecord,
            1
        );
        sqlite3_int64 source_id = sqlite3_column_int64 (
            statement,
            6
        );

        record->evidence_kind = g_strdup (evidence_kind);
        record->evidence_id = sqlite3_column_int64 (
            statement,
            0
        );
        record->logical_source_id = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                1
            )
        );
        record->source_path = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                2
            )
        );
        record->locator = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                3
            )
        );

        if (sqlite3_column_type (statement, 4) != SQLITE_NULL) {
            record->title = g_strdup (
                (const char *) sqlite3_column_text (
                    statement,
                    4
                )
            );
        }

        if (sqlite3_column_type (statement, 5) != SQLITE_NULL) {
            record->body = g_strdup (
                (const char *) sqlite3_column_text (
                    statement,
                    5
                )
            );
        }

        if (!load_source_roles (
                db,
                role_statement,
                source_id,
                &record->source_roles,
                error
            )) {
            atm_evidence_record_free (record);
            sqlite3_finalize (statement);
            return FALSE;
        }

        g_ptr_array_add (results, record);
    }

    sqlite3_finalize (statement);

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not finish exact retrieval query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_retrieval_lookup_exact (
    const char *index_path,
    const char *identifier,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    static const char *entity_sql =
        "SELECT e.id, e.logical_source_id, s.path, e.locator, "
        "COALESCE(e.label, e.native_id), e.payload_json, s.id "
        "FROM structured_entities e "
        "JOIN source_files s ON s.id = e.source_id "
        "WHERE e.native_id = ?1 COLLATE BINARY "
        "OR e.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, e.locator COLLATE BINARY "
        "LIMIT ?2;";

    static const char *relation_sql =
        "SELECT r.id, r.logical_source_id, s.path, r.locator, "
        "COALESCE(r.native_id, r.relation_type), r.payload_json, s.id "
        "FROM structured_relations r "
        "JOIN source_files s ON s.id = r.source_id "
        "WHERE r.native_id = ?1 COLLATE BINARY "
        "OR r.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, r.locator COLLATE BINARY "
        "LIMIT ?2;";

    static const char *dataset_sql =
        "SELECT d.id, d.logical_source_id, s.path, d.locator, "
        "COALESCE(d.title, d.native_id, d.logical_source_id), "
        "d.metadata_json, s.id "
        "FROM datasets d "
        "JOIN source_files s ON s.id = d.source_id "
        "WHERE d.native_id = ?1 COLLATE BINARY "
        "OR d.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, d.locator COLLATE BINARY "
        "LIMIT ?2;";

    static const char *section_sql =
        "SELECT d.id, d.logical_source_id, s.path, d.locator, "
        "COALESCE(d.title, d.heading_path, d.logical_source_id), "
        "d.body, s.id "
        "FROM document_sections d "
        "JOIN source_files s ON s.id = d.source_id "
        "WHERE d.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, d.locator COLLATE BINARY "
        "LIMIT ?2;";

    sqlite3 *db = NULL;
    sqlite3_stmt *role_statement = NULL;
    GPtrArray *results = NULL;
    gboolean ok = FALSE;
    int rc;

    g_return_val_if_fail (index_path != NULL, FALSE);
    g_return_val_if_fail (identifier != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    gsize identifier_length = strlen (identifier);

    if (identifier_length == 0 ||
        identifier_length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
        max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS ||
        !g_utf8_validate (identifier, identifier_length, NULL)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Exact retrieval arguments are invalid."
        );
        return FALSE;
    }

    rc = sqlite3_open_v2 (
        index_path,
        &db,
        SQLITE_OPEN_READONLY,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not open retrieval index read-only: %s",
            db != NULL ? sqlite3_errmsg (db) : "unknown SQLite error"
        );
        goto out;
    }

    if (sqlite3_exec (
            db,
            "PRAGMA trusted_schema = OFF;"
            "PRAGMA query_only = ON;"
            "PRAGMA foreign_keys = ON;",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not configure read-only retrieval connection: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    if (sqlite3_prepare_v2 (
            db,
            "SELECT role FROM source_roles "
            "WHERE source_id = ?1 ORDER BY role COLLATE BINARY;",
            -1,
            &role_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare source-role retrieval query: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    results = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_evidence_record_free
    );

    const struct {
        const char *sql;
        const char *kind;
    } queries[] = {
        { entity_sql, "entity" },
        { relation_sql, "relation" },
        { dataset_sql, "dataset" },
        { section_sql, "section" }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (queries); i++) {
        guint remaining = max_results - results->len;

        if (remaining == 0) {
            break;
        }

        if (!append_exact_rows (
                db,
                role_statement,
                queries[i].sql,
                identifier,
                queries[i].kind,
                remaining,
                results,
                error
            )) {
            goto out;
        }
    }

    *out_results = g_steal_pointer (&results);
    ok = TRUE;

out:
    g_clear_pointer (&results, g_ptr_array_unref);

    if (role_statement != NULL) {
        sqlite3_finalize (role_statement);
    }

    if (db != NULL) {
        sqlite3_close (db);
    }

    return ok;
}
