#include "retrieval_fts_consistency.h"
#include "retrieval_index.h"

#include <string.h>

/*
 * Keep the index error domain stable without linking this shared validator
 * against retrieval_index.c (some focused query tests compile it alone).
 */
#undef ATM_RETRIEVAL_INDEX_ERROR
#define ATM_RETRIEVAL_INDEX_ERROR \
    (g_quark_from_static_string ("atm-retrieval-index-error-quark"))


static void
log_validator_query_plan (
    sqlite3 *db,
    const char *label,
    const char *sql
)
{
    if (g_getenv ("ATM_RETRIEVAL_VALIDATOR_EXPLAIN") == NULL) {
        return;
    }

    char *explain_sql = sqlite3_mprintf (
        "EXPLAIN QUERY PLAN %s",
        sql
    );
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2 (
        db,
        explain_sql,
        -1,
        &statement,
        NULL
    );
    sqlite3_free (explain_sql);

    if (rc != SQLITE_OK) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_PLAN_ERROR label=%s error=%s\n",
            label,
            sqlite3_errmsg (db)
        );
        return;
    }

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        const char *detail = (const char *) sqlite3_column_text (
            statement,
            3
        );
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_PLAN label=%s id=%d parent=%d detail=%s\n",
            label,
            sqlite3_column_int (statement, 0),
            sqlite3_column_int (statement, 1),
            detail != NULL ? detail : "(null)"
        );
    }

    if (rc != SQLITE_DONE) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_PLAN_ERROR label=%s error=%s\n",
            label,
            sqlite3_errmsg (db)
        );
    }

    sqlite3_finalize (statement);
}

/*
 * Measurement-only alternative to the nested GROUP BY identity count.
 * COUNT(DISTINCT) excludes NULL, so subtract one NULL group when present to
 * preserve GROUP BY duplicate-count semantics. This probe never decides
 * whether an index is valid and is disabled unless explicitly requested.
 */
static void
measure_distinct_fts_identity_counts (sqlite3 *db, const guint64 expected_counts[4])
{
    static const char *sql =
        "SELECT evidence_kind, COUNT(DISTINCT evidence_id), "
        "COUNT(*) - COUNT(DISTINCT evidence_id) - "
        "CASE WHEN COUNT(evidence_id) < COUNT(*) THEN 1 ELSE 0 END "
        "FROM search_fts GROUP BY evidence_kind;";
    guint64 counts[4] = { 0, 0, 0, 0 };
    guint64 duplicates[4] = { 0, 0, 0, 0 };
    gboolean equivalent = TRUE;
    sqlite3_stmt *statement = NULL;
    gint64 started_us = g_get_monotonic_time ();
    int rc = sqlite3_prepare_v2 (db, sql, -1, &statement, NULL);

    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
            const char *kind = (const char *) sqlite3_column_text (statement, 0);
            gint index = -1;
            if (g_strcmp0 (kind, "section") == 0) index = 0;
            else if (g_strcmp0 (kind, "entity") == 0) index = 1;
            else if (g_strcmp0 (kind, "relation") == 0) index = 2;
            else if (g_strcmp0 (kind, "dataset_row") == 0) index = 3;

            if (index < 0) {
                equivalent = FALSE;
                continue;
            }
            counts[index] = (guint64) sqlite3_column_int64 (statement, 1);
            duplicates[index] = (guint64) sqlite3_column_int64 (statement, 2);
            if (counts[index] != expected_counts[index] ||
                duplicates[index] != 0) {
                equivalent = FALSE;
            }
        }
    }

    if (rc != SQLITE_DONE) {
        equivalent = FALSE;
    }
    sqlite3_finalize (statement);

    g_printerr (
        "ATM_RETRIEVAL_VALIDATOR_DISTINCT result=%s elapsed_us=%"
        G_GINT64_FORMAT " section=%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT
        " entity=%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT
        " relation=%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT
        " dataset_row=%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT,
        equivalent ? "equivalent" : "different",
        g_get_monotonic_time () - started_us,
        counts[0], duplicates[0],
        counts[1], duplicates[1],
        counts[2], duplicates[2],
        counts[3], duplicates[3]
    );
    if (rc != SQLITE_DONE) {
        g_printerr (" error=%s", sqlite3_errmsg (db));
    }
    g_printerr ("\n");
}

