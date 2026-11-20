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

    g_printerr (
        "ATM_RETRIEVAL_VALIDATOR_SQLITE label=%s version=%s source_id=%s\n",
        label,
        sqlite3_libversion (),
        sqlite3_sourceid ()
    );

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
 * Measurement-only paired comparison of the original nested GROUP BY
 * aggregate and the equivalent COUNT(DISTINCT) aggregate. Both variants run
 * against the same connection and snapshot, in alternating order. This probe
 * never decides whether an index is valid and is disabled unless explicitly
 * requested.
 */
typedef struct {
    guint64 counts[4];
    guint64 duplicates[4];
} FtsIdentityCountResult;

static gint
fts_identity_kind_index (const char *kind)
{
    if (g_strcmp0 (kind, "section") == 0) return 0;
    if (g_strcmp0 (kind, "entity") == 0) return 1;
    if (g_strcmp0 (kind, "relation") == 0) return 2;
    if (g_strcmp0 (kind, "dataset_row") == 0) return 3;
    return -1;
}

static gboolean
execute_fts_identity_count_query (
    sqlite3 *db,
    const char *sql,
    FtsIdentityCountResult *result,
    gint64 *elapsed_us
)
{
    sqlite3_stmt *statement = NULL;
    gboolean seen[4] = { FALSE, FALSE, FALSE, FALSE };
    gint64 started_us = g_get_monotonic_time ();
    int rc = sqlite3_prepare_v2 (db, sql, -1, &statement, NULL);

    memset (result, 0, sizeof (*result));
    if (rc != SQLITE_OK) {
        return FALSE;
    }

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        const char *kind = (const char *) sqlite3_column_text (statement, 0);
        gint index = fts_identity_kind_index (kind);

        if (index < 0 || seen[index]) {
            rc = SQLITE_ERROR;
            break;
        }

        seen[index] = TRUE;
        result->counts[index] =
            (guint64) sqlite3_column_int64 (statement, 1);
        result->duplicates[index] =
            (guint64) sqlite3_column_int64 (statement, 2);
    }

    sqlite3_finalize (statement);
    *elapsed_us = g_get_monotonic_time () - started_us;
    return rc == SQLITE_DONE;
}

static gint64
median_elapsed_us (const gint64 samples[], guint count)
{
    gint64 sorted[9];

    g_return_val_if_fail (count > 0 && count <= G_N_ELEMENTS (sorted), 0);
    memcpy (sorted, samples, count * sizeof (sorted[0]));

    for (guint i = 1; i < count; i++) {
        gint64 value = sorted[i];
        guint j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }

    return sorted[count / 2];
}

static void
measure_fts_identity_aggregate_ab (sqlite3 *db)
{
    static const char *original_sql =
        "SELECT evidence_kind, SUM(identity_count), "
        "SUM(identity_count - 1) FROM ("
        "SELECT evidence_kind, evidence_id, COUNT(*) AS identity_count "
        "FROM search_fts GROUP BY evidence_kind, evidence_id"
        ") GROUP BY evidence_kind;";
    static const char *candidate_sql =
        "SELECT evidence_kind, COUNT(*), "
        "COUNT(*) - COUNT(DISTINCT evidence_id) - "
        "CASE WHEN COUNT(evidence_id) < COUNT(*) THEN 1 ELSE 0 END "
        "FROM search_fts GROUP BY evidence_kind;";
    enum { SAMPLE_COUNT = 9 };
    FtsIdentityCountResult original_result;
    FtsIdentityCountResult candidate_result;
    gint64 original_times[SAMPLE_COUNT] = { 0 };
    gint64 candidate_times[SAMPLE_COUNT] = { 0 };
    gboolean equivalent = TRUE;
    gint64 warmup_us = 0;

    /* Warm both query plans before collecting paired samples. */
    if (!execute_fts_identity_count_query (
            db, original_sql, &original_result, &warmup_us) ||
        !execute_fts_identity_count_query (
            db, candidate_sql, &candidate_result, &warmup_us) ||
        memcmp (&original_result, &candidate_result,
            sizeof (original_result)) != 0) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_AGGREGATE_AB result=warmup_mismatch "
            "error=%s\n",
            sqlite3_errmsg (db)
        );
        return;
    }

    for (guint sample = 0; sample < SAMPLE_COUNT; sample++) {
        gboolean original_first = (sample % 2) == 0;
        gboolean original_ok;
        gboolean candidate_ok;

        if (original_first) {
            original_ok = execute_fts_identity_count_query (
                db, original_sql, &original_result,
                &original_times[sample]);
            candidate_ok = execute_fts_identity_count_query (
                db, candidate_sql, &candidate_result,
                &candidate_times[sample]);
        } else {
            candidate_ok = execute_fts_identity_count_query (
                db, candidate_sql, &candidate_result,
                &candidate_times[sample]);
            original_ok = execute_fts_identity_count_query (
                db, original_sql, &original_result,
                &original_times[sample]);
        }

        if (!original_ok || !candidate_ok ||
            memcmp (&original_result, &candidate_result,
                sizeof (original_result)) != 0) {
            equivalent = FALSE;
            break;
        }
    }

    if (!equivalent) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_AGGREGATE_AB result=different "
            "error=%s\n",
            sqlite3_errmsg (db)
        );
        return;
    }

    gint64 original_median = median_elapsed_us (
        original_times, SAMPLE_COUNT);
    gint64 candidate_median = median_elapsed_us (
        candidate_times, SAMPLE_COUNT);
    gint64 original_max = original_times[0];
    gint64 candidate_max = candidate_times[0];

    for (guint i = 1; i < SAMPLE_COUNT; i++) {
        original_max = MAX (original_max, original_times[i]);
        candidate_max = MAX (candidate_max, candidate_times[i]);
    }

    g_printerr (
        "ATM_RETRIEVAL_VALIDATOR_AGGREGATE_AB result=equivalent "
        "samples=%u original_median_us=%" G_GINT64_FORMAT
        " candidate_median_us=%" G_GINT64_FORMAT
        " original_max_us=%" G_GINT64_FORMAT
        " candidate_max_us=%" G_GINT64_FORMAT
        " median_delta_percent=%.2f\n",
        SAMPLE_COUNT,
        original_median,
        candidate_median,
        original_max,
        candidate_max,
        original_median > 0
            ? ((gdouble) candidate_median / (gdouble) original_median - 1.0) * 100.0
            : 0.0
    );
}

