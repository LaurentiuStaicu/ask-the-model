#include <gio/gio.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>
#include <string.h>

#include "retrieval_index_lifecycle.h"
#include "retrieval_query.h"

#define QUERY_SAMPLES 15
#define SESSION_OPEN_SAMPLES 15
#define MAX_RESULTS 10

typedef struct {
    const char *id;
    const char *root_env;
    const char *sha_env;
    const char *exact_identifier;
    const char *fts_query;
    gboolean expect_tabular;
} RepositoryCase;

static const RepositoryCase repository_cases[] = {
    {
        "ewd", "ATM_BENCH_EWD_ROOT", "ATM_BENCH_EWD_SHA",
        "ewd:entity:variable:food_per_capita",
        "food_per_capita historical MAPE fit_diagnostics",
        TRUE
    },
    {
        "cbd", "ATM_BENCH_CBD_ROOT", "ATM_BENCH_CBD_SHA",
        "cbd:entity:variable:VAR.BELIEF.CLAIM",
        "EVSD medium n_target n_foil recovery probability",
        TRUE
    },
    {
        "rmd", "ATM_BENCH_RMD_ROOT", "ATM_BENCH_RMD_SHA",
        "rmd:entity:feedback_loop:government_refinancing_interest_loop",
        "government_refinancing_interest_loop",
        FALSE
    }
};

typedef gboolean (*QueryFunction)(
    AtmRetrievalQuerySession *, const char *, const char *, guint,
    GPtrArray **, GError **
);

typedef gboolean (*OneShotFunction)(
    const char *, const char *, const char *, guint, GPtrArray **, GError **
);

static const RepositoryCase *
find_repository_case(const char *id)
{
    for (guint i = 0; i < G_N_ELEMENTS(repository_cases); i++)
        if (g_strcmp0(repository_cases[i].id, id) == 0)
            return &repository_cases[i];
    return NULL;
}

static void
add_samples(JsonBuilder *builder, const char *name,
            const gint64 *samples, guint count)
{
    json_builder_set_member_name(builder, name);
    json_builder_begin_array(builder);
    for (guint i = 0; i < count; i++)
        json_builder_add_int_value(builder, samples[i]);
    json_builder_end_array(builder);
}

static void
checksum_field(GChecksum *checksum, const char *value)
{
    const char *text = value != NULL ? value : "<null>";
    g_checksum_update(checksum, (const guchar *) text, strlen(text));
    g_checksum_update(checksum, (const guchar *) "\n", 1);
}

static char *
records_digest(const GPtrArray *records)
{
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    for (guint i = 0; i < records->len; i++) {
        const AtmEvidenceRecord *record = g_ptr_array_index((GPtrArray *) records, i);
        char *evidence_id = g_strdup_printf("%" G_GINT64_FORMAT, record->evidence_id);
        checksum_field(checksum, record->evidence_kind);
        checksum_field(checksum, evidence_id);
        checksum_field(checksum, record->repository_id);
        checksum_field(checksum, record->repository_version);
        checksum_field(checksum, record->snapshot_sha);
        checksum_field(checksum, record->logical_source_id);
        checksum_field(checksum, record->dataset_logical_source_id);
        checksum_field(checksum, record->row_key);
        checksum_field(checksum, record->title);
        checksum_field(checksum, record->body);
        g_free(evidence_id);
    }
    char *digest = g_strdup(g_checksum_get_string(checksum));
    g_checksum_free(checksum);
    return digest;
}

