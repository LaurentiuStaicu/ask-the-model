#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#include "retrieval_index_lifecycle.h"

#define COLD_SAMPLES 5
#define WARM_SAMPLES 15

typedef struct {
    const char *id;
    const char *root_env;
    const char *sha_env;
} RepositoryEnvironment;

static const RepositoryEnvironment repository_environment[] = {
    { "ewd", "ATM_BENCH_EWD_ROOT", "ATM_BENCH_EWD_SHA" },
    { "cbd", "ATM_BENCH_CBD_ROOT", "ATM_BENCH_CBD_SHA" },
    { "rmd", "ATM_BENCH_RMD_ROOT", "ATM_BENCH_RMD_SHA" }
};

static const char *
required_string(JsonObject *object, const char *member, GError **error)
{
    JsonNode *node = json_object_get_member(object, member);
    if (node == NULL || json_node_get_node_type(node) != JSON_NODE_VALUE ||
        json_node_get_value_type(node) != G_TYPE_STRING) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Missing string member '%s'.", member);
        return NULL;
    }
    return json_node_get_string(node);
}

static const RepositoryEnvironment *
find_environment(const char *repository_id)
{
    for (guint i = 0; i < G_N_ELEMENTS(repository_environment); i++) {
        if (g_strcmp0(repository_environment[i].id, repository_id) == 0)
            return &repository_environment[i];
    }
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

static gboolean
run_repository(JsonBuilder *builder, JsonObject *item, GError **error)
{
    const char *repository_id = required_string(item, "repository_id", error);
    const char *expected_version = required_string(item, "repository_version", error);
    const char *snapshot_sha = required_string(item, "snapshot_sha", error);
    if (repository_id == NULL || expected_version == NULL || snapshot_sha == NULL)
        return FALSE;

    const RepositoryEnvironment *environment = find_environment(repository_id);
    if (environment == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Unsupported repository '%s'.", repository_id);
        return FALSE;
    }

    const char *snapshot_root = g_getenv(environment->root_env);
    const char *actual_sha = g_getenv(environment->sha_env);
    if (snapshot_root == NULL || actual_sha == NULL ||
        g_strcmp0(actual_sha, snapshot_sha) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Missing or mismatched pinned snapshot for '%s'.", repository_id);
        return FALSE;
    }

    gint64 cold_us[COLD_SAMPLES] = { 0 };
    gint64 warm_us[WARM_SAMPLES] = { 0 };
    char *version = NULL;
    char *index_path = NULL;

    for (guint i = 0; i < COLD_SAMPLES; i++) {
        GError *local_error = NULL;
        char *cache_root = g_dir_make_tmp("atm-e1-cold-XXXXXX", &local_error);
        if (cache_root == NULL) {
            g_propagate_prefixed_error(error, local_error,
                                       "Cannot create cold cache root: ");
            return FALSE;
        }

        AtmRetrievalEnsureResult result;
        gint64 started = g_get_monotonic_time();
        gboolean ok = atm_retrieval_index_ensure_for_snapshot(
            cache_root, snapshot_root, repository_id, snapshot_sha,
            &index_path, &version, &result, &local_error);
        cold_us[i] = g_get_monotonic_time() - started;

        if (!ok || result != ATM_RETRIEVAL_ENSURE_REBUILT ||
            g_strcmp0(version, expected_version) != 0) {
            const char *reason = local_error != NULL ? local_error->message :
                (!ok ? "index ensure failed" :
                 (result != ATM_RETRIEVAL_ENSURE_REBUILT ?
                  "unexpected cache reuse" : "repository version mismatch"));
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Cold sample %u for %s did not rebuild the expected index: %s",
                        i, repository_id, reason);
            g_clear_error(&local_error);
            g_free(cache_root);
            g_free(index_path);
            g_free(version);
            return FALSE;
        }
        g_clear_error(&local_error);
        g_free(cache_root);
        g_clear_pointer(&index_path, g_free);
        g_clear_pointer(&version, g_free);
    }

    GError *local_error = NULL;
    char *warm_cache_root = g_dir_make_tmp("atm-e1-warm-XXXXXX", &local_error);
    if (warm_cache_root == NULL) {
        g_propagate_prefixed_error(error, local_error,
                                   "Cannot create warm cache root: ");
        return FALSE;
    }

    AtmRetrievalEnsureResult result;
    if (!atm_retrieval_index_ensure_for_snapshot(
            warm_cache_root, snapshot_root, repository_id, snapshot_sha,
            &index_path, &version, &result, error) ||
        result != ATM_RETRIEVAL_ENSURE_REBUILT ||
        g_strcmp0(version, expected_version) != 0) {
        if (error != NULL && *error == NULL)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Warm-cache priming for %s did not build the expected index.",
                        repository_id);
        g_free(warm_cache_root);
        g_free(index_path);
        g_free(version);
        return FALSE;
    }

    for (guint i = 0; i < WARM_SAMPLES; i++) {
        g_clear_pointer(&index_path, g_free);
        g_clear_pointer(&version, g_free);
        gint64 started = g_get_monotonic_time();
        gboolean ok = atm_retrieval_index_ensure_for_snapshot(
            warm_cache_root, snapshot_root, repository_id, snapshot_sha,
            &index_path, &version, &result, error);
        warm_us[i] = g_get_monotonic_time() - started;
        if (!ok || result != ATM_RETRIEVAL_ENSURE_REUSED ||
            g_strcmp0(version, expected_version) != 0) {
            if (error != NULL && *error == NULL)
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Warm sample %u for %s did not reuse the pinned index.",
                            i, repository_id);
            g_free(warm_cache_root);
            g_free(index_path);
            g_free(version);
            return FALSE;
        }
    }

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "repository_id");
    json_builder_add_string_value(builder, repository_id);
    json_builder_set_member_name(builder, "repository_version");
    json_builder_add_string_value(builder, version);
    json_builder_set_member_name(builder, "snapshot_sha");
    json_builder_add_string_value(builder, snapshot_sha);
    json_builder_set_member_name(builder, "cold_classification");
    json_builder_add_string_value(builder, "rebuilt");
    json_builder_set_member_name(builder, "warm_classification");
    json_builder_add_string_value(builder, "reused");
    add_samples(builder, "cold_ensure_us", cold_us, COLD_SAMPLES);
    add_samples(builder, "warm_ensure_us", warm_us, WARM_SAMPLES);
    json_builder_end_object(builder);

    g_print("E1 %s cold=%u rebuilt warm=%u reused sqlite=%s\n",
            repository_id, COLD_SAMPLES, WARM_SAMPLES, sqlite3_libversion());

    g_free(warm_cache_root);
    g_free(index_path);
    g_free(version);
    return TRUE;
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
    if (input_root == NULL ||
        json_node_get_node_type(input_root) != JSON_NODE_OBJECT) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Benchmark root must be an object.");
        goto out;
    }

    JsonObject *benchmark = json_node_get_object(input_root);
    JsonArray *corpus = json_object_get_array_member(benchmark, "corpus");
    if (corpus == NULL || json_array_get_length(corpus) == 0) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Benchmark corpus must be non-empty.");
        goto out;
    }

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "schema_version");
    json_builder_add_int_value(builder, 1);
    json_builder_set_member_name(builder, "benchmark_id");
    json_builder_add_string_value(builder, "atm-retrieval-lifecycle-e1");
    json_builder_set_member_name(builder, "run_id");
    json_builder_add_string_value(builder, argv[3]);
    json_builder_set_member_name(builder, "sqlite_version");
    json_builder_add_string_value(builder, sqlite3_libversion());
    json_builder_set_member_name(builder, "cold_samples_per_repository");
    json_builder_add_int_value(builder, COLD_SAMPLES);
    json_builder_set_member_name(builder, "warm_samples_per_repository");
    json_builder_add_int_value(builder, WARM_SAMPLES);
    json_builder_set_member_name(builder, "repositories");
    json_builder_begin_array(builder);

    for (guint i = 0; i < json_array_get_length(corpus); i++) {
        JsonNode *node = json_array_get_element(corpus, i);
        if (node == NULL || json_node_get_node_type(node) != JSON_NODE_OBJECT) {
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Corpus entry must be an object.");
            goto out;
        }
        if (!run_repository(builder, json_node_get_object(node), &error))
            goto out;
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);
    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    if (!json_generator_to_file(generator, argv[2], &error))
        goto out;

    g_print("PASS: E1 lifecycle measurements written to %s\n", argv[2]);
    exit_code = 0;

out:
    if (error != NULL)
        g_printerr("E1 benchmark failed: %s\n", error->message);
    g_clear_error(&error);
    g_clear_pointer(&root, json_node_free);
    g_object_unref(generator);
    g_object_unref(builder);
    g_object_unref(parser);
    return exit_code;
}