/* Original predicate is available only in the benchmark/test build. */
#ifdef ATM_RETRIEVAL_VALIDATOR_TESTING
static const char *fts_canonical_original_query =
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
        "f.evidence_kind NOT IN ('section','entity','relation','dataset_row');";
#endif

static gboolean
validate_canonical_coverage_counts (sqlite3 *db, GError **error, gboolean diagnostics)
{
    static const char *fts_counts_sql =
        "SELECT evidence_kind, COUNT(*), "
        "COUNT(*) - COUNT(DISTINCT evidence_id) - "
        "CASE WHEN COUNT(evidence_id) < COUNT(*) THEN 1 ELSE 0 END "
        "FROM search_fts GROUP BY evidence_kind;";
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

    if (diagnostics && g_getenv ("ATM_RETRIEVAL_VALIDATOR_EXPLAIN") != NULL) {
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
    if (diagnostics && rc == SQLITE_DONE &&
        g_getenv ("ATM_RETRIEVAL_VALIDATOR_DISTINCT_TIMING") != NULL) {
        measure_fts_identity_aggregate_ab (db);
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

    if (diagnostics && g_getenv ("ATM_RETRIEVAL_VALIDATOR_TIMING") != NULL) {
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
 * Measurement-only paired comparison of the production NOT EXISTS predicate
 * and the equivalent LEFT JOIN form for dataset-row canonical validation.
 * Both run on the same connection/snapshot, with alternating sample order.
 * This probe never participates in the validator's pass/fail decision.
 */
static gint
execute_fts_canonical_predicate_probe (
    sqlite3 *db,
    const char *sql,
    gint64 *elapsed_us
)
{
    sqlite3_stmt *statement = NULL;
    gint64 started_us = g_get_monotonic_time ();
    int rc = sqlite3_prepare_v2 (db, sql, -1, &statement, NULL);

    if (rc == SQLITE_OK) {
        rc = sqlite3_step (statement);
    }

    sqlite3_finalize (statement);
    *elapsed_us = g_get_monotonic_time () - started_us;

    if (rc == SQLITE_DONE) return 0;
    if (rc == SQLITE_ROW) return 1;
    return -1;
}

static void
measure_fts_canonical_predicate_ab (sqlite3 *db)
{
    static const char *original_sql =
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
        "f.evidence_kind NOT IN ('section','entity','relation','dataset_row');";
    static const char *candidate_sql =
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
    enum { SAMPLE_COUNT = 9 };
    gint64 original_times[SAMPLE_COUNT] = { 0 };
    gint64 candidate_times[SAMPLE_COUNT] = { 0 };
    gint64 warmup_us = 0;
    gint original_outcome;
    gint candidate_outcome;

    log_validator_query_plan (
        db, "fts_to_canonical_not_exists_ab", original_sql
    );
    log_validator_query_plan (
        db, "fts_to_canonical_left_join_ab", candidate_sql
    );

    /* Warm both query plans and compare their validation outcomes. */
    original_outcome = execute_fts_canonical_predicate_probe (
        db, original_sql, &warmup_us
    );
    candidate_outcome = execute_fts_canonical_predicate_probe (
        db, candidate_sql, &warmup_us
    );
    if (original_outcome < 0 || candidate_outcome < 0 ||
        original_outcome != candidate_outcome) {
        g_printerr (
            "ATM_RETRIEVAL_VALIDATOR_FTS_PREDICATE_AB "
            "result=warmup_mismatch original=%d candidate=%d error=%s\n",
            original_outcome, candidate_outcome, sqlite3_errmsg (db)
        );
        return;
    }

    for (guint sample = 0; sample < SAMPLE_COUNT; sample++) {
        if ((sample % 2) == 0) {
            original_outcome = execute_fts_canonical_predicate_probe (
                db, original_sql, &original_times[sample]
            );
            candidate_outcome = execute_fts_canonical_predicate_probe (
                db, candidate_sql, &candidate_times[sample]
            );
        } else {
            candidate_outcome = execute_fts_canonical_predicate_probe (
                db, candidate_sql, &candidate_times[sample]
            );
            original_outcome = execute_fts_canonical_predicate_probe (
                db, original_sql, &original_times[sample]
            );
        }

        if (original_outcome < 0 || candidate_outcome < 0 ||
            original_outcome != candidate_outcome) {
            g_printerr (
                "ATM_RETRIEVAL_VALIDATOR_FTS_PREDICATE_AB "
                "result=different sample=%u original=%d candidate=%d error=%s\n",
                sample, original_outcome, candidate_outcome, sqlite3_errmsg (db)
            );
            return;
        }
    }

    gint64 original_median = median_elapsed_us (
        original_times, SAMPLE_COUNT
    );
    gint64 candidate_median = median_elapsed_us (
        candidate_times, SAMPLE_COUNT
    );
    gint64 original_max = original_times[0];
    gint64 candidate_max = candidate_times[0];

    for (guint i = 1; i < SAMPLE_COUNT; i++) {
        original_max = MAX (original_max, original_times[i]);
        candidate_max = MAX (candidate_max, candidate_times[i]);
    }

    g_printerr (
        "ATM_RETRIEVAL_VALIDATOR_FTS_PREDICATE_AB "
        "result=equivalent outcome=%s samples=%u "
        "original_median_us=%" G_GINT64_FORMAT
        " candidate_median_us=%" G_GINT64_FORMAT
        " original_max_us=%" G_GINT64_FORMAT
        " candidate_max_us=%" G_GINT64_FORMAT
        " median_delta_percent=%.2f\n",
        original_outcome == 0 ? "valid" : "mismatch",
        SAMPLE_COUNT,
        original_median,
        candidate_median,
        original_max,
        candidate_max,
        original_median > 0
            ? ((gdouble) candidate_median / (gdouble) original_median - 1.0) * 100.0
            : 0.0
    );
}

static gboolean
validate_fts_canonical_consistency_impl (
    sqlite3 *db,
    GError **error,
    gboolean use_original_predicate,
    gboolean diagnostics
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


    const char *candidate_query =
        "SELECT 1 FROM search_fts f "
        "LEFT JOIN dataset_rows dr ON "
        "f.evidence_kind = 'dataset_row' AND dr.id = f.evidence_id "
        "LEFT JOIN datasets dd ON dd.id = dr.dataset_id "
        "LEFT JOIN source_files ds ON ds.id = dd.source_id "
        "LEFT JOIN snapshot_metadata dm ON dm.id = 1 "
        "WHERE "
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
        "(f.evidence_kind = 'dataset_row' AND ("
        "dr.id IS NULL OR dd.id IS NULL OR ds.id IS NULL OR dm.id IS NULL OR "
        "printf('%s:dataset-row:%s:%d', dm.repository_id, ds.path, dr.ordinal) "
        "IS NOT f.logical_source_id OR ds.path IS NOT f.title OR "
        "dr.search_text IS NOT f.body)) OR "
        "f.evidence_kind NOT IN ('section','entity','relation','dataset_row');";
    const char *canonical_query = candidate_query;
#ifdef ATM_RETRIEVAL_VALIDATOR_TESTING
    if (use_original_predicate)
        canonical_query = fts_canonical_original_query;
#else
    (void) use_original_predicate;
#endif


    if (diagnostics)
        log_validator_query_plan (db, "fts_to_canonical", canonical_query);

    gint64 query_started_us = g_get_monotonic_time ();
    rc = sqlite3_prepare_v2 (db, canonical_query, -1, &statement, NULL);
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

    if (diagnostics) measure_validator_branch_timings (db);
    if (diagnostics && g_getenv ("ATM_RETRIEVAL_VALIDATOR_BRANCH_TIMING") != NULL) {
        measure_fts_canonical_predicate_ab (db);
    }

    query_started_us = g_get_monotonic_time ();
    if (!validate_canonical_coverage_counts (db, error, diagnostics)) return FALSE;
    canonical_query_us[1] = g_get_monotonic_time () - query_started_us;

    if (diagnostics && g_getenv ("ATM_RETRIEVAL_VALIDATOR_TIMING") != NULL) {
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


/* Production entry point is deliberately pinned to the LEFT JOIN candidate. */
gboolean
atm_retrieval_index_validate_fts_canonical_consistency (
    sqlite3 *db,
    GError **error
)
{
    return validate_fts_canonical_consistency_impl (db, error, FALSE, TRUE);
}

#ifdef ATM_RETRIEVAL_VALIDATOR_TESTING
gboolean
atm_retrieval_index_validate_fts_canonical_consistency_for_test (
    sqlite3 *db,
    gboolean use_original_predicate,
    GError **error
)
{
    return validate_fts_canonical_consistency_impl (
        db, error, use_original_predicate, FALSE
    );
}
#endif
