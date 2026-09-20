#include "retrieval_router.h"

#include "retrieval_index.h"
#include "repository_sources.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (
                    directory
                )) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );
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
    char *root = g_dir_make_tmp (
        prefix,
        &error
    );

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
    char *path = g_build_filename (
        root,
        relative,
        NULL
    );
    char *parent = g_path_get_dirname (path);

    g_assert_cmpint (
        g_mkdir_with_parents (
            parent,
            0700
        ),
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
new_ewd_snapshot (void)
{
    char *root = new_temp_root (
        "atm-router-ewd-XXXXXX"
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
        "  \"required_paths\": ["
        "\"STATUS.md\", \"model/core.json\"],\n"
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
        "EWD current status and system boundary.\n"
    );

    write_text (
        root,
        "model/core.json",
        "{"
        "\"variables\":[{"
            "\"id\":\"food_per_capita\","
            "\"label\":{\"en\":\"Food per capita\"}"
        "}]"
        "}\n"
    );

    write_text (
        root,
        "data/series.csv",
        "year,value\n"
        "2025,1\n"
    );

    return root;
}

static char *
new_rmd_snapshot (void)
{
    char *root = new_temp_root (
        "atm-router-rmd-XXXXXX"
    );

    write_text (
        root,
        ".atm/repository.json",
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"rmd\",\n"
        "  \"acronym\": \"RMD\",\n"
        "  \"display_name\": \"Romanian Monetary Dynamics\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": ["
        "\"STATUS.md\", "
        "\"model/dynamics/feedback_registry.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [\"model/dynamics\"],\n"
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
        "RMD current status and accounting boundary.\n"
    );

    write_text (
        root,
        "model/dynamics/feedback_registry.json",
        "{"
        "\"loops\":[{"
            "\"id\":\"government_refinancing_interest_loop\","
            "\"label\":{"
                "\"en\":\"Government refinancing interest loop\""
            "}"
        "}]"
        "}\n"
    );

    write_text (
        root,
        "data/series.csv",
        "period,value\n"
        "2024,2\n"
    );

    return root;
}

static char *
build_index (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha
)
{
    AtmSourceCatalog *catalog = NULL;
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            repository_id,
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    AtmRetrievalIndexMetadata metadata = {
        .repository_id = repository_id,
        .repository_version = "0.1.0",
        .snapshot_sha = snapshot_sha,
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

typedef struct {
    char *cache_root;
    char *ewd_root;
    char *rmd_root;
    char *ewd_index;
    char *rmd_index;
    GPtrArray *active;
} RouterFixture;

static RouterFixture *
router_fixture_new (void)
{
    RouterFixture *fixture = g_new0 (
        RouterFixture,
        1
    );

    fixture->cache_root = new_temp_root (
        "atm-router-cache-XXXXXX"
    );
    fixture->ewd_root = new_ewd_snapshot ();
    fixture->rmd_root = new_rmd_snapshot ();
    fixture->ewd_index = build_index (
        fixture->cache_root,
        fixture->ewd_root,
        "ewd",
        "1111111111111111111111111111111111111111"
    );
    fixture->rmd_index = build_index (
        fixture->cache_root,
        fixture->rmd_root,
        "rmd",
        "2222222222222222222222222222222222222222"
    );

    fixture->active = g_ptr_array_new_with_free_func (
        (GDestroyNotify)
            atm_retrieval_repository_scope_free
    );
    g_ptr_array_add (
        fixture->active,
        atm_retrieval_repository_scope_new (
            "ewd",
            fixture->ewd_index
        )
    );
    g_ptr_array_add (
        fixture->active,
        atm_retrieval_repository_scope_new (
            "rmd",
            fixture->rmd_index
        )
    );

    return fixture;
}

static void
router_fixture_free (RouterFixture *fixture)
{
    if (fixture == NULL) {
        return;
    }

    g_clear_pointer (&fixture->active, g_ptr_array_unref);
    g_free (fixture->rmd_index);
    g_free (fixture->ewd_index);

    remove_tree_best_effort (fixture->rmd_root);
    remove_tree_best_effort (fixture->ewd_root);
    remove_tree_best_effort (fixture->cache_root);

    g_free (fixture->rmd_root);
    g_free (fixture->ewd_root);
    g_free (fixture->cache_root);
    g_free (fixture);
}

static AtmRepositoryEvidenceSet *
repository_set_at (
    AtmRetrievalResultSet *results,
    guint index
)
{
    return g_ptr_array_index (
        results->repositories,
        index
    );
}

static AtmEvidenceRecord *
evidence_at (
    AtmRepositoryEvidenceSet *set,
    guint index
)
{
    return g_ptr_array_index (
        set->evidence,
        index
    );
}

static void
test_romanian_explicit_rmd_routes_to_canonical_status (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_run (
            "Care este starea curentă în RMD?",
            fixture->active,
            5,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (results->explicit_scope);
    g_assert_false (results->requested_outside_scope);
    g_assert_true (
        (results->intents &
         ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0
    );
    g_assert_nonnull (
        strstr (
            results->expanded_query,
            "current"
        )
    );
    g_assert_cmpuint (results->repositories->len, ==, 1);

    AtmRepositoryEvidenceSet *set = repository_set_at (
        results,
        0
    );

    g_assert_cmpstr (set->repository_id, ==, "rmd");
    g_assert_cmpuint (set->evidence->len, >, 0);
    g_assert_cmpstr (
        evidence_at (set, 0)->source_path,
        ==,
        "STATUS.md"
    );
    g_assert_cmpstr (
        evidence_at (set, 0)->repository_id,
        ==,
        "rmd"
    );
    g_assert_cmpstr (
        evidence_at (set, 0)->repository_version,
        ==,
        "0.1.0"
    );
    g_assert_cmpstr (
        evidence_at (set, 0)->snapshot_sha,
        ==,
        "2222222222222222222222222222222222222222"
    );
    g_assert_true (
        (evidence_at (set, 0)->source_roles &
         ATM_SOURCE_ROLE_CANONICAL) != 0
    );

    atm_retrieval_result_set_free (results);
    router_fixture_free (fixture);
}

static void
test_exact_entity_precedes_lexical_and_year_row (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_run (
            "food_per_capita 2025",
            fixture->active,
            8,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (results->explicit_scope);
    g_assert_cmpuint (results->repositories->len, ==, 2);

    AtmRepositoryEvidenceSet *ewd = repository_set_at (
        results,
        0
    );

    g_assert_cmpstr (ewd->repository_id, ==, "ewd");
    g_assert_cmpuint (ewd->evidence->len, >=, 2);

    AtmEvidenceRecord *first = evidence_at (ewd, 0);

    g_assert_cmpint (
        first->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_EXACT
    );
    g_assert_cmpstr (
        first->logical_source_id,
        ==,
        "ewd:entity:variable:food_per_capita"
    );

    gboolean found_year_row = FALSE;

    for (guint i = 0; i < ewd->evidence->len; i++) {
        AtmEvidenceRecord *record = evidence_at (ewd, i);

        if (record->match_kind == ATM_EVIDENCE_MATCH_TABULAR &&
            g_strcmp0 (record->title, "2025") == 0) {
            found_year_row = TRUE;
            break;
        }
    }

    g_assert_true (found_year_row);

    atm_retrieval_result_set_free (results);
    router_fixture_free (fixture);
}

static void
test_explicit_cross_repository_query_keeps_sets_separate (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_run (
            "Compară EWD și RMD current status",
            fixture->active,
            5,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (results->explicit_scope);
    g_assert_cmpuint (results->repositories->len, ==, 2);

    AtmRepositoryEvidenceSet *ewd = repository_set_at (
        results,
        0
    );
    AtmRepositoryEvidenceSet *rmd = repository_set_at (
        results,
        1
    );

    g_assert_cmpstr (ewd->repository_id, ==, "ewd");
    g_assert_cmpstr (rmd->repository_id, ==, "rmd");
    g_assert_cmpuint (ewd->evidence->len, >, 0);
    g_assert_cmpuint (rmd->evidence->len, >, 0);
    g_assert_cmpstr (
        evidence_at (ewd, 0)->source_path,
        ==,
        "STATUS.md"
    );
    g_assert_cmpstr (
        evidence_at (rmd, 0)->source_path,
        ==,
        "STATUS.md"
    );

    atm_retrieval_result_set_free (results);
    router_fixture_free (fixture);
}

static void
test_rmd_feedback_loop_exact_lookup (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_run (
            "government_refinancing_interest_loop RMD",
            fixture->active,
            5,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->repositories->len, ==, 1);

    AtmRepositoryEvidenceSet *rmd = repository_set_at (
        results,
        0
    );

    g_assert_cmpuint (rmd->evidence->len, >, 0);
    g_assert_cmpint (
        evidence_at (rmd, 0)->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_EXACT
    );
    g_assert_cmpstr (
        evidence_at (rmd, 0)->logical_source_id,
        ==,
        "rmd:entity:feedback_loop:"
        "government_refinancing_interest_loop"
    );

    atm_retrieval_result_set_free (results);
    router_fixture_free (fixture);
}

static void
test_mixed_outside_scope_fails_closed_without_partial_evidence (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_run (
            "Compară EWD și CBD current status",
            fixture->active,
            5,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (results->explicit_scope);
    g_assert_true (results->requested_outside_scope);
    g_assert_cmpuint (results->repositories->len, ==, 0);

    atm_retrieval_result_set_free (results);
    router_fixture_free (fixture);
}

static void
test_scope_repository_must_match_index_provenance (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_ptr_array_set_size (fixture->active, 0);
    g_ptr_array_add (
        fixture->active,
        atm_retrieval_repository_scope_new (
            "ewd",
            fixture->rmd_index
        )
    );

    g_assert_false (
        atm_retrieval_run (
            "current status",
            fixture->active,
            5,
            &results,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_ROUTER_ERROR,
        ATM_RETRIEVAL_ROUTER_ERROR_PROVENANCE
    );
    g_assert_null (results);

    g_clear_error (&error);
    router_fixture_free (fixture);
}

static void
test_repository_outside_active_scope_is_not_queried (void)
{
    RouterFixture *fixture = router_fixture_new ();
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_ptr_array_remove_index (fixture->active, 1);

    g_assert_true (
        atm_retrieval_run (
            "Care este starea RMD?",
            fixture->active,
            5,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (results->explicit_scope);
    g_assert_true (results->requested_outside_scope);
    g_assert_cmpuint (results->repositories->len, ==, 0);

    atm_retrieval_result_set_free (results);
    router_fixture_free (fixture);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-router/romanian-rmd-current",
        test_romanian_explicit_rmd_routes_to_canonical_status
    );
    g_test_add_func (
        "/retrieval-router/exact-plus-tabular",
        test_exact_entity_precedes_lexical_and_year_row
    );
    g_test_add_func (
        "/retrieval-router/cross-repository",
        test_explicit_cross_repository_query_keeps_sets_separate
    );
    g_test_add_func (
        "/retrieval-router/rmd-feedback-loop",
        test_rmd_feedback_loop_exact_lookup
    );
    g_test_add_func (
        "/retrieval-router/mixed-outside-fail-closed",
        test_mixed_outside_scope_fails_closed_without_partial_evidence
    );
    g_test_add_func (
        "/retrieval-router/provenance-scope-match",
        test_scope_repository_must_match_index_provenance
    );
    g_test_add_func (
        "/retrieval-router/outside-scope",
        test_repository_outside_active_scope_is_not_queried
    );

    return g_test_run ();
}
