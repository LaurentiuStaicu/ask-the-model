#include "retrieval_index_lifecycle.h"
#include "retrieval_index.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

typedef struct {
    const char *name;
    const char *repository_id;
    const char *root_env;
    const char *sha_env;
} RealRepository;

static int
query_count (sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;
    int value;

    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_ROW
    );

    value = sqlite3_column_int (statement, 0);
    sqlite3_finalize (statement);
    return value;
}

static void
exercise_repository (const RealRepository *repository)
{
    const char *snapshot_root = g_getenv (repository->root_env);
    const char *snapshot_sha = g_getenv (repository->sha_env);
    GError *error = NULL;
    char *cache_root = g_dir_make_tmp (
        "atm-real-repository-index-XXXXXX",
        &error
    );
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult ensure_result;
    sqlite3 *db = NULL;

    g_assert_no_error (error);
    g_assert_nonnull (cache_root);
    g_assert_nonnull (snapshot_root);
    g_assert_nonnull (snapshot_sha);
    g_assert_cmpuint (strlen (snapshot_sha), ==, 40);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            repository->repository_id,
            snapshot_sha,
            &index_path,
            &version,
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
    g_assert_nonnull (index_path);
    g_assert_nonnull (version);

    g_assert_true (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            repository->repository_id,
            snapshot_sha,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    int source_files = query_count (
        db,
        "SELECT count(*) FROM source_files;"
    );
    int source_roles = query_count (
        db,
        "SELECT count(*) FROM source_roles;"
    );
    int document_sections = query_count (
        db,
        "SELECT count(*) FROM document_sections;"
    );
    int structured_entities = query_count (
        db,
        "SELECT count(*) FROM structured_entities;"
    );
    int structured_relations = query_count (
        db,
        "SELECT count(*) FROM structured_relations;"
    );
    int datasets = query_count (
        db,
        "SELECT count(*) FROM datasets;"
    );
    int dataset_rows = query_count (
        db,
        "SELECT count(*) FROM dataset_rows;"
    );
    int fts_rows = query_count (
        db,
        "SELECT count(*) FROM search_fts;"
    );

    g_assert_cmpint (source_files, >, 0);
    g_assert_cmpint (source_roles, >=, source_files);
    g_assert_cmpint (document_sections, >, 0);
    g_assert_cmpint (fts_rows, >, 0);

    if (g_strcmp0 (repository->repository_id, "rmd") == 0) {
        g_assert_cmpint (
            query_count (
                db,
                "SELECT count(*) FROM structured_entities e "
                "JOIN source_files s ON s.id = e.source_id "
                "WHERE e.native_id = "
                "'government_refinancing_interest_loop' "
                "AND e.entity_type = 'feedback_loop' "
                "AND e.logical_source_id = "
                "'rmd:entity:feedback_loop:"
                "model/dynamics/feedback_registry.json:"
                "json:/loops/0' "
                "AND e.locator = 'json:/loops/0' "
                "AND s.path = "
                "'model/dynamics/feedback_registry.json';"
            ),
            ==,
            1
        );
    }

    g_print (
        "PASS: %s version=%s sha=%s "
        "sources=%d roles=%d sections=%d entities=%d "
        "relations=%d datasets=%d rows=%d fts=%d\n",
        repository->name,
        version,
        snapshot_sha,
        source_files,
        source_roles,
        document_sections,
        structured_entities,
        structured_relations,
        datasets,
        dataset_rows,
        fts_rows
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    char *reused_path = NULL;
    char *reused_version = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            repository->repository_id,
            snapshot_sha,
            &reused_path,
            &reused_version,
            &ensure_result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        ensure_result,
        ==,
        ATM_RETRIEVAL_ENSURE_REUSED
    );
    g_assert_cmpstr (reused_path, ==, index_path);
    g_assert_cmpstr (reused_version, ==, version);

    g_free (reused_version);
    g_free (reused_path);
    g_free (version);
    g_free (index_path);

    char *retrieval_dir = g_build_filename (
        cache_root,
        "retrieval",
        repository->repository_id,
        NULL
    );
    GDir *directory = g_dir_open (retrieval_dir, 0, NULL);
    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (
                retrieval_dir,
                name,
                NULL
            );
            g_remove (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_rmdir (retrieval_dir);
    char *retrieval_root = g_build_filename (
        cache_root,
        "retrieval",
        NULL
    );
    g_rmdir (retrieval_root);
    g_rmdir (cache_root);

    g_free (retrieval_root);
    g_free (retrieval_dir);
    g_free (cache_root);
}

int
main (void)
{
    const RealRepository repositories[] = {
        {
            "EWD",
            "ewd",
            "ATM_REAL_EWD_ROOT",
            "ATM_REAL_EWD_SHA"
        },
        {
            "CBD",
            "cbd",
            "ATM_REAL_CBD_ROOT",
            "ATM_REAL_CBD_SHA"
        },
        {
            "RMD",
            "rmd",
            "ATM_REAL_RMD_ROOT",
            "ATM_REAL_RMD_SHA"
        }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (repositories); i++) {
        exercise_repository (&repositories[i]);
    }

    g_print (
        "PASS: real EWD/CBD/RMD snapshots build and reopen validated R2 indexes\n"
    );
    return 0;
}
