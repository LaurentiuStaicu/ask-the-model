#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "retrieval_query.h"
#include "retrieval_index.h"
#include "repository_sources.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include <string.h>

#include <sys/stat.h>
#include <math.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) || S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static char *
new_temp_root (const char *prefix)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (prefix, &error);

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_text (
    const char *root,
    const char *relative,
    const char *contents
)
{
    GError *error = NULL;
    char *path = g_build_filename (root, relative, NULL);
    char *parent = g_path_get_dirname (path);

    g_assert_cmpint (
        g_mkdir_with_parents (parent, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (parent);
    g_free (path);
}

static char *
new_snapshot (void)
{
    char *root = new_temp_root (
        "atm-exact-retrieval-snapshot-XXXXXX"
    );

    write_text (
        root,
        ".atm/repository.json",
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"acronym\": \"EWD\",\n"
        "  \"display_name\": \"Empirical World3 Dynamics\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": [\"STATUS.md\", \"model/core.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [\"model\"],\n"
        "    \"evidence\": [\"data\"],\n"
        "    \"tabular\": [\"data\"],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n"
    );

    write_text (
        root,
        "STATUS.md",
        "# Scientific status\n"
        "Stare curentă verificată. Current baseline.\n"
        "## Current validation boundary\n"
        "Canonical validation boundary.\n"
    );

    write_text (
        root,
        "data/noise.csv",
        "id,text\n"
        "1,ce este pentru și în de care validation validation boundary\n"
        "2,ce este pentru și în de care validation boundary\n"
        "3,ce este pentru și în de care validation boundary\n"
    );

    write_text (
        root,
        "model/core.json",
        "{"
        "\"variables\":["
            "{\"id\":\"food_per_capita\","
             "\"label\":{\"en\":\"Food per capita\"}},"
            "{\"id\":\"population\","
             "\"label\":{\"en\":\"Population\"}}"
        "],"
        "\"links\":[{"
            "\"id\":\"LINK.FOOD.POP\","
            "\"source\":\"food_per_capita\","
            "\"target\":\"population\","
            "\"relation_type\":\"INFLUENCE\""
        "}]"
        "}\n"
    );

    write_text (
        root,
        "data/series.csv",
        "year,value\n2025,1\n"
    );

    return root;
}

static char *
build_index (
    const char *snapshot_root,
    const char *cache_root
)
{
    AtmSourceCatalog *catalog = NULL;
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    AtmRetrievalIndexMetadata metadata = {
        .repository_id = "ewd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .manifest_schema_version = 1,
        .manifest_sha256 = catalog->manifest_sha256,
        .created_at_utc = "2026-09-20T00:00:00Z"
    };

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);

    atm_source_catalog_free (catalog);
    return index_path;
}

static AtmEvidenceRecord *
result_at (GPtrArray *results, guint index)
{
    return g_ptr_array_index (results, index);
}

static void
assert_snapshot_provenance (const AtmEvidenceRecord *record)
{
    g_assert_cmpstr (
        record->repository_id,
        ==,
        "ewd"
    );
    g_assert_cmpstr (
        record->repository_version,
        ==,
        "0.1.0"
    );
    g_assert_cmpstr (
        record->snapshot_sha,
        ==,
        "0123456789abcdef0123456789abcdef01234567"
    );
}

static void
test_native_entity_lookup (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-exact-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_exact (
            index_path,
            "food_per_capita",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *record = result_at (results, 0);

    assert_snapshot_provenance (record);
    g_assert_cmpstr (record->evidence_kind, ==, "entity");
    g_assert_cmpint (
        record->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_EXACT
    );
    g_assert_cmpstr (
        record->logical_source_id,
        ==,
        "ewd:entity:variable:food_per_capita"
    );
    g_assert_cmpstr (
        record->source_path,
        ==,
        "model/core.json"
    );
    g_assert_cmpstr (
        record->locator,
        ==,
        "json:/variables/0"
    );
    g_assert_cmpstr (
        record->title,
        ==,
        "Food per capita"
    );
    g_assert_cmpstr (record->entity_type, ==, "variable");
    g_assert_cmpstr (record->native_id, ==, "food_per_capita");
    g_assert_null (record->relation_type);
    g_assert_null (record->from_logical_source_id);
    g_assert_null (record->to_logical_source_id);
    g_assert_null (record->dataset_logical_source_id);
    g_assert_null (record->row_key);
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_STRUCTURAL) != 0
    );
    g_assert_nonnull (
        strstr (record->body, "food_per_capita")
    );

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_native_relation_and_logical_id_lookup (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-exact-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_exact (
            index_path,
            "LINK.FOOD.POP",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *relation = result_at (results, 0);
    g_assert_cmpstr (
        relation->evidence_kind,
        ==,
        "relation"
    );
    g_assert_cmpstr (
        relation->logical_source_id,
        ==,
        "ewd:entity:relation:LINK.FOOD.POP"
    );
    g_assert_cmpstr (relation->relation_type, ==, "INFLUENCE");
    g_assert_cmpstr (relation->native_id, ==, "LINK.FOOD.POP");
    g_assert_cmpstr (
        relation->from_logical_source_id,
        ==,
        "ewd:entity:variable:food_per_capita"
    );
    g_assert_cmpstr (
        relation->to_logical_source_id,
        ==,
        "ewd:entity:variable:population"
    );
    g_assert_null (relation->entity_type);
    g_assert_null (relation->dataset_logical_source_id);
    g_assert_null (relation->row_key);
    g_assert_true (
        (relation->source_roles & ATM_SOURCE_ROLE_STRUCTURAL) != 0
    );

    g_ptr_array_unref (results);
    results = NULL;

    g_assert_true (
        atm_retrieval_lookup_exact (
            index_path,
            "ewd:entity:variable:population",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);
    g_assert_cmpstr (
        result_at (results, 0)->title,
        ==,
        "Population"
    );

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_dataset_logical_id_lookup (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-exact-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_exact (
            index_path,
            "ewd:dataset:data/series.csv",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *record = result_at (results, 0);

    g_assert_cmpstr (record->evidence_kind, ==, "dataset");
    g_assert_cmpstr (record->source_path, ==, "data/series.csv");
    g_assert_cmpstr (
        record->dataset_logical_source_id,
        ==,
        "ewd:dataset:data/series.csv"
    );
    g_assert_null (record->row_key);
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_EVIDENCE) != 0
    );
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_TABULAR) != 0
    );

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_exact_lookup_is_case_sensitive (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-exact-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_exact (
            index_path,
            "Food_Per_Capita",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 0);

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_fts_romanian_diacritic_search (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-fts-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "care este starea curenta a modelului",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *record = result_at (results, 0);

    assert_snapshot_provenance (record);
    g_assert_cmpstr (record->evidence_kind, ==, "section");
    g_assert_cmpstr (record->source_path, ==, "STATUS.md");
    g_assert_null (record->entity_type);
    g_assert_null (record->relation_type);
    g_assert_null (record->native_id);
    g_assert_null (record->from_logical_source_id);
    g_assert_null (record->to_logical_source_id);
    g_assert_null (record->dataset_logical_source_id);
    g_assert_null (record->row_key);
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_CANONICAL) != 0
    );
    g_assert_cmpint (
        record->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_LEXICAL
    );
    g_assert_true (record->has_lexical_score);
    g_assert_true (isfinite (record->lexical_score));
    g_assert_nonnull (
        strstr (record->body, "curentă")
    );

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_fts_common_function_words_do_not_swamp_technical_terms (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-fts-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "ce este pentru food_per_capita și în de care",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, >, 0);
    AtmEvidenceRecord *record = result_at (results, 0);
    g_assert_cmpstr (
        record->logical_source_id,
        ==,
        "ewd:entity:variable:food_per_capita"
    );
    g_assert_cmpstr (record->entity_type, ==, "variable");
    g_assert_cmpstr (record->native_id, ==, "food_per_capita");

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_fts_title_match_outweighs_body_only_noise (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-fts-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "current validation boundary",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, >, 0);
    g_assert_cmpstr (
        result_at (results, 0)->source_path,
        ==,
        "STATUS.md"
    );
    g_assert_cmpstr (
        result_at (results, 0)->title,
        ==,
        "Current validation boundary"
    );

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_fts_dataset_row_provenance (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-fts-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "2025 value",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *record = result_at (results, 0);

    assert_snapshot_provenance (record);
    g_assert_cmpstr (
        record->evidence_kind,
        ==,
        "dataset_row"
    );
    g_assert_cmpstr (
        record->source_path,
        ==,
        "data/series.csv"
    );
    g_assert_cmpstr (record->locator, ==, "lines:2-2");
    g_assert_cmpstr (
        record->dataset_logical_source_id,
        ==,
        "ewd:dataset:data/series.csv"
    );
    g_assert_cmpstr (record->row_key, ==, "2025");
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_EVIDENCE) != 0
    );
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_TABULAR) != 0
    );
    g_assert_true (record->has_lexical_score);

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_fts_syntax_is_not_passed_through (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-fts-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "current\" OR * baseline",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (results);

    g_ptr_array_unref (results);
    results = NULL;

    g_assert_false (
        atm_retrieval_search_fts (
            index_path,
            "\"\"***",
            10,
            &results,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_QUERY_ERROR,
        ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT
    );
    g_assert_null (results);

    g_clear_error (&error);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_tabular_row_key_lookup_global_and_scoped (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-tabular-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            NULL,
            "2025",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *record = result_at (results, 0);

    g_assert_cmpstr (
        record->evidence_kind,
        ==,
        "dataset_row"
    );
    g_assert_cmpstr (
        record->logical_source_id,
        ==,
        "ewd:dataset-row:data/series.csv:0"
    );
    g_assert_cmpstr (
        record->source_path,
        ==,
        "data/series.csv"
    );
    g_assert_cmpstr (record->locator, ==, "lines:2-2");
    g_assert_cmpstr (record->title, ==, "2025");
    g_assert_cmpstr (
        record->dataset_logical_source_id,
        ==,
        "ewd:dataset:data/series.csv"
    );
    g_assert_cmpstr (record->row_key, ==, "2025");
    g_assert_cmpint (
        record->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_TABULAR
    );
    g_assert_nonnull (strstr (record->body, "\"value\":\"1\""));
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_EVIDENCE) != 0
    );
    g_assert_true (
        (record->source_roles & ATM_SOURCE_ROLE_TABULAR) != 0
    );
    g_assert_false (record->has_lexical_score);

    g_ptr_array_unref (results);
    results = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            "ewd:dataset:data/series.csv",
            "2025",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    g_ptr_array_unref (results);
    results = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            "data/series.csv",
            "2025",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_tabular_duplicate_row_keys_are_preserved (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-tabular-retrieval-cache-XXXXXX"
    );

    write_text (
        snapshot_root,
        "data/series.csv",
        "year,value\n"
        "2025,1\n"
        "2025,2\n"
    );

    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            NULL,
            "2025",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 2);

    g_assert_cmpstr (
        result_at (results, 0)->logical_source_id,
        ==,
        "ewd:dataset-row:data/series.csv:0"
    );
    g_assert_cmpstr (
        result_at (results, 1)->logical_source_id,
        ==,
        "ewd:dataset-row:data/series.csv:1"
    );
    g_assert_cmpstr (
        result_at (results, 0)->locator,
        ==,
        "lines:2-2"
    );
    g_assert_cmpstr (
        result_at (results, 1)->locator,
        ==,
        "lines:3-3"
    );

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_tabular_scope_and_missing_key (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-tabular-retrieval-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            "ewd:dataset:missing.csv",
            "2025",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 0);
    g_ptr_array_unref (results);
    results = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            NULL,
            "1999",
            10,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 0);

    g_ptr_array_unref (results);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


