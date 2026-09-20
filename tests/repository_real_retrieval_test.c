#include "retrieval_index_lifecycle.h"
#include "retrieval_router.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

typedef struct {
    const char *name;
    const char *repository_id;
    const char *root_env;
    const char *sha_env;
    char *index_path;
    char *version;
} RealRepository;

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

        while ((name = g_dir_read_name (directory)) != NULL) {
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

static RealRepository *
find_repository (
    RealRepository *repositories,
    gsize count,
    const char *repository_id
)
{
    for (gsize i = 0; i < count; i++) {
        if (g_strcmp0 (
                repositories[i].repository_id,
                repository_id
            ) == 0) {
            return &repositories[i];
        }
    }

    return NULL;
}

static AtmRepositoryEvidenceSet *
find_repository_set (
    AtmRetrievalResultSet *results,
    const char *repository_id
)
{
    for (guint i = 0; i < results->repositories->len; i++) {
        AtmRepositoryEvidenceSet *set = g_ptr_array_index (
            results->repositories,
            i
        );

        if (g_strcmp0 (
                set->repository_id,
                repository_id
            ) == 0) {
            return set;
        }
    }

    return NULL;
}

static AtmEvidenceRecord *
find_logical_id (
    AtmRepositoryEvidenceSet *set,
    const char *logical_source_id
)
{
    for (guint i = 0; i < set->evidence->len; i++) {
        AtmEvidenceRecord *record = g_ptr_array_index (
            set->evidence,
            i
        );

        if (g_strcmp0 (
                record->logical_source_id,
                logical_source_id
            ) == 0) {
            return record;
        }
    }

    return NULL;
}

static AtmEvidenceRecord *
find_source_path (
    AtmRepositoryEvidenceSet *set,
    const char *source_path
)
{
    for (guint i = 0; i < set->evidence->len; i++) {
        AtmEvidenceRecord *record = g_ptr_array_index (
            set->evidence,
            i
        );

        if (g_strcmp0 (
                record->source_path,
                source_path
            ) == 0) {
            return record;
        }
    }

    return NULL;
}

static AtmEvidenceRecord *
find_match_kind (
    AtmRepositoryEvidenceSet *set,
    AtmEvidenceMatch match_kind,
    const char *title
)
{
    for (guint i = 0; i < set->evidence->len; i++) {
        AtmEvidenceRecord *record = g_ptr_array_index (
            set->evidence,
            i
        );

        if (record->match_kind != match_kind) {
            continue;
        }

        if (title == NULL ||
            g_strcmp0 (record->title, title) == 0) {
            return record;
        }
    }

    return NULL;
}

static void
assert_set_provenance (
    AtmRepositoryEvidenceSet *set,
    const RealRepository *repository
)
{
    const char *expected_sha = g_getenv (
        repository->sha_env
    );

    g_assert_nonnull (set);
    g_assert_cmpstr (
        set->repository_id,
        ==,
        repository->repository_id
    );
    g_assert_cmpuint (set->evidence->len, >, 0);
    g_assert_nonnull (repository->version);
    g_assert_nonnull (expected_sha);

    for (guint i = 0; i < set->evidence->len; i++) {
        const AtmEvidenceRecord *record = g_ptr_array_index (
            set->evidence,
            i
        );

        g_assert_cmpstr (
            record->repository_id,
            ==,
            repository->repository_id
        );
        g_assert_cmpstr (
            record->repository_version,
            ==,
            repository->version
        );
        g_assert_cmpstr (
            record->snapshot_sha,
            ==,
            expected_sha
        );
        g_assert_nonnull (record->logical_source_id);
        g_assert_nonnull (record->source_path);
        g_assert_nonnull (record->locator);
    }
}

static AtmRetrievalResultSet *
run_query (
    const char *query,
    const GPtrArray *active,
    guint max_results
)
{
    AtmRetrievalResultSet *results = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_run (
            query,
            active,
            max_results,
            &results,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (results);
    return results;
}

static void
test_current_state_rmd (
    GPtrArray *active,
    RealRepository *repositories,
    gsize repository_count
)
{
    AtmRetrievalResultSet *results = run_query (
        "Care este starea curentă în RMD?",
        active,
        8
    );

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

    AtmRepositoryEvidenceSet *set = find_repository_set (
        results,
        "rmd"
    );
    RealRepository *repository = find_repository (
        repositories,
        repository_count,
        "rmd"
    );

    assert_set_provenance (set, repository);

    AtmEvidenceRecord *first = g_ptr_array_index (
        set->evidence,
        0
    );

    g_assert_cmpstr (
        first->source_path,
        ==,
        "STATUS.md"
    );
    g_assert_true (
        (first->source_roles &
         ATM_SOURCE_ROLE_CANONICAL) != 0
    );

    g_print (
        "PASS R3 current-state: RMD top=%s locator=%s\n",
        first->source_path,
        first->locator
    );

    atm_retrieval_result_set_free (results);
}

static void
test_exact_identities (
    GPtrArray *active,
    RealRepository *repositories,
    gsize repository_count
)
{
    const struct {
        const char *query;
        const char *repository_id;
        const char *logical_source_id;
    } cases[] = {
        {
            "food_per_capita EWD",
            "ewd",
            "ewd:entity:variable:food_per_capita"
        },
        {
            "VAR.BELIEF.CLAIM CBD",
            "cbd",
            "cbd:entity:variable:VAR.BELIEF.CLAIM"
        },
        {
            "government_refinancing_interest_loop RMD",
            "rmd",
            "rmd:entity:feedback_loop:"
            "government_refinancing_interest_loop"
        }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (cases); i++) {
        AtmRetrievalResultSet *results = run_query (
            cases[i].query,
            active,
            10
        );

        g_assert_true (results->explicit_scope);
        g_assert_false (results->requested_outside_scope);
        g_assert_cmpuint (
            results->repositories->len,
            ==,
            1
        );

        AtmRepositoryEvidenceSet *set =
            find_repository_set (
                results,
                cases[i].repository_id
            );
        RealRepository *repository = find_repository (
            repositories,
            repository_count,
            cases[i].repository_id
        );

        assert_set_provenance (set, repository);

        AtmEvidenceRecord *record = find_logical_id (
            set,
            cases[i].logical_source_id
        );

        g_assert_nonnull (record);
        g_assert_cmpint (
            record->match_kind,
            ==,
            ATM_EVIDENCE_MATCH_EXACT
        );

        g_print (
            "PASS R3 exact: %s -> %s (%s)\n",
            cases[i].query,
            record->logical_source_id,
            record->locator
        );

        atm_retrieval_result_set_free (results);
    }
}

static void
test_real_tabular_lookup (
    GPtrArray *active,
    RealRepository *repositories,
    gsize repository_count
)
{
    AtmRetrievalResultSet *results = run_query (
        "EWD food_per_capita 2025",
        active,
        20
    );

    AtmRepositoryEvidenceSet *set = find_repository_set (
        results,
        "ewd"
    );
    RealRepository *repository = find_repository (
        repositories,
        repository_count,
        "ewd"
    );

    assert_set_provenance (set, repository);

    AtmEvidenceRecord *exact = find_logical_id (
        set,
        "ewd:entity:variable:food_per_capita"
    );
    AtmEvidenceRecord *row = find_match_kind (
        set,
        ATM_EVIDENCE_MATCH_TABULAR,
        "2025"
    );

    g_assert_nonnull (exact);
    g_assert_cmpint (
        exact->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_EXACT
    );
    g_assert_nonnull (row);
    g_assert_true (
        (row->source_roles &
         ATM_SOURCE_ROLE_TABULAR) != 0
    );

    g_print (
        "PASS R3 tabular: %s %s\n",
        row->source_path,
        row->locator
    );

    atm_retrieval_result_set_free (results);
}

static void
test_cross_repository_current_state (
    GPtrArray *active,
    RealRepository *repositories,
    gsize repository_count
)
{
    AtmRetrievalResultSet *results = run_query (
        "Compară EWD, CBD și RMD: care este starea curentă?",
        active,
        8
    );

    g_assert_true (results->explicit_scope);
    g_assert_false (results->requested_outside_scope);
    g_assert_cmpuint (results->repositories->len, ==, 3);

    const char *ids[] = { "ewd", "cbd", "rmd" };

    for (gsize i = 0; i < G_N_ELEMENTS (ids); i++) {
        AtmRepositoryEvidenceSet *set =
            find_repository_set (
                results,
                ids[i]
            );
        RealRepository *repository = find_repository (
            repositories,
            repository_count,
            ids[i]
        );

        assert_set_provenance (set, repository);

        AtmEvidenceRecord *first = g_ptr_array_index (
            set->evidence,
            0
        );
        AtmEvidenceRecord *status = find_source_path (
            set,
            "STATUS.md"
        );

        g_assert_true (
            (first->source_roles &
             ATM_SOURCE_ROLE_CANONICAL) != 0
        );
        g_assert_nonnull (status);
        g_assert_true (
            (status->source_roles &
             ATM_SOURCE_ROLE_CANONICAL) != 0
        );

        g_print (
            "PASS R3 cross-repo: %s top=%s status=%s\n",
            ids[i],
            first->source_path,
            status->source_path
        );
    }

    atm_retrieval_result_set_free (results);
}

int
main (void)
{
    RealRepository repositories[] = {
        {
            "EWD",
            "ewd",
            "ATM_REAL_EWD_ROOT",
            "ATM_REAL_EWD_SHA",
            NULL,
            NULL
        },
        {
            "CBD",
            "cbd",
            "ATM_REAL_CBD_ROOT",
            "ATM_REAL_CBD_SHA",
            NULL,
            NULL
        },
        {
            "RMD",
            "rmd",
            "ATM_REAL_RMD_ROOT",
            "ATM_REAL_RMD_SHA",
            NULL,
            NULL
        }
    };
    GError *error = NULL;
    char *cache_root = g_dir_make_tmp (
        "atm-real-r3-cache-XXXXXX",
        &error
    );
    GPtrArray *active = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );

    g_assert_no_error (error);
    g_assert_nonnull (cache_root);

    for (gsize i = 0;
         i < G_N_ELEMENTS (repositories);
         i++) {
        const char *snapshot_root = g_getenv (
            repositories[i].root_env
        );
        const char *snapshot_sha = g_getenv (
            repositories[i].sha_env
        );
        AtmRetrievalEnsureResult ensure_result;

        g_assert_nonnull (snapshot_root);
        g_assert_nonnull (snapshot_sha);
        g_assert_cmpuint (strlen (snapshot_sha), ==, 40);

        g_assert_true (
            atm_retrieval_index_ensure_for_snapshot (
                cache_root,
                snapshot_root,
                repositories[i].repository_id,
                snapshot_sha,
                &repositories[i].index_path,
                &repositories[i].version,
                &ensure_result,
                &error
            )
        );
        g_assert_no_error (error);
        g_assert_cmpint (
            ensure_result,
            ==,
            ATM_RETRIEVAL_ENSURE_REBUILT
        );

        g_ptr_array_add (
            active,
            atm_retrieval_repository_scope_new (
                repositories[i].repository_id,
                repositories[i].index_path
            )
        );

        g_print (
            "R3 fixture: %s version=%s sha=%s\n",
            repositories[i].name,
            repositories[i].version,
            snapshot_sha
        );
    }

    test_current_state_rmd (
        active,
        repositories,
        G_N_ELEMENTS (repositories)
    );
    test_exact_identities (
        active,
        repositories,
        G_N_ELEMENTS (repositories)
    );
    test_real_tabular_lookup (
        active,
        repositories,
        G_N_ELEMENTS (repositories)
    );
    test_cross_repository_current_state (
        active,
        repositories,
        G_N_ELEMENTS (repositories)
    );

    g_ptr_array_unref (active);

    for (gsize i = 0;
         i < G_N_ELEMENTS (repositories);
         i++) {
        g_free (repositories[i].version);
        g_free (repositories[i].index_path);
    }

    remove_tree_best_effort (cache_root);
    g_free (cache_root);

    g_print (
        "PASS: real EWD/CBD/RMD snapshots satisfy deterministic R3 retrieval\n"
    );

    return 0;
}