static gboolean
validate_canonical_coverage_counts (sqlite3 *db, GError **error)
{
    static const char *fts_counts_sql =
        "SELECT evidence_kind, SUM(identity_count), SUM(identity_count - 1) "
        "FROM ("
        "SELECT evidence_kind, evidence_id, COUNT(*) AS identity_count "
        "FROM search_fts GROUP BY evidence_kind, evidence_id"
        ") GROUP BY evidence_kind;";
    static const char *canonical_counts_sql =
        "SELECT (SELECT count(*) FROM document_sections), "
        "(SELECT count(*) FROM structured_entities), "
        "(SELECT count(*) FROM structured_relations), "
        "(SELECT count(*) FROM dataset_rows);";
    guint64 fts_counts[4] = { 0, 0, 0, 0 };
    sqlite3_stmt *statement = NULL;
    gint64 coverage_started_us;
    gint64 fts_identity_us = 0;
    gint64 canonical_population_us = 0;

    if (g_getenv ("ATM_RETRIEVAL_VALIDATOR_EXPLAIN") != NULL) {
        log_validator_query_plan (db, "fts_identity_counts", fts_counts_sql);
        log_validator_query_plan (db, "canonical_population_counts", canonical_counts_sql);
    }

    coverage_started_us = g_get_monotonic_time ();
    int rc = sqlite3_prepare_v2 (db, fts_counts_sql, -1, &statement, NULL);

    if (rc != SQLITE_OK) {
        g_set_error (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare FTS identity counts: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        const char *kind = (const char *) sqlite3_column_text (statement, 0);
        gint index = -1;
        if (g_strcmp0 (kind, "section") == 0) index = 0;
        else if (g_strcmp0 (kind, "entity") == 0) index = 1;
        else if (g_strcmp0 (kind, "relation") == 0) index = 2;
        else if (g_strcmp0 (kind, "dataset_row") == 0) index = 3;

        if (index < 0) {
            sqlite3_finalize (statement);
            g_set_error_literal (error, ATM_RETRIEVAL_INDEX_ERROR,
                ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
                "FTS contains an unknown evidence kind.");
            return FALSE;
        }

        fts_counts[index] = (guint64) sqlite3_column_int64 (statement, 1);
        if (sqlite3_column_int64 (statement, 2) != 0) {
            sqlite3_finalize (statement);
            g_set_error_literal (error, ATM_RETRIEVAL_INDEX_ERROR,
                ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
                "FTS contains duplicate evidence identities.");
            return FALSE;
        }
    }

    sqlite3_finalize (statement);
    statement = NULL;
    fts_identity_us = g_get_monotonic_time () - coverage_started_us;
    if (rc == SQLITE_DONE &&
        g_getenv ("ATM_RETRIEVAL_VALIDATOR_DISTINCT_TIMING") != NULL) {
        measure_distinct_fts_identity_counts (db, fts_counts);
    }
    if (rc != SQLITE_DONE) {
        g_set_error (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not read FTS identity counts: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    gint64 canonical_started_us = g_get_monotonic_time ();
    rc = sqlite3_prepare_v2 (db, canonical_counts_sql, -1, &statement, NULL);
    if (rc != SQLITE_OK) {
        g_set_error (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare canonical evidence counts: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    rc = sqlite3_step (statement);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        g_set_error (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not read canonical evidence counts: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    for (guint i = 0; i < G_N_ELEMENTS (fts_counts); i++) {
        guint64 canonical_count =
            (guint64) sqlite3_column_int64 (statement, (int) i);
        if (canonical_count != fts_counts[i]) {
            sqlite3_finalize (statement);
            g_set_error_literal (error, ATM_RETRIEVAL_INDEX_ERROR,
                ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
                "FTS and canonical evidence populations differ.");
            return FALSE;
        }
    }

    sqlite3_finalize (statement);
    canonical_population_us = g_get_monotonic_time () - canonical_started_us;

    if (g_getenv ("ATM_RETRIEVAL_VALIDATOR_TIMING") != NULL) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_COVERAGE fts_identity_us=%" G_GINT64_FORMAT
            " canonical_population_us=%" G_GINT64_FORMAT
            " total_us=%" G_GINT64_FORMAT "\n",
            fts_identity_us,
            canonical_population_us,
            g_get_monotonic_time () - coverage_started_us
        );
    }
    return TRUE;
}


/*
 * Measurement-only decomposition of the FTS-to-canonical pass.
 * This is deliberately opt-in and does not participate in validation results.
 * Each query walks one evidence-kind population and counts mismatches, so the
 * diagnostic cannot stop at the first valid row as the production probe does.
 */
static void
measure_validator_branch_timings (sqlite3 *db)
{
    static const struct {
        const char *kind;
        const char *sql;
    } probes[] = {
        {
            "section",
            "SELECT count(*), COALESCE(sum(CASE WHEN "
            "f.evidence_id IS NULL OR typeof(f.evidence_id) <> 'integer' OR "
            "f.logical_source_id IS NULL OR NOT EXISTS ("
            "SELECT 1 FROM document_sections d WHERE d.id = f.evidence_id "
            "AND d.logical_source_id IS f.logical_source_id "
            "AND d.title IS f.title AND d.body IS f.body) "
            "THEN 1 ELSE 0 END), 0) "
            "FROM search_fts f WHERE f.evidence_kind = 'section';"
        },
        {
            "entity",
            "SELECT count(*), COALESCE(sum(CASE WHEN "
            "f.evidence_id IS NULL OR typeof(f.evidence_id) <> 'integer' OR "
            "f.logical_source_id IS NULL OR NOT EXISTS ("
            "SELECT 1 FROM structured_entities e WHERE e.id = f.evidence_id "
            "AND e.logical_source_id IS f.logical_source_id "
            "AND COALESCE(e.label, e.native_id) IS f.title "
            "AND e.payload_json IS f.body) "
            "THEN 1 ELSE 0 END), 0) "
            "FROM search_fts f WHERE f.evidence_kind = 'entity';"
        },
        {
            "relation",
            "SELECT count(*), COALESCE(sum(CASE WHEN "
            "f.evidence_id IS NULL OR typeof(f.evidence_id) <> 'integer' OR "
            "f.logical_source_id IS NULL OR NOT EXISTS ("
            "SELECT 1 FROM structured_relations r WHERE r.id = f.evidence_id "
            "AND r.logical_source_id IS f.logical_source_id "
            "AND COALESCE(r.native_id, r.relation_type) IS f.title "
            "AND r.payload_json IS f.body) "
            "THEN 1 ELSE 0 END), 0) "
            "FROM search_fts f WHERE f.evidence_kind = 'relation';"
        },
        {
            "dataset_row",
            "SELECT count(*), COALESCE(sum(CASE WHEN "
            "f.evidence_id IS NULL OR typeof(f.evidence_id) <> 'integer' OR "
            "f.logical_source_id IS NULL OR NOT EXISTS ("
            "SELECT 1 FROM dataset_rows r "
            "JOIN datasets d ON d.id = r.dataset_id "
            "JOIN source_files s ON s.id = d.source_id "
            "JOIN snapshot_metadata m ON m.id = 1 "
            "WHERE r.id = f.evidence_id "
            "AND printf('%s:dataset-row:%s:%d', m.repository_id, s.path, r.ordinal) "
            "IS f.logical_source_id AND s.path IS f.title "
            "AND r.search_text IS f.body) "
            "THEN 1 ELSE 0 END), 0) "
            "FROM search_fts f WHERE f.evidence_kind = 'dataset_row';"
        },
        {
            "dataset_row_join",
            "SELECT count(*), COALESCE(sum(CASE WHEN "
            "f.evidence_id IS NULL OR typeof(f.evidence_id) <> 'integer' OR "
            "f.logical_source_id IS NULL OR r.id IS NULL OR d.id IS NULL OR "
            "s.id IS NULL OR m.id IS NULL OR "
            "printf('%s:dataset-row:%s:%d', m.repository_id, s.path, r.ordinal) "
            "IS NOT f.logical_source_id OR s.path IS NOT f.title OR "
            "r.search_text IS NOT f.body "
            "THEN 1 ELSE 0 END), 0) "
            "FROM search_fts f "
            "LEFT JOIN dataset_rows r ON r.id = f.evidence_id "
            "LEFT JOIN datasets d ON d.id = r.dataset_id "
            "LEFT JOIN source_files s ON s.id = d.source_id "
            "LEFT JOIN snapshot_metadata m ON m.id = 1 "
            "WHERE f.evidence_kind = 'dataset_row';"
        }
    };

    if (g_getenv ("ATM_RETRIEVAL_VALIDATOR_BRANCH_TIMING") == NULL) {
        return;
    }

    for (gsize i = 0; i < G_N_ELEMENTS (probes); i++) {
        sqlite3_stmt *statement = NULL;
        gint64 started_us = g_get_monotonic_time ();
        int rc = sqlite3_prepare_v2 (
            db, probes[i].sql, -1, &statement, NULL
        );
        if (rc == SQLITE_OK) {
            rc = sqlite3_step (statement);
        }

        if (rc == SQLITE_ROW) {
            gint64 rows = sqlite3_column_int64 (statement, 0);
            gint64 mismatches = sqlite3_column_int64 (statement, 1);
            g_printerr (
                "ATM_RETRIEVAL_VALIDATOR_BRANCH kind=%s rows=%" G_GINT64_FORMAT
                " mismatches=%" G_GINT64_FORMAT " elapsed_us=%" G_GINT64_FORMAT "\n",
                probes[i].kind,
                rows,
                mismatches,
                g_get_monotonic_time () - started_us
            );
        } else {
            g_printerr (
                "ATM_RETRIEVAL_VALIDATOR_BRANCH_ERROR kind=%s error=%s\n",
                probes[i].kind,
                sqlite3_errmsg (db)
            );
        }

        sqlite3_finalize (statement);
    }
}

/*
 * Compare the complete canonical-consistency pass with a joined dataset-row
 * branch. This is diagnostic-only: production continues to use the query
 * above until equivalence and end-to-end performance are established.
 */
static void
measure_joined_dataset_validator_timing (sqlite3 *db)
{
    static const char *sql =
        "SELECT 1 FROM search_fts f "
        "LEFT JOIN dataset_rows dr ON "
        "f.evidence_kind = 'dataset_row' AND dr.id = f.evidence_id "
        "LEFT JOIN datasets dd ON dd.id = dr.dataset_id "
        "LEFT JOIN source_files ds ON ds.id = dd.source_id "
        "LEFT JOIN snapshot_metadata dm ON dm.id = 1 "
        "WHERE f.evidence_kind IS NULL OR f.evidence_id IS NULL OR "
        "typeof(f.evidence_id) <> 'integer' OR "
        "f.logical_source_id IS NULL OR "
        "(f.evidence_kind = 'section' AND NOT EXISTS ("
        "SELECT 1 FROM document_sections d WHERE "
        "d.id = f.evidence_id "
        "AND d.logical_source_id IS f.logical_source_id "
        "AND d.title IS f.title AND d.body IS f.body)) OR "
        "(f.evidence_kind = 'entity' AND NOT EXISTS ("
        "SELECT 1 FROM structured_entities e WHERE "
        "e.id = f.evidence_id "
        "AND e.logical_source_id IS f.logical_source_id "
        "AND COALESCE(e.label, e.native_id) IS f.title "
        "AND e.payload_json IS f.body)) OR "
        "(f.evidence_kind = 'relation' AND NOT EXISTS ("
        "SELECT 1 FROM structured_relations r WHERE "
        "r.id = f.evidence_id "
        "AND r.logical_source_id IS f.logical_source_id "
        "AND COALESCE(r.native_id, r.relation_type) IS f.title "
        "AND r.payload_json IS f.body)) OR "
        "(f.evidence_kind = 'dataset_row' AND ("
        "dr.id IS NULL OR dd.id IS NULL OR ds.id IS NULL OR dm.id IS NULL OR "
        "printf('%s:dataset-row:%s:%d', dm.repository_id, ds.path, dr.ordinal) "
        "IS NOT f.logical_source_id OR ds.path IS NOT f.title OR "
        "dr.search_text IS NOT f.body)) OR "
        "f.evidence_kind NOT IN ('section','entity','relation','dataset_row');";

    log_validator_query_plan (db, "fts_to_canonical_joined_dataset", sql);

    sqlite3_stmt *statement = NULL;
    gint64 started_us = g_get_monotonic_time ();
    int rc = sqlite3_prepare_v2 (db, sql, -1, &statement, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step (statement);
    }

    const char *result =
        rc == SQLITE_DONE ? "valid" :
        rc == SQLITE_ROW ? "mismatch" : "error";
    g_printerr (
        "ATM_RETRIEVAL_VALIDATOR_JOINED_DATASET result=%s elapsed_us=%"
        G_GINT64_FORMAT,
        result,
        g_get_monotonic_time () - started_us
    );
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        g_printerr (" error=%s", sqlite3_errmsg (db));
    }
    g_printerr ("\n");
    sqlite3_finalize (statement);
}

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
    gint64 validation_started_us = g_get_monotonic_time ();
    gint64 foreign_key_check_us = 0;
    gint64 canonical_query_us[2] = { 0, 0 };

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

    /*
     * PRAGMA foreign_keys=ON only governs writes on the current connection;
     * it does not validate rows already persisted with enforcement disabled.
     * Check the stored index explicitly before deriving canonical FTS coverage.
     */
    rc = sqlite3_prepare_v2 (
        db,
        "PRAGMA foreign_key_check;",
        -1,
        &statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        g_set_error (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare retrieval-index foreign-key check: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    gint64 foreign_key_started_us = g_get_monotonic_time ();
    rc = sqlite3_step (statement);
    foreign_key_check_us = g_get_monotonic_time () - foreign_key_started_us;
    sqlite3_finalize (statement);
    statement = NULL;
    if (rc == SQLITE_ROW) {
        g_set_error_literal (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "Retrieval index contains foreign-key violations."
        );
        return FALSE;
    }
    if (rc != SQLITE_DONE) {
        g_set_error (
            error, ATM_RETRIEVAL_INDEX_ERROR, ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not execute retrieval-index foreign-key check: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }


    const char *queries[] = {
        "SELECT 1 FROM search_fts f WHERE "
        "f.evidence_kind IS NULL OR f.evidence_id IS NULL OR "
        "typeof(f.evidence_id) <> 'integer' OR "
        "f.logical_source_id IS NULL OR "
        "(f.evidence_kind = 'section' AND NOT EXISTS ("
        "SELECT 1 FROM document_sections d WHERE "
        "d.id = f.evidence_id "
        "AND d.logical_source_id IS f.logical_source_id "
        "AND d.title IS f.title AND d.body IS f.body)) OR "
        "(f.evidence_kind = 'entity' AND NOT EXISTS ("
        "SELECT 1 FROM structured_entities e WHERE "
        "e.id = f.evidence_id "
        "AND e.logical_source_id IS f.logical_source_id "
        "AND COALESCE(e.label, e.native_id) IS f.title "
        "AND e.payload_json IS f.body)) OR "
        "(f.evidence_kind = 'relation' AND NOT EXISTS ("
        "SELECT 1 FROM structured_relations r WHERE "
        "r.id = f.evidence_id "
        "AND r.logical_source_id IS f.logical_source_id "
        "AND COALESCE(r.native_id, r.relation_type) IS f.title "
        "AND r.payload_json IS f.body)) OR "
        "(f.evidence_kind = 'dataset_row' AND NOT EXISTS ("
        "SELECT 1 FROM dataset_rows r "
        "JOIN datasets d ON d.id = r.dataset_id "
        "JOIN source_files s ON s.id = d.source_id "
        "JOIN snapshot_metadata m ON m.id = 1 "
        "WHERE r.id = f.evidence_id "
        "AND printf('%s:dataset-row:%s:%d', m.repository_id, s.path, r.ordinal) "
        "IS f.logical_source_id AND s.path IS f.title "
        "AND r.search_text IS f.body)) OR "
        "f.evidence_kind NOT IN ('section','entity','relation','dataset_row');"
    };


    log_validator_query_plan (db, "fts_to_canonical", queries[0]);

    gint64 query_started_us = g_get_monotonic_time ();
    rc = sqlite3_prepare_v2 (db, queries[0], -1, &statement, NULL);
    if (rc != SQLITE_OK) {
        g_set_error (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not prepare FTS canonical-consistency query: %s",
            sqlite3_errmsg (db));
        return FALSE;
    }

    rc = sqlite3_step (statement);
    sqlite3_finalize (statement);
    statement = NULL;
    if (rc == SQLITE_ROW) {
        g_set_error_literal (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
            "FTS evidence and canonical retrieval records are inconsistent.");
        return FALSE;
    }
    canonical_query_us[0] = g_get_monotonic_time () - query_started_us;
    if (rc != SQLITE_DONE) {
        g_set_error (error, ATM_RETRIEVAL_INDEX_ERROR,
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
            "Could not execute FTS canonical-consistency query: %s",
            sqlite3_errmsg (db));
        return FALSE;
    }

    measure_validator_branch_timings (db);
    if (g_getenv ("ATM_RETRIEVAL_VALIDATOR_BRANCH_TIMING") != NULL) {
        measure_joined_dataset_validator_timing (db);
    }

    query_started_us = g_get_monotonic_time ();
    if (!validate_canonical_coverage_counts (db, error)) return FALSE;
    canonical_query_us[1] = g_get_monotonic_time () - query_started_us;

    if (g_getenv ("ATM_RETRIEVAL_VALIDATOR_TIMING") != NULL) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_TIMING fk_us=%" G_GINT64_FORMAT
            " canonical0_us=%" G_GINT64_FORMAT
            " canonical1_us=%" G_GINT64_FORMAT
            " total_us=%" G_GINT64_FORMAT "\n",
            foreign_key_check_us,
            canonical_query_us[0],
            canonical_query_us[1],
            g_get_monotonic_time () - validation_started_us
        );
    }
    return TRUE;
}

