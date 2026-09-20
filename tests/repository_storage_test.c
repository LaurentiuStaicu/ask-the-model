#include "repository_storage.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
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
new_data_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-storage-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static const char *
valid_sha (void)
{
    return "0123456789abcdef0123456789abcdef01234567";
}

static void
test_storage_paths (void)
{
    char *root = new_data_root ();
    char *snapshot = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );

    char *snapshot_suffix = g_build_filename (
        "repositories",
        "ewd",
        "snapshots",
        valid_sha (),
        NULL
    );
    char *staging_name = g_strdup_printf (
        "%s.part",
        valid_sha ()
    );
    char *staging_suffix = g_build_filename (
        "repository-staging",
        "ewd",
        staging_name,
        NULL
    );

    g_assert_true (g_str_has_suffix (snapshot, snapshot_suffix));
    g_assert_true (g_str_has_suffix (staging, staging_suffix));

    g_free (snapshot_suffix);
    g_free (staging_name);
    g_free (staging_suffix);
    g_free (snapshot);
    g_free (staging);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_atomic_promotion (void)
{
    char *root = new_data_root ();
    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *staging_parent = g_path_get_dirname (staging);
    char *file = g_build_filename (staging, "STATUS.md", NULL);
    char *snapshot = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (staging, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            file,
            "# ready\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_repository_promote_snapshot (
            root,
            "ewd",
            valid_sha (),
            staging,
            &snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (snapshot, G_FILE_TEST_IS_DIR));

    char *promoted_file = g_build_filename (
        snapshot,
        "STATUS.md",
        NULL
    );
    char *contents = NULL;

    g_assert_true (
        g_file_get_contents (
            promoted_file,
            &contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (contents, ==, "# ready\n");

    g_free (contents);
    g_free (promoted_file);
    g_free (snapshot);
    g_free (file);
    g_free (staging_parent);
    g_free (staging);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_existing_snapshot_not_overwritten (void)
{
    char *root = new_data_root ();
    char *snapshot = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *snapshot_parent = g_path_get_dirname (snapshot);
    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *existing_file = g_build_filename (
        snapshot,
        "sentinel.txt",
        NULL
    );
    char *new_file = g_build_filename (
        staging,
        "new.txt",
        NULL
    );
    char *promoted = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (snapshot, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir_with_parents (staging, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            existing_file,
            "keep\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        g_file_set_contents (
            new_file,
            "replace\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_repository_promote_snapshot (
            root,
            "ewd",
            valid_sha (),
            staging,
            &promoted,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_EXISTS
    );
    g_assert_null (promoted);
    g_assert_true (g_file_test (existing_file, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (new_file, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_free (existing_file);
    g_free (new_file);
    g_free (snapshot_parent);
    g_free (snapshot);
    g_free (staging);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_wrong_staging_path_rejected (void)
{
    char *root = new_data_root ();
    char *wrong = g_build_filename (root, "wrong", NULL);
    char *promoted = NULL;
    GError *error = NULL;

    g_assert_cmpint (g_mkdir (wrong, 0700), ==, 0);

    g_assert_false (
        atm_repository_promote_snapshot (
            root,
            "ewd",
            valid_sha (),
            wrong,
            &promoted,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_INVALID_STAGING
    );
    g_assert_null (promoted);
    g_assert_true (g_file_test (wrong, G_FILE_TEST_IS_DIR));

    g_clear_error (&error);
    g_free (wrong);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_identity_inputs_rejected (void)
{
    char *root = new_data_root ();
    char *promoted = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_promote_snapshot (
            root,
            "other",
            valid_sha (),
            root,
            &promoted,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_INVALID_ID
    );
    g_clear_error (&error);

    g_assert_false (
        atm_repository_promote_snapshot (
            root,
            "ewd",
            "not-a-sha",
            root,
            &promoted,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_INVALID_SHA
    );

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/storage/paths", test_storage_paths);
    g_test_add_func (
        "/storage/atomic-promotion",
        test_atomic_promotion
    );
    g_test_add_func (
        "/storage/existing-not-overwritten",
        test_existing_snapshot_not_overwritten
    );
    g_test_add_func (
        "/storage/wrong-staging-rejected",
        test_wrong_staging_path_rejected
    );
    g_test_add_func (
        "/storage/invalid-identity-inputs",
        test_invalid_identity_inputs_rejected
    );

    return g_test_run ();
}