static gboolean
find_tabular_key(const GPtrArray *records, char **out_dataset, char **out_row_key)
{
    for (guint i = 0; i < records->len; i++) {
        const AtmEvidenceRecord *record = g_ptr_array_index((GPtrArray *) records, i);
        if (g_strcmp0(record->evidence_kind, "dataset_row") == 0 &&
            record->dataset_logical_source_id != NULL &&
            record->row_key != NULL) {
            *out_dataset = g_strdup(record->dataset_logical_source_id);
            *out_row_key = g_strdup(record->row_key);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
run_session_query(AtmRetrievalQuerySession *session, const char *kind,
                  const char *query, const char *row_key,
                  GPtrArray **out, GError **error)
{
    if (g_strcmp0(kind, "exact") == 0)
        return atm_retrieval_query_session_lookup_exact(
            session, query, MAX_RESULTS, out, error);
    if (g_strcmp0(kind, "fts") == 0)
        return atm_retrieval_query_session_search_fts(
            session, query, MAX_RESULTS, out, error);
    return atm_retrieval_query_session_lookup_dataset_rows(
        session, query, row_key, MAX_RESULTS, out, error);
}

static gboolean
run_one_shot_query(const char *index_path, const char *kind,
                   const char *query, const char *row_key,
                   GPtrArray **out, GError **error)
{
    if (g_strcmp0(kind, "exact") == 0)
        return atm_retrieval_lookup_exact(index_path, query, MAX_RESULTS, out, error);
    if (g_strcmp0(kind, "fts") == 0)
        return atm_retrieval_search_fts(index_path, query, MAX_RESULTS, out, error);
    return atm_retrieval_lookup_dataset_rows(
        index_path, query, row_key, MAX_RESULTS, out, error);
}

static gboolean
measure_query_pair(JsonBuilder *builder, AtmRetrievalQuerySession *session,
                   const char *index_path, const char *kind,
                   const char *query, const char *row_key, GError **error)
{
    gint64 session_us[QUERY_SAMPLES] = { 0 };
    gint64 one_shot_us[QUERY_SAMPLES] = { 0 };
    char *expected_digest = NULL;

    for (guint i = 0; i < QUERY_SAMPLES; i++) {
        for (guint mode = 0; mode < 2; mode++) {
            gboolean use_session = ((i + mode) % 2) == 0;
            GPtrArray *records = NULL;
            gint64 started = g_get_monotonic_time();
            gboolean ok = use_session
                ? run_session_query(session, kind, query, row_key, &records, error)
                : run_one_shot_query(index_path, kind, query, row_key, &records, error);
            gint64 elapsed = g_get_monotonic_time() - started;

            if (use_session)
                session_us[i] = elapsed;
            else
                one_shot_us[i] = elapsed;

            if (!ok) {
                g_clear_pointer(&records, g_ptr_array_unref);
                g_free(expected_digest);
                return FALSE;
            }
            if (records == NULL || records->len == 0) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "%s query returned no evidence.", kind);
                g_clear_pointer(&records, g_ptr_array_unref);
                g_free(expected_digest);
                return FALSE;
            }

            char *digest = records_digest(records);
            if (expected_digest == NULL)
                expected_digest = g_strdup(digest);
            else if (g_strcmp0(expected_digest, digest) != 0) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "%s query results differ between persistent and one-shot sessions.",
                            kind);
                g_free(digest);
                g_ptr_array_unref(records);
                g_free(expected_digest);
                return FALSE;
            }
            g_free(digest);
            g_ptr_array_unref(records);
        }
    }

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "query_kind");
    json_builder_add_string_value(builder, kind);
    json_builder_set_member_name(builder, "semantic_sha256");
    json_builder_add_string_value(builder, expected_digest);
    json_builder_set_member_name(builder, "result_equivalence");
    json_builder_add_string_value(builder, "identical");
    add_samples(builder, "persistent_session_query_us", session_us, QUERY_SAMPLES);
    add_samples(builder, "one_shot_open_query_close_us", one_shot_us, QUERY_SAMPLES);
    json_builder_end_object(builder);

    g_free(expected_digest);
    return TRUE;
}

static gboolean
run_repository(JsonBuilder *builder, const char *repository_id,
               const char *repository_version, const char *snapshot_sha,
               GError **error)
{
    const RepositoryCase *test_case = find_repository_case(repository_id);
    if (test_case == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Unsupported repository '%s'.", repository_id);
        return FALSE;
    }
    const char *snapshot_root = g_getenv(test_case->root_env);
    const char *actual_sha = g_getenv(test_case->sha_env);
    if (snapshot_root == NULL || actual_sha == NULL ||
        g_strcmp0(actual_sha, snapshot_sha) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Missing or mismatched pinned snapshot for '%s'.", repository_id);
        return FALSE;
    }

    GError *local_error = NULL;
    char *cache_root = g_dir_make_tmp("atm-e2-index-XXXXXX", &local_error);
    if (cache_root == NULL) {
        g_propagate_prefixed_error(error, local_error, "Cannot create E2 cache: ");
        return FALSE;
    }

    char *index_path = NULL;
    char *actual_version = NULL;
    AtmRetrievalEnsureResult ensure_result;
    if (!atm_retrieval_index_ensure_for_snapshot(
            cache_root, snapshot_root, repository_id, snapshot_sha,
            &index_path, &actual_version, &ensure_result, error) ||
        ensure_result != ATM_RETRIEVAL_ENSURE_REBUILT ||
        g_strcmp0(actual_version, repository_version) != 0) {
        if (error != NULL && *error == NULL)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "E2 could not build pinned index for %s.", repository_id);
        g_free(cache_root);
        g_free(index_path);
        g_free(actual_version);
        return FALSE;
    }

    gint64 open_us[SESSION_OPEN_SAMPLES] = { 0 };
    for (guint i = 0; i < SESSION_OPEN_SAMPLES; i++) {
        gint64 started = g_get_monotonic_time();
        AtmRetrievalQuerySession *session =
            atm_retrieval_query_session_open(index_path, error);
        open_us[i] = g_get_monotonic_time() - started;
        if (session == NULL) {
            g_free(cache_root);
            g_free(index_path);
            g_free(actual_version);
            return FALSE;
        }
        atm_retrieval_query_session_close(session);
    }

    AtmRetrievalQuerySession *session =
        atm_retrieval_query_session_open(index_path, error);
    if (session == NULL) {
        g_free(cache_root);
        g_free(index_path);
        g_free(actual_version);
        return FALSE;
    }

    GPtrArray *fts_records = NULL;
    if (!atm_retrieval_query_session_search_fts(
            session, test_case->fts_query, MAX_RESULTS, &fts_records, error)) {
        atm_retrieval_query_session_close(session);
        g_free(cache_root);
        g_free(index_path);
        g_free(actual_version);
        return FALSE;
    }

    char *dataset_identifier = NULL;
    char *row_key = NULL;
    gboolean has_tabular = find_tabular_key(
        fts_records, &dataset_identifier, &row_key);
    g_ptr_array_unref(fts_records);
    if (test_case->expect_tabular && !has_tabular) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "FTS query for %s did not return a dataset row.", repository_id);
        atm_retrieval_query_session_close(session);
        g_free(cache_root);
        g_free(index_path);
        g_free(actual_version);
        return FALSE;
    }

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "repository_id");
    json_builder_add_string_value(builder, repository_id);
    json_builder_set_member_name(builder, "repository_version");
    json_builder_add_string_value(builder, actual_version);
    json_builder_set_member_name(builder, "snapshot_sha");
    json_builder_add_string_value(builder, snapshot_sha);
    json_builder_set_member_name(builder, "session_open_samples");
    json_builder_add_int_value(builder, SESSION_OPEN_SAMPLES);
    add_samples(builder, "session_open_and_validate_us", open_us, SESSION_OPEN_SAMPLES);
    json_builder_set_member_name(builder, "queries");
    json_builder_begin_array(builder);

    gboolean ok =
        measure_query_pair(builder, session, index_path, "exact",
                           test_case->exact_identifier, NULL, error) &&
        measure_query_pair(builder, session, index_path, "fts",
                           test_case->fts_query, NULL, error);
    if (ok && has_tabular)
        ok = measure_query_pair(builder, session, index_path, "dataset_rows",
                                dataset_identifier, row_key, error);
    json_builder_end_array(builder);
    json_builder_end_object(builder);

    atm_retrieval_query_session_close(session);
    g_free(dataset_identifier);
    g_free(row_key);
    g_free(cache_root);
    g_free(index_path);
    g_free(actual_version);

    if (ok)
        g_print("E2 %s: session-open=%u; exact+FTS%s semantic equivalence PASS\n",
                repository_id, SESSION_OPEN_SAMPLES,
                has_tabular ? "+dataset-row" : "");
    return ok;
}

