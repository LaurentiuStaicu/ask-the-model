#include "retrieval_index_lifecycle.h"
#include "retrieval_index.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include <sys/stat.h>

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
        "atm-index-lifecycle-snapshot-XXXXXX"
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
        "\"CITATION.cff\", \"STATUS.md\", \"model/core.json\"],\n"
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
        "CITATION.cff",
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Test EWD\n"
        "version: 0.1.0\n"
    );

    write_text (
        root,
        "STATUS.md",
        "# Scientific status\n"
        "## Release status\n"
        "Current baseline.\n"
    );

    write_text (
        root,
        "model/core.json",
        "{\"variables\":["
        "{\"id\":\"food_per_capita\","
        "\"label\":{\"en\":\"Food per capita\"}}"
        "]}\n"
    );

    write_text (
        root,
        "data/series.csv",
        "year,value\n2025,1\n"
    );

    return root;
}

static const char *
snapshot_sha (void)
{
    return "0123456789abcdef0123456789abcdef01234567";
}

static void
test_missing_index_is_built_and_then_reused (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_assert_cmpstr (version, ==, "0.1.0");
    g_assert_true (
        g_file_test (index_path, G_FILE_TEST_IS_REGULAR)
    );

    char *first_path = g_strdup (index_path);
    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REUSED
    );
    g_assert_cmpstr (index_path, ==, first_path);
    g_assert_cmpstr (version, ==, "0.1.0");

    g_free (first_path);
    g_free (index_path);
    g_free (version);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_deleted_cache_is_rebuilt_from_snapshot (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (g_remove (index_path), ==, 0);
    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_assert_true (
        g_file_test (index_path, G_FILE_TEST_IS_REGULAR)
    );

    g_free (index_path);
    g_free (version);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_old_schema_index_is_rebuilt (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    sqlite3 *db = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "PRAGMA user_version = 999;",
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );

    g_assert_true (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &error
        )
    );
    g_assert_no_error (error);

    g_free (index_path);
    g_free (version);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_corrupt_index_is_rebuilt (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = atm_retrieval_index_path (
        cache_root,
        "ewd",
        snapshot_sha ()
    );
    char *parent = g_path_get_dirname (index_path);
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (parent, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            index_path,
            "not sqlite",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_clear_pointer (&index_path, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );

    g_assert_true (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &error
        )
    );
    g_assert_no_error (error);

    g_free (version);
    g_free (parent);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_non_regular_cache_path_is_not_replaced (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = atm_retrieval_index_path (
        cache_root,
        "ewd",
        snapshot_sha ()
    );
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (index_path, 0700),
        ==,
        0
    );
    g_clear_pointer (&index_path, g_free);

    g_assert_false (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_LIFECYCLE_ERROR,
        ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE
    );
    g_assert_null (index_path);
    g_assert_null (version);

    g_clear_error (&error);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-lifecycle/missing-build-reuse",
        test_missing_index_is_built_and_then_reused
    );
    g_test_add_func (
        "/retrieval-lifecycle/cache-loss-rebuild",
        test_deleted_cache_is_rebuilt_from_snapshot
    );
    g_test_add_func (
        "/retrieval-lifecycle/old-schema-rebuild",
        test_old_schema_index_is_rebuilt
    );
    g_test_add_func (
        "/retrieval-lifecycle/corrupt-index-rebuild",
        test_corrupt_index_is_rebuilt
    );
    g_test_add_func (
        "/retrieval-lifecycle/non-regular-cache-refused",
        test_non_regular_cache_path_is_not_replaced
    );

    return g_test_run ();
}
