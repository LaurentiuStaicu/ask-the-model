#include "retrieval_fts_consistency.h"
#include "retrieval_index.h"

#include <string.h>

gboolean
atm_retrieval_index_validate_fts_canonical_consistency (
    sqlite3 *db,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    const char *schema_sql = NULL;
    char *upper_schema_sql = NULL;
    int rc;

    rc = sqlite3_prepare_v2 (
        db,
        "SELECT sql FROM sqlite_master "
        "WHERE type = 'table' AND name = 'search_fts';",
        -1,
        &statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        g_set_error (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not inspect the FTS schema: %s", sqlite3_errmsg (db)
        );
        return FALSE;
    }

    rc = sqlite3_step (statement);
    if (rc == SQLITE_ROW) {
        schema_sql = (const char *) sqlite3_column_text (statement, 0);
        if (schema_sql != NULL)
            upper_schema_sql = g_ascii_strup (schema_sql, -1);
    }

    if (rc != SQLITE_ROW ||
        upper_schema_sql == NULL ||
        !g_str_has_prefix (upper_schema_sql, "CREATE VIRTUAL TABLE") ||
        strstr (upper_schema_sql, "USING FTS5") == NULL ||
        sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_free (upper_schema_sql);
        g_set_error_literal (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Retrieval index does not contain the required FTS5 virtual table."
        );
        return FALSE;
    }
    sqlite3_finalize (statement);
    statement = NULL;
    g_free (upper_schema_sql);

    static const char *expected_columns[] = {
        "evidence_kind",
        "evidence_id",
        "logical_source_id",
        "title",
        "body"
    };
    rc = sqlite3_prepare_v2 (
        db,
        "PRAGMA table_info(search_fts);",
        -1,
        &statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        g_set_error (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not inspect FTS columns: %s", sqlite3_errmsg (db)
        );
        return FALSE;
    }
    for (gsize i = 0; i < G_N_ELEMENTS (expected_columns); i++) {
        rc = sqlite3_step (statement);
        if (rc != SQLITE_ROW ||
            g_strcmp0 (
                (const char *) sqlite3_column_text (statement, 1),
                expected_columns[i]
            ) != 0) {
            sqlite3_finalize (statement);
            g_set_error_literal (
                error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
                "FTS5 columns do not match the retrieval-index v2 contract."
            );
            return FALSE;
        }
    }
    rc = sqlite3_step (statement);
    sqlite3_finalize (statement);
    statement = NULL;
    if (rc != SQLITE_DONE) {
        g_set_error_literal (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "FTS5 columns do not match the retrieval-index v2 contract."
        );
        return FALSE;
    }

    const char *queries[] = {
        "SELECT 1 FROM search_fts f WHERE "
        "f.evidence_kind IS NULL OR f.evidence_id IS NULL OR "
        "f.logical_source_id IS NULL OR "
        "(f.evidence_kind = 'section' AND NOT EXISTS ("
        "SELECT 1 FROM document_sections d WHERE "
        "d.id = f.evidence_id AND CAST(d.id AS TEXT) = CAST(f.evidence_id AS TEXT) "
        "AND d.logical_source_id IS f.logical_source_id "
        "AND d.title IS f.title AND d.body IS f.body)) OR "
        "(f.evidence_kind = 'entity' AND NOT EXISTS ("
        "SELECT 1 FROM structured_entities e WHERE "
        "e.id = f.evidence_id AND CAST(e.id AS TEXT) = CAST(f.evidence_id AS TEXT) "
        "AND e.logical_source_id IS f.logical_source_id "
        "AND COALESCE(e.label, e.native_id) IS f.title "
        "AND e.payload_json IS f.body)) OR "
        "(f.evidence_kind = 'relation' AND NOT EXISTS ("
        "SELECT 1 FROM structured_relations r WHERE "
        "r.id = f.evidence_id AND CAST(r.id AS TEXT) = CAST(f.evidence_id AS TEXT) "
        "AND r.logical_source_id IS f.logical_source_id "
        "AND COALESCE(r.native_id, r.relation_type) IS f.title "
        "AND r.payload_json IS f.body)) OR "
        "(f.evidence_kind = 'dataset_row' AND NOT EXISTS ("
        "SELECT 1 FROM dataset_rows r "
        "JOIN datasets d ON d.id = r.dataset_id "
        "JOIN source_files s ON s.id = d.source_id "
        "JOIN snapshot_metadata m ON m.id = 1 "
        "WHERE r.id = f.evidence_id AND CAST(r.id AS TEXT) = CAST(f.evidence_id AS TEXT) "
        "AND printf('%s:dataset-row:%s:%d', m.repository_id, s.path, r.ordinal) "
        "IS f.logical_source_id AND s.path IS f.title "
        "AND r.search_text IS f.body)) OR "
        "f.evidence_kind NOT IN ('section','entity','relation','dataset_row');",

        "SELECT 1 FROM ("
        "SELECT 'section', CAST(d.id AS TEXT), d.logical_source_id, d.title, d.body "
        "FROM document_sections d "
        "EXCEPT SELECT evidence_kind, CAST(evidence_id AS TEXT), logical_source_id, title, body "
        "FROM search_fts WHERE evidence_kind = 'section') "
        "UNION ALL SELECT 1 FROM ("
        "SELECT 'entity', CAST(e.id AS TEXT), e.logical_source_id, "
        "COALESCE(e.label,e.native_id), e.payload_json FROM structured_entities e "
        "EXCEPT SELECT evidence_kind, CAST(evidence_id AS TEXT), logical_source_id, title, body "
        "FROM search_fts WHERE evidence_kind = 'entity') "
        "UNION ALL SELECT 1 FROM ("
        "SELECT 'relation', CAST(r.id AS TEXT), r.logical_source_id, "
        "COALESCE(r.native_id,r.relation_type), r.payload_json FROM structured_relations r "
        "EXCEPT SELECT evidence_kind, CAST(evidence_id AS TEXT), logical_source_id, title, body "
        "FROM search_fts WHERE evidence_kind = 'relation') "
        "UNION ALL SELECT 1 FROM ("
        "SELECT 'dataset_row', CAST(r.id AS TEXT), "
        "printf('%s:dataset-row:%s:%d',m.repository_id,s.path,r.ordinal), "
        "s.path, r.search_text FROM dataset_rows r "
        "JOIN datasets d ON d.id=r.dataset_id "
        "JOIN source_files s ON s.id=d.source_id "
        "JOIN snapshot_metadata m ON m.id=1 "
        "EXCEPT SELECT evidence_kind,CAST(evidence_id AS TEXT),logical_source_id,title,body "
        "FROM search_fts WHERE evidence_kind='dataset_row') "
        "UNION ALL SELECT 1 FROM ("
        "SELECT evidence_kind,evidence_id FROM search_fts "
        "GROUP BY evidence_kind,CAST(evidence_id AS TEXT) HAVING count(*) > 1) "
        "LIMIT 1;"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (queries); i++) {
        rc = sqlite3_prepare_v2 (db, queries[i], -1, &statement, NULL);
        if (rc != SQLITE_OK) {
            g_set_error (
                error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                "Could not prepare FTS canonical-consistency query: %s",
                sqlite3_errmsg (db)
            );
            return FALSE;
        }

        rc = sqlite3_step (statement);
        sqlite3_finalize (statement);
        statement = NULL;

        if (rc == SQLITE_ROW) {
            g_set_error_literal (
                error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
                "FTS evidence and canonical retrieval records are inconsistent."
            );
            return FALSE;
        }
        if (rc != SQLITE_DONE) {
            g_set_error (
                error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
                "Could not execute FTS canonical-consistency query: %s",
                sqlite3_errmsg (db)
            );
            return FALSE;
        }
    }
    return TRUE;
}