int
main(int argc, char **argv)
{
    if (argc != 4) {
        g_printerr("Usage: %s BENCHMARK_JSON OUTPUT_JSON RUN_ID\n", argv[0]);
        return 2;
    }

    GError *error = NULL;
    JsonParser *parser = json_parser_new();
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = NULL;
    int exit_code = 1;

    if (!json_parser_load_from_file(parser, argv[1], &error))
        goto out;
    JsonNode *input_root = json_parser_get_root(parser);
    if (input_root == NULL || json_node_get_node_type(input_root) != JSON_NODE_OBJECT) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Benchmark root must be an object.");
        goto out;
    }
    JsonObject *benchmark = json_node_get_object(input_root);
    JsonArray *corpus = json_object_get_array_member(benchmark, "corpus");
    if (corpus == NULL || json_array_get_length(corpus) != 3) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "E2 requires exactly three pinned repositories.");
        goto out;
    }

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "schema_version");
    json_builder_add_int_value(builder, 1);
    json_builder_set_member_name(builder, "benchmark_id");
    json_builder_add_string_value(builder, "atm-retrieval-query-session-e2");
    json_builder_set_member_name(builder, "run_id");
    json_builder_add_string_value(builder, argv[3]);
    json_builder_set_member_name(builder, "sqlite_version");
    json_builder_add_string_value(builder, sqlite3_libversion());
    json_builder_set_member_name(builder, "session_open_samples_per_repository");
    json_builder_add_int_value(builder, SESSION_OPEN_SAMPLES);
    json_builder_set_member_name(builder, "query_samples_per_mode");
    json_builder_add_int_value(builder, QUERY_SAMPLES);
    json_builder_set_member_name(builder, "repositories");
    json_builder_begin_array(builder);

    for (guint i = 0; i < json_array_get_length(corpus); i++) {
        JsonNode *node = json_array_get_element(corpus, i);
        if (node == NULL || json_node_get_node_type(node) != JSON_NODE_OBJECT) {
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "E2 corpus entry must be an object.");
            goto out;
        }
        JsonObject *item = json_node_get_object(node);
        const char *repository_id = json_object_get_string_member(item, "repository_id");
        const char *repository_version = json_object_get_string_member(item, "repository_version");
        const char *snapshot_sha = json_object_get_string_member(item, "snapshot_sha");
        if (repository_id == NULL || repository_version == NULL || snapshot_sha == NULL ||
            !run_repository(builder, repository_id, repository_version, snapshot_sha, &error))
            goto out;
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);
    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    if (!json_generator_to_file(generator, argv[2], &error))
        goto out;

    g_print("PASS: E2 query-session measurements written to %s\n", argv[2]);
    exit_code = 0;

out:
    if (error != NULL)
        g_printerr("E2 benchmark failed: %s\n", error->message);
    g_clear_error(&error);
    g_clear_pointer(&root, json_node_free);
    g_object_unref(generator);
    g_object_unref(builder);
    g_object_unref(parser);
    return exit_code;
}
