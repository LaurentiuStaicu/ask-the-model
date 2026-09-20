#include "retrieval_query.h"
#include "retrieval_index.h"
#include "repository_sources.h"

#include <glib.h>
#include <glib/gstdio.h>

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
    );

    write_text (
        root,
        "data/noise.csv",
        "id,text\n"
        "1,ce este pentru și în de care\n"
        "2,ce este pentru și în de care\n"
        "3,ce este pentru și în de care\n"
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
    g_assert_cmpstr (
        result_at (results, 0)->logical_source_id,
        ==,
        "ewd:entity:variable:food_per_capita"
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
        "/retrieval-query/invalid-arguments",
        test_invalid_arguments_rejected
    );

    return g_test_run ();
}
