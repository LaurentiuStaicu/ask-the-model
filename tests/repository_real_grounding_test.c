#include "citation_labels.h"
#include "grounding_context.h"
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

static AtmGroundingContext *
build_context (
    const AtmRetrievalResultSet *results,
    guint max_sources,
    gsize max_bytes
)
{
    AtmGroundingContext *context = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_grounding_context_build (
            results,
            max_sources,
            max_bytes,
            &context,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (context);
    g_assert_cmpuint (context->evidence_bytes, <=, max_bytes);
    return context;
}

static AtmCitationResolution *
resolve_answer (
    const char *answer,
    const AtmGroundingContext *context
)
{
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_citation_resolve_labels (
            answer,
            context,
            &resolution,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (resolution);
    return resolution;
}

static void
test_rmd_turn_traceability (
    const GPtrArray *active,
    RealRepository *repositories,
    gsize repository_count
)
{
    AtmRetrievalResultSet *results = run_query (
        "Care este starea curentă în RMD?",
        active,
        8
    );
    AtmGroundingContext *context = build_context (
        results,
        4,
        12288
    );

    g_assert_cmpuint (context->sources->len, >, 0);

    AtmGroundingSource *source = g_ptr_array_index (
        context->sources,
        0
    );
    RealRepository *rmd = find_repository (
        repositories,
        repository_count,
        "rmd"
    );

    g_assert_nonnull (rmd);
    g_assert_cmpstr (source->label, ==, "[S1]");
    g_assert_cmpstr (source->repository_id, ==, "rmd");
    g_assert_cmpstr (source->source_path, ==, "STATUS.md");
    g_assert_cmpstr (
        source->repository_version,
        ==,
        rmd->version
    );
    g_assert_cmpstr (
        source->snapshot_sha,
        ==,
        g_getenv (rmd->sha_env)
    );
    g_assert_true (
        (source->source_roles &
         ATM_SOURCE_ROLE_CANONICAL) != 0
    );
    g_assert_nonnull (
        strstr (
            context->evidence_text,
            "BEGIN_UNTRUSTED_REPOSITORY_DATA"
        )
    );

    AtmCitationResolution *resolution = resolve_answer (
        "Starea curentă este descrisă de sursa canonică [S1]. "
        "O etichetă inventată nu trebuie rezolvată [S99].",
        context
    );

    g_assert_cmpuint (resolution->citations->len, ==, 1);
    g_assert_cmpuint (resolution->unknown_labels->len, ==, 1);

    AtmCitationReference *citation = g_ptr_array_index (
        resolution->citations,
        0
    );

    g_assert_cmpstr (citation->label, ==, "[S1]");
    g_assert_cmpstr (citation->repository_id, ==, "rmd");
    g_assert_cmpstr (
        citation->snapshot_sha,
        ==,
        g_getenv (rmd->sha_env)
    );
    g_assert_cmpstr (
        citation->logical_source_id,
        ==,
        source->logical_source_id
    );
    g_assert_cmpstr (
        citation->source_path,
        ==,
        source->source_path
    );
    g_assert_cmpstr (
        citation->locator,
        ==,
        source->locator
    );
    g_assert_nonnull (citation->excerpt);

    char *saved_sha = g_strdup (citation->snapshot_sha);
    char *saved_locator = g_strdup (citation->locator);

    atm_grounding_context_free (context);
    atm_retrieval_result_set_free (results);

    g_assert_cmpstr (citation->snapshot_sha, ==, saved_sha);
    g_assert_cmpstr (citation->locator, ==, saved_locator);

    g_print (
        "PASS R4 traceability: %s %s %s %s\n",
        citation->repository_id,
        citation->snapshot_sha,
        citation->source_path,
        citation->locator
    );

    g_free (saved_locator);
    g_free (saved_sha);
    atm_citation_resolution_free (resolution);
}

static void
test_cross_repository_citation_balance (
    const GPtrArray *active,
    RealRepository *repositories,
    gsize repository_count
)
{
    AtmRetrievalResultSet *results = run_query (
        "Compară EWD, CBD și RMD: care este starea curentă?",
        active,
        8
    );
    AtmGroundingContext *context = build_context (
        results,
        6,
        24576
    );

    g_assert_cmpuint (context->sources->len, >=, 3);

    const char *ids[] = { "ewd", "cbd", "rmd" };

    for (guint i = 0; i < 3; i++) {
        AtmGroundingSource *source = g_ptr_array_index (
            context->sources,
            i
        );
        char *expected_label = g_strdup_printf (
            "[S%u]",
            i + 1
        );
        RealRepository *repository = find_repository (
            repositories,
            repository_count,
            ids[i]
        );

        g_assert_nonnull (repository);
        g_assert_cmpstr (
            source->label,
            ==,
            expected_label
        );
        g_assert_cmpstr (
            source->repository_id,
            ==,
            ids[i]
        );
        g_assert_cmpstr (
            source->snapshot_sha,
            ==,
            g_getenv (repository->sha_env)
        );
        g_assert_true (
            (source->source_roles &
             ATM_SOURCE_ROLE_CANONICAL) != 0
        );

        g_free (expected_label);
    }

    AtmCitationResolution *resolution = resolve_answer (
        "EWD [S1], CBD [S2] și RMD [S3] rămân surse distincte.",
        context
    );

    g_assert_cmpuint (resolution->citations->len, ==, 3);
    g_assert_cmpuint (resolution->unknown_labels->len, ==, 0);

    for (guint i = 0; i < 3; i++) {
        AtmCitationReference *citation = g_ptr_array_index (
            resolution->citations,
            i
        );
        RealRepository *repository = find_repository (
            repositories,
            repository_count,
            ids[i]
        );

        g_assert_cmpstr (
            citation->repository_id,
            ==,
            ids[i]
        );
        g_assert_cmpstr (
            citation->snapshot_sha,
            ==,
            g_getenv (repository->sha_env)
        );
        g_assert_nonnull (citation->logical_source_id);
        g_assert_nonnull (citation->source_path);
        g_assert_nonnull (citation->locator);
        g_assert_nonnull (citation->excerpt);

        g_print (
            "PASS R4 cross-repo citation: %s %s %s\n",
            citation->label,
            citation->repository_id,
            citation->snapshot_sha
        );
    }

    atm_citation_resolution_free (resolution);
    atm_grounding_context_free (context);
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
        "atm-real-r4-cache-XXXXXX",
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

        g_ptr_array_add (
            active,
            atm_retrieval_repository_scope_new (
                repositories[i].repository_id,
                repositories[i].index_path
            )
        );

        g_print (
            "R4 fixture: %s version=%s sha=%s ensure=%d\n",
            repositories[i].name,
            repositories[i].version,
            snapshot_sha,
            ensure_result
        );
    }

    test_rmd_turn_traceability (
        active,
        repositories,
        G_N_ELEMENTS (repositories)
    );
    test_cross_repository_citation_balance (
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
        "PASS: real EWD/CBD/RMD snapshots satisfy R4 grounding traceability\n"
    );

    return 0;
}