static AtmEvidenceRecord *
find_by_logical_source_id (
    GPtrArray *results,
    const char *logical_source_id
)
{
    for (guint i = 0; i < results->len; i++) {
        AtmEvidenceRecord *record = result_at (results, i);

        if (g_strcmp0 (
                record->logical_source_id,
                logical_source_id
            ) == 0) {
            return record;
        }
    }

    return NULL;
}

static void
test_semantic_metadata_consistent_across_routes (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root (
        "atm-semantic-metadata-cache-XXXXXX"
    );
    char *index_path = build_index (
        snapshot_root,
        cache_root
    );
    GPtrArray *exact = NULL;
    GPtrArray *lexical = NULL;
    GPtrArray *tabular = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_lookup_exact (
            index_path,
            "food_per_capita",
            10,
            &exact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (exact->len, ==, 1);

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "food_per_capita",
            10,
            &lexical,
            &error
        )
    );
    g_assert_no_error (error);

    AtmEvidenceRecord *exact_entity = result_at (exact, 0);
    AtmEvidenceRecord *lexical_entity = find_by_logical_source_id (
        lexical,
        exact_entity->logical_source_id
    );

    g_assert_nonnull (lexical_entity);
    g_assert_cmpstr (
        lexical_entity->entity_type,
        ==,
        exact_entity->entity_type
    );
    g_assert_cmpstr (
        lexical_entity->native_id,
        ==,
        exact_entity->native_id
    );
    g_assert_cmpstr (
        lexical_entity->relation_type,
        ==,
        exact_entity->relation_type
    );

    g_ptr_array_unref (lexical);
    lexical = NULL;

    g_assert_true (
        atm_retrieval_lookup_dataset_rows (
            index_path,
            NULL,
            "2025",
            10,
            &tabular,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (tabular->len, ==, 1);

    g_assert_true (
        atm_retrieval_search_fts (
            index_path,
            "2025 value",
            10,
            &lexical,
            &error
        )
    );
    g_assert_no_error (error);

    AtmEvidenceRecord *tabular_row = result_at (tabular, 0);
    AtmEvidenceRecord *lexical_row = find_by_logical_source_id (
        lexical,
        tabular_row->logical_source_id
    );

    g_assert_nonnull (lexical_row);
    g_assert_cmpstr (
        lexical_row->dataset_logical_source_id,
        ==,
        tabular_row->dataset_logical_source_id
    );
    g_assert_cmpstr (
        lexical_row->row_key,
        ==,
        tabular_row->row_key
    );

    g_ptr_array_unref (lexical);
    g_ptr_array_unref (tabular);
    g_ptr_array_unref (exact);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


static void
test_query_session_reuses_validated_snapshot (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-query-session-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    AtmRetrievalQuerySession *session = NULL;
    GPtrArray *exact = NULL;
    GPtrArray *lexical = NULL;
    GPtrArray *tabular = NULL;
    GError *error = NULL;

    session = atm_retrieval_query_session_open (index_path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (session);

    g_assert_true (atm_retrieval_query_session_lookup_exact (
        session, "food_per_capita", 10, &exact, &error
    ));
    g_assert_no_error (error);
    g_assert_cmpuint (exact->len, ==, 1);

    g_assert_true (atm_retrieval_query_session_search_fts (
        session, "food_per_capita", 10, &lexical, &error
    ));
    g_assert_no_error (error);
    g_assert_cmpuint (lexical->len, >, 0);

    g_assert_true (atm_retrieval_query_session_lookup_dataset_rows (
        session, NULL, "2025", 10, &tabular, &error
    ));
    g_assert_no_error (error);
    g_assert_cmpuint (tabular->len, ==, 1);

    g_ptr_array_unref (tabular);
    g_ptr_array_unref (lexical);
    g_ptr_array_unref (exact);
    atm_retrieval_query_session_close (session);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_query_session_refresh_revalidates_external_in_place_mutation (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-query-session-refresh-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    AtmRetrievalQuerySession *session = NULL;
    sqlite3 *db = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    /* WAL permits the writer to commit while the session holds a read snapshot. */
    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    session = atm_retrieval_query_session_open (index_path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (session);

    g_assert_true (atm_retrieval_query_session_search_fts (
        session, "current validation boundary", 10, &results, &error
    ));
    g_assert_no_error (error);
    g_assert_nonnull (results);
    g_ptr_array_unref (results);
    results = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "UPDATE search_fts SET body=COALESCE(body,'') || ' tampered' "
            "WHERE rowid=(SELECT rowid FROM search_fts ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (db), ==, 1);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    /* The old transaction must be discarded; the fresh one must reject corruption. */
    g_assert_false (
        atm_retrieval_query_session_refresh (session, &error)
    );
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    atm_retrieval_query_session_close (session);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


static void
test_invalid_arguments_rejected (void)
{
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_retrieval_lookup_exact (
            "/does/not/matter.sqlite",
            "",
            10,
            &results,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_QUERY_ERROR,
        ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT
    );
    g_assert_null (results);

    g_clear_error (&error);

    g_assert_false (
        atm_retrieval_lookup_exact (
            "/does/not/matter.sqlite",
            "food_per_capita",
            ATM_RETRIEVAL_MAX_EXACT_RESULTS + 1,
            &results,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_QUERY_ERROR,
        ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT
    );
    g_assert_null (results);

    g_clear_error (&error);
}

static void
test_fts_canonical_consistency_precedes_all_routes (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-consistency-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "UPDATE search_fts SET evidence_id='-1' WHERE rowid=("
            "SELECT rowid FROM search_fts WHERE evidence_kind='section' "
            "ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (db), ==, 1);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE);
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_search_fts (
        index_path, "current validation boundary", 10, &results, &error
    ));
    g_assert_error (error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE);
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_lookup_dataset_rows (
        index_path, NULL, "2025", 10, &results, &error
    ));
    g_assert_error (error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE);
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

/*
 * Compare the production NOT EXISTS predicate with the diagnostic LEFT JOIN
 * alternative on both valid and deliberately corrupted FTS populations.
 * This qualifies only the FTS-to-canonical pass; duplicate detection and
 * reverse-coverage checks remain separate proof obligations.
 */
static gboolean
fts_to_canonical_query_reports_mismatch (sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2 (db, sql, -1, &statement, NULL);
    g_assert_cmpint (rc, ==, SQLITE_OK);

    rc = sqlite3_step (statement);
    gboolean mismatch = rc == SQLITE_ROW;
    g_assert_true (rc == SQLITE_ROW || rc == SQLITE_DONE);
    sqlite3_finalize (statement);
    return mismatch;
}

static void
test_fts_to_canonical_left_join_alternative_is_equivalent (void)
{
    static const char *production_sql =
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
    static const char *left_join_sql =
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
    static const struct {
        const char *name;
        const char *sql;
    } corruptions[] = {
        { "section logical source",
          "UPDATE search_fts SET logical_source_id='tampered' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "section title",
          "UPDATE search_fts SET title='tampered' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "section body",
          "UPDATE search_fts SET body='tampered' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "text evidence id",
          "UPDATE search_fts SET evidence_id='-1' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "blob evidence id",
          "UPDATE search_fts SET evidence_id=CAST(x'31' AS BLOB) WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "null evidence id",
          "UPDATE search_fts SET evidence_id=NULL WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "missing integer evidence id",
          "UPDATE search_fts SET evidence_id=9223372036854775807 WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "null evidence kind",
          "UPDATE search_fts SET evidence_kind=NULL WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "unknown evidence kind",
          "UPDATE search_fts SET evidence_kind='unknown' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "null logical source",
          "UPDATE search_fts SET logical_source_id=NULL WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='section' LIMIT 1);" },
        { "dataset row logical source",
          "UPDATE search_fts SET logical_source_id='tampered' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='dataset_row' LIMIT 1);" },
        { "dataset row title",
          "UPDATE search_fts SET title='tampered' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='dataset_row' LIMIT 1);" },
        { "dataset row body",
          "UPDATE search_fts SET body='tampered' WHERE rowid=("
          "SELECT rowid FROM search_fts WHERE evidence_kind='dataset_row' LIMIT 1);" }
    };

    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-left-join-equivalence-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_false (fts_to_canonical_query_reports_mismatch (db, production_sql));
    g_assert_false (fts_to_canonical_query_reports_mismatch (db, left_join_sql));
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);

    for (gsize i = 0; i < G_N_ELEMENTS (corruptions); i++) {
        snapshot_root = new_snapshot ();
        cache_root = new_temp_root ("atm-fts-left-join-case-XXXXXX");
        index_path = build_index (snapshot_root, cache_root);
        db = NULL;
        g_assert_cmpint (
            sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
            ==, SQLITE_OK
        );
        g_assert_cmpint (
            sqlite3_exec (db, corruptions[i].sql, NULL, NULL, NULL),
            ==, SQLITE_OK
        );
        g_assert_cmpint (sqlite3_changes (db), ==, 1);

        g_test_message ("Comparing validator predicates for %s", corruptions[i].name);
        g_assert_true (fts_to_canonical_query_reports_mismatch (db, production_sql));
        g_assert_true (fts_to_canonical_query_reports_mismatch (db, left_join_sql));

        g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
        g_free (index_path);
        remove_tree_best_effort (snapshot_root);
        g_free (snapshot_root);
        remove_tree_best_effort (cache_root);
        g_free (cache_root);
    }
}

static void
test_fts_canonical_metadata_mismatch_is_rejected_by_all_routes (void)
{
    static const struct {
        const char *field;
        const char *update_sql;
    } corruptions[] = {
        {
            "logical_source_id",
            "UPDATE search_fts SET logical_source_id='tampered-source' "
            "WHERE rowid=(SELECT rowid FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1);"
        },
        {
            "title",
            "UPDATE search_fts SET title='tampered-title' "
            "WHERE rowid=(SELECT rowid FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1);"
        },
        {
            "body",
            "UPDATE search_fts SET body='tampered-body' "
            "WHERE rowid=(SELECT rowid FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1);"
        }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (corruptions); i++) {
        char *snapshot_root = new_snapshot ();
        char *cache_root = new_temp_root ("atm-fts-metadata-cache-XXXXXX");
        char *index_path = build_index (snapshot_root, cache_root);
        sqlite3 *db = NULL;
        GPtrArray *results = NULL;
        GError *error = NULL;

        g_assert_cmpint (
            sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
            ==, SQLITE_OK
        );
        g_assert_cmpint (
            sqlite3_exec (db, corruptions[i].update_sql, NULL, NULL, NULL),
            ==, SQLITE_OK
        );
        g_assert_cmpint (sqlite3_changes (db), ==, 1);
        g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

        g_test_message (
            "Checking canonical %s mismatch across retrieval routes",
            corruptions[i].field
        );

        g_assert_false (atm_retrieval_lookup_exact (
            index_path, "food_per_capita", 10, &results, &error
        ));
        g_assert_error (
            error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
        );
        g_assert_null (results);
        g_clear_error (&error);

        g_assert_false (atm_retrieval_search_fts (
            index_path, "current validation boundary", 10, &results, &error
        ));
        g_assert_error (
            error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
        );
        g_assert_null (results);
        g_clear_error (&error);

        g_assert_false (atm_retrieval_lookup_dataset_rows (
            index_path, NULL, "2025", 10, &results, &error
        ));
        g_assert_error (
            error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
        );
        g_assert_null (results);
        g_clear_error (&error);

        g_free (index_path);
        remove_tree_best_effort (snapshot_root);
        g_free (snapshot_root);
        remove_tree_best_effort (cache_root);
        g_free (cache_root);
    }
}

static void
test_foreign_key_violation_is_rejected_by_all_routes (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-fk-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    /*
     * Simulate an index written by a connection that had FK enforcement
     * disabled. Re-enabling it later does not retroactively validate rows.
     */
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "PRAGMA foreign_keys=OFF;"
            "INSERT INTO dataset_rows "
            "(dataset_id,ordinal,row_key,locator,payload_json,search_text) "
            "VALUES (9223372036854770000,999999,'orphan','lines:0-0','{}','orphan');",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (db, "PRAGMA foreign_key_check;", -1, &statement, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    sqlite3_finalize (statement);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_search_fts (
        index_path, "current validation boundary", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_lookup_dataset_rows (
        index_path, NULL, "2025", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


static void
test_text_fts_evidence_id_is_rejected_by_all_routes (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-text-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "UPDATE search_fts SET evidence_id=CAST(evidence_id AS TEXT) "
            "WHERE rowid=(SELECT rowid FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (db), ==, 1);
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            "SELECT typeof(evidence_id) FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1;",
            -1, &statement, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    g_assert_cmpstr (
        (const char *) sqlite3_column_text (statement, 0), ==, "text"
    );
    sqlite3_finalize (statement);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_search_fts (
        index_path, "current validation boundary", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_lookup_dataset_rows (
        index_path, NULL, "2025", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


static void
test_blob_fts_evidence_id_is_rejected_by_all_routes (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-blob-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "UPDATE search_fts SET evidence_id=CAST(x'31' AS BLOB) "
            "WHERE rowid=(SELECT rowid FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (db), ==, 1);
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            "SELECT typeof(evidence_id) FROM search_fts "
            "WHERE evidence_kind='section' ORDER BY rowid LIMIT 1;",
            -1, &statement, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    g_assert_cmpstr (
        (const char *) sqlite3_column_text (statement, 0), ==, "blob"
    );
    sqlite3_finalize (statement);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    /*
     * The paired-cast-only identity predicate would accept BLOB x'31'
     * because CAST(BLOB AS TEXT) yields "1". The production predicate
     * also requires numeric identity and must reject this malformed value.
     */
    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_search_fts (
        index_path, "current validation boundary", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_false (atm_retrieval_lookup_dataset_rows (
        index_path, NULL, "2025", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_duplicate_fts_identity_is_rejected (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-duplicate-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "INSERT INTO search_fts(evidence_kind,evidence_id,logical_source_id,title,body) "
            "SELECT evidence_kind,evidence_id,logical_source_id,title,body "
            "FROM search_fts WHERE evidence_kind='section' LIMIT 1;",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_missing_canonical_fts_row_is_rejected (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-missing-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "DELETE FROM search_fts WHERE rowid=("
            "SELECT rowid FROM search_fts WHERE evidence_kind='section' "
            "ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (db), ==, 1);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE);
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_non_fts5_search_table_is_rejected (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-type-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *db = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (db, "DROP TABLE search_fts;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE);
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "CREATE TABLE search_fts("
            "evidence_kind TEXT,evidence_id TEXT,logical_source_id TEXT,"
            "title TEXT,body TEXT);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE);
    g_assert_null (results);
    g_clear_error (&error);

    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


typedef struct {
    guint64 device;
    guint64 inode;
    guint64 size;
    gint64 mtime_ns;
    gint64 ctime_ns;
} TestRetrievalFileIdentity;

static gint
test_sqlite_data_version (sqlite3 *db)
{
    sqlite3_stmt *statement = NULL;
    gint version = -1;

    g_assert_cmpint (
        sqlite3_prepare_v2 (db, "PRAGMA data_version;", -1, &statement, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    version = sqlite3_column_int (statement, 0);
    g_assert_cmpint (sqlite3_finalize (statement), ==, SQLITE_OK);
    return version;
}

static char *
test_first_fts_body (sqlite3 *db)
{
    sqlite3_stmt *statement = NULL;
    char *body = NULL;

    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db, "SELECT body FROM search_fts ORDER BY rowid LIMIT 1;",
            -1, &statement, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    body = g_strdup ((const char *) sqlite3_column_text (statement, 0));
    g_assert_cmpint (sqlite3_finalize (statement), ==, SQLITE_OK);
    return body;
}

/*
 * Characterize SQLite's connection-local data_version signal. The first read
 * pins a WAL snapshot; a writer then commits an in-place FTS mutation. The
 * existing transaction must continue to see its old snapshot, while a fresh
 * transaction must observe both the new contents and a changed data_version.
 */
static void
test_data_version_tracks_external_commit_across_transactions (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-data-version-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    sqlite3 *reader = NULL;
    sqlite3 *writer = NULL;
    char *old_body = NULL;
    char *pinned_body = NULL;
    char *fresh_body = NULL;
    gint version_before = -1;
    gint version_during_old_snapshot = -1;
    gint version_after_commit = -1;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &writer, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (writer, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (writer), ==, SQLITE_OK);
    writer = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &reader, SQLITE_OPEN_READONLY, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (reader, "PRAGMA query_only=ON; BEGIN;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );

    old_body = test_first_fts_body (reader);
    version_before = test_sqlite_data_version (reader);

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &writer, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            writer,
            "UPDATE search_fts SET body=COALESCE(body,'') || ' generation_probe' "
            "WHERE rowid=(SELECT rowid FROM search_fts ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (writer), ==, 1);
    g_assert_cmpint (sqlite3_close (writer), ==, SQLITE_OK);
    writer = NULL;

    version_during_old_snapshot = test_sqlite_data_version (reader);
    pinned_body = test_first_fts_body (reader);
    g_assert_cmpstr (pinned_body, ==, old_body);

    g_assert_cmpint (
        sqlite3_exec (reader, "COMMIT; BEGIN;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );
    fresh_body = test_first_fts_body (reader);
    version_after_commit = test_sqlite_data_version (reader);

    g_assert_cmpstr (fresh_body, !=, old_body);
    g_assert_nonnull (strstr (fresh_body, "generation_probe"));
    g_assert_cmpint (version_after_commit, !=, version_before);

    g_test_message (
        "data_version before=%d during_old_snapshot=%d after_fresh_snapshot=%d",
        version_before,
        version_during_old_snapshot,
        version_after_commit
    );

    g_free (old_body);
    g_free (pinned_body);
    g_free (fresh_body);
    sqlite3_close (reader);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}


static gboolean
test_retrieval_file_identity_read (
    const char *path,
    TestRetrievalFileIdentity *identity
)
{
    struct stat stat_buffer;

    if (path == NULL || identity == NULL ||
        g_stat (path, &stat_buffer) != 0 ||
        !S_ISREG (stat_buffer.st_mode)) {
        return FALSE;
    }

    identity->device = (guint64) stat_buffer.st_dev;
    identity->inode = (guint64) stat_buffer.st_ino;
    identity->size = (guint64) stat_buffer.st_size;
    identity->mtime_ns =
        ((gint64) stat_buffer.st_mtim.tv_sec * G_GINT64_CONSTANT (1000000000)) +
        stat_buffer.st_mtim.tv_nsec;
    identity->ctime_ns =
        ((gint64) stat_buffer.st_ctim.tv_sec * G_GINT64_CONSTANT (1000000000)) +
        stat_buffer.st_ctim.tv_nsec;
    return TRUE;
}

static gboolean
test_retrieval_file_identity_equal (
    const TestRetrievalFileIdentity *left,
    const TestRetrievalFileIdentity *right
)
{
    return left->device == right->device &&
        left->inode == right->inode &&
        left->size == right->size &&
        left->mtime_ns == right->mtime_ns &&
        left->ctime_ns == right->ctime_ns;
}

static gboolean
test_fts_contains_marker (sqlite3 *db, const char *marker)
{
    sqlite3_stmt *statement = NULL;
    gboolean found = FALSE;
    char *sql = sqlite3_mprintf (
        "SELECT 1 FROM search_fts WHERE body LIKE '%%%q%%' LIMIT 1;",
        marker
    );

    g_assert_nonnull (sql);
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &statement, NULL), ==, SQLITE_OK);
    g_free (sql);

    int rc = sqlite3_step (statement);
    g_assert_true (rc == SQLITE_ROW || rc == SQLITE_DONE);
    found = rc == SQLITE_ROW;
    g_assert_cmpint (sqlite3_finalize (statement), ==, SQLITE_OK);
    return found;
}

/*
 * An open SQLite connection stays attached to the old inode after atomic
 * replacement of its pathname. data_version on that connection cannot signal
 * the new path generation; path identity must be checked independently.
 */
static void
test_data_version_does_not_detect_atomic_path_replacement (void)
{
    const char *replacement_marker = "replacement_generation_marker";
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-path-generation-cache-XXXXXX");
    char *replacement_snapshot_root = new_snapshot ();
    char *replacement_cache_root =
        new_temp_root ("atm-path-generation-replacement-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    char *replacement_source = NULL;
    char *replacement_path = g_strdup_printf ("%s.replacement", index_path);
    char *replacement_status = g_strdup_printf (
        "# Replacement generation\n%s\n", replacement_marker
    );
    sqlite3 *reader = NULL;
    sqlite3 *fresh_reader = NULL;
    TestRetrievalFileIdentity original_identity = { 0 };
    TestRetrievalFileIdentity replacement_identity = { 0 };
    gint original_data_version = -1;
    gint after_replace_data_version = -1;
    GError *error = NULL;

    write_text (replacement_snapshot_root, "STATUS.md", replacement_status);
    replacement_source = build_index (
        replacement_snapshot_root, replacement_cache_root
    );

    g_assert_true (test_retrieval_file_identity_read (
        index_path, &original_identity
    ));
    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &reader, SQLITE_OPEN_READONLY, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (reader, "PRAGMA query_only=ON; BEGIN;", NULL, NULL, NULL),
        ==, SQLITE_OK
    );
    g_assert_false (test_fts_contains_marker (reader, replacement_marker));
    original_data_version = test_sqlite_data_version (reader);

    g_assert_cmpint (g_rename (replacement_source, index_path), ==, 0);
    g_assert_true (test_retrieval_file_identity_read (
        index_path, &replacement_identity
    ));
    g_assert_cmpuint (
        original_identity.inode, !=, replacement_identity.inode
    );

    after_replace_data_version = test_sqlite_data_version (reader);
    g_assert_cmpint (
        after_replace_data_version, ==, original_data_version
    );
    g_assert_false (test_fts_contains_marker (reader, replacement_marker));

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path, &fresh_reader, SQLITE_OPEN_READONLY, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_true (test_fts_contains_marker (
        fresh_reader, replacement_marker
    ));

    g_test_message (
        "atomic replacement preserved old connection data_version=%d; "
        "path inode changed from %" G_GUINT64_FORMAT " to %" G_GUINT64_FORMAT,
        original_data_version,
        original_identity.inode,
        replacement_identity.inode
    );

    sqlite3_close (fresh_reader);
    sqlite3_close (reader);
    g_free (replacement_status);
    g_free (replacement_path);
    g_free (replacement_source);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (replacement_snapshot_root);
    g_free (replacement_snapshot_root);
    remove_tree_best_effort (replacement_cache_root);
    g_free (replacement_cache_root);
}


static void
test_validation_file_identity_invalidates_on_mutation_and_replacement (void)
{
    char *snapshot_root = new_snapshot ();
    char *cache_root = new_temp_root ("atm-fts-identity-cache-XXXXXX");
    char *replacement_snapshot_root = new_snapshot ();
    char *replacement_cache_root =
        new_temp_root ("atm-fts-identity-replacement-cache-XXXXXX");
    char *index_path = build_index (snapshot_root, cache_root);
    char *replacement_source =
        build_index (replacement_snapshot_root, replacement_cache_root);
    char *replacement_path = g_strdup_printf ("%s.replacement", index_path);
    TestRetrievalFileIdentity original_identity = { 0 };
    TestRetrievalFileIdentity mutated_identity = { 0 };
    TestRetrievalFileIdentity replaced_identity = { 0 };
    sqlite3 *db = NULL;
    GPtrArray *results = NULL;
    GError *error = NULL;
    char *replacement_contents = NULL;
    gsize replacement_length = 0;

    g_assert_true (test_retrieval_file_identity_read (
        index_path, &original_identity
    ));

    g_assert_cmpint (
        sqlite3_open_v2 (index_path, &db, SQLITE_OPEN_READWRITE, NULL),
        ==, SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "UPDATE search_fts SET body=COALESCE(body,'') || ' tampered' "
            "WHERE rowid=(SELECT rowid FROM search_fts ORDER BY rowid LIMIT 1);",
            NULL, NULL, NULL
        ),
        ==, SQLITE_OK
    );
    g_assert_cmpint (sqlite3_changes (db), ==, 1);
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    g_assert_true (test_retrieval_file_identity_read (
        index_path, &mutated_identity
    ));
    g_assert_false (test_retrieval_file_identity_equal (
        &original_identity, &mutated_identity
    ));

    /* A fresh open must validate the modified database, not trust its old identity. */
    g_assert_false (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_error (
        error, ATM_RETRIEVAL_QUERY_ERROR, ATM_RETRIEVAL_QUERY_ERROR_SQLITE
    );
    g_assert_null (results);
    g_clear_error (&error);

    g_assert_true (g_file_get_contents (
        replacement_source,
        &replacement_contents,
        &replacement_length,
        &error
    ));
    g_assert_no_error (error);
    g_assert_true (g_file_set_contents (
        replacement_path,
        replacement_contents,
        (gssize) replacement_length,
        &error
    ));
    g_assert_no_error (error);
    g_clear_pointer (&replacement_contents, g_free);

    /* Replacing the path with another inode must invalidate the identity. */
    g_assert_cmpint (g_rename (replacement_path, index_path), ==, 0);
    g_assert_true (test_retrieval_file_identity_read (
        index_path, &replaced_identity
    ));
    g_assert_false (test_retrieval_file_identity_equal (
        &mutated_identity, &replaced_identity
    ));
    g_assert_cmpuint (
        replaced_identity.inode, !=, mutated_identity.inode
    );

    g_assert_true (atm_retrieval_lookup_exact (
        index_path, "food_per_capita", 10, &results, &error
    ));
    g_assert_no_error (error);
    g_assert_nonnull (results);
    g_ptr_array_unref (results);

    g_free (replacement_path);
    g_free (replacement_source);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (replacement_snapshot_root);
    g_free (replacement_snapshot_root);
    remove_tree_best_effort (replacement_cache_root);
    g_free (replacement_cache_root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-query/native-entity",
        test_native_entity_lookup
    );
    g_test_add_func (
        "/retrieval-query/native-relation-logical-id",
        test_native_relation_and_logical_id_lookup
    );
    g_test_add_func (
        "/retrieval-query/dataset-logical-id",
        test_dataset_logical_id_lookup
    );
    g_test_add_func (
        "/retrieval-query/case-sensitive",
        test_exact_lookup_is_case_sensitive
    );
    g_test_add_func (
        "/retrieval-query/fts-romanian-diacritics",
        test_fts_romanian_diacritic_search
    );
    g_test_add_func (
        "/retrieval-query/fts-stopwords",
        test_fts_common_function_words_do_not_swamp_technical_terms
    );
    g_test_add_func (
        "/retrieval-query/fts-title-weight",
        test_fts_title_match_outweighs_body_only_noise
    );
    g_test_add_func (
        "/retrieval-query/fts-dataset-row",
        test_fts_dataset_row_provenance
    );
    g_test_add_func (
        "/retrieval-query/fts-syntax-safety",
        test_fts_syntax_is_not_passed_through
    );
    g_test_add_func (
        "/retrieval-query/tabular-row-key",
        test_tabular_row_key_lookup_global_and_scoped
    );
    g_test_add_func (
        "/retrieval-query/tabular-duplicate-row-key",
        test_tabular_duplicate_row_keys_are_preserved
    );
    g_test_add_func (
        "/retrieval-query/tabular-scope-missing",
        test_tabular_scope_and_missing_key
    );
    g_test_add_func (
        "/retrieval-query/semantic-metadata-consistent",
        test_semantic_metadata_consistent_across_routes
    );
    g_test_add_func (
        "/retrieval-query/session-reuses-validated-snapshot",
        test_query_session_reuses_validated_snapshot
    );
    g_test_add_func (
        "/retrieval-query/fts-canonical-consistency-all-routes",
        test_fts_canonical_consistency_precedes_all_routes
    );
    g_test_add_func (
        "/retrieval-query/fts-to-canonical-left-join-equivalence",
        test_fts_to_canonical_left_join_alternative_is_equivalent
    );
    g_test_add_func (
        "/retrieval-query/fts-canonical-metadata-mismatch-all-routes",
        test_fts_canonical_metadata_mismatch_is_rejected_by_all_routes
    );
    g_test_add_func (
        "/retrieval-query/fts-foreign-key-violation-all-routes",
        test_foreign_key_violation_is_rejected_by_all_routes
    );
    g_test_add_func (
        "/retrieval-query/fts-text-evidence-id-all-routes",
        test_text_fts_evidence_id_is_rejected_by_all_routes
    );
    g_test_add_func (
        "/retrieval-query/fts-blob-evidence-id-all-routes",
        test_blob_fts_evidence_id_is_rejected_by_all_routes
    );
    g_test_add_func (
        "/retrieval-query/fts-duplicate-identity",
        test_duplicate_fts_identity_is_rejected
    );
    g_test_add_func (
        "/retrieval-query/fts-canonical-missing-row",
        test_missing_canonical_fts_row_is_rejected
    );
    g_test_add_func (
        "/retrieval-query/fts-non-fts5-table",
        test_non_fts5_search_table_is_rejected
    );
    g_test_add_func (
        "/retrieval-query/data-version-external-commit",
        test_data_version_tracks_external_commit_across_transactions
    );
    g_test_add_func (
        "/retrieval-query/data-version-atomic-replacement-boundary",
        test_data_version_does_not_detect_atomic_path_replacement
    );
    g_test_add_func (
        "/retrieval-query/validation-file-identity-invalidation",
        test_validation_file_identity_invalidates_on_mutation_and_replacement
    );
    g_test_add_func (
        "/retrieval-query/session-refresh-revalidates-mutation",
        test_query_session_refresh_revalidates_external_in_place_mutation
    );
    g_test_add_func (
        "/retrieval-query/invalid-arguments",
        test_invalid_arguments_rejected
    );

    return g_test_run ();
}
