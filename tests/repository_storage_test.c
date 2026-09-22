#include "repository_storage.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static const char *
second_valid_sha (void)
{
    return "89abcdef0123456789abcdef0123456789abcdef";
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
        "Repositories",
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
        "Repositories",
        ".staging",
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
test_invalid_snapshot_quarantine (void)
{
    char *root = new_data_root ();
    char *snapshot = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *file = g_build_filename (
        snapshot,
        "tampered.txt",
        NULL
    );
    char *quarantine = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (snapshot, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            file,
            "preserve for diagnosis\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_repository_quarantine_snapshot (
            root,
            "ewd",
            valid_sha (),
            &quarantine,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (quarantine);
    g_assert_false (
        g_file_test (
            snapshot,
            G_FILE_TEST_EXISTS
        )
    );
    g_assert_true (
        g_file_test (
            quarantine,
            G_FILE_TEST_IS_DIR
        )
    );

    char *preserved = g_build_filename (
        quarantine,
        "tampered.txt",
        NULL
    );
    char *contents = NULL;
    g_assert_true (
        g_file_get_contents (
            preserved,
            &contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        contents,
        ==,
        "preserve for diagnosis\n"
    );

    g_free (contents);
    g_free (preserved);
    g_free (quarantine);
    g_free (file);
    g_free (snapshot);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_symlink_snapshot_not_quarantined (void)
{
    char *root = new_data_root ();
    char *target = g_build_filename (
        root,
        "target",
        NULL
    );
    char *snapshot = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *snapshot_parent =
        g_path_get_dirname (snapshot);
    char *quarantine = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            snapshot_parent,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir (target, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        symlink (target, snapshot),
        ==,
        0
    );

    g_assert_false (
        atm_repository_quarantine_snapshot (
            root,
            "ewd",
            valid_sha (),
            &quarantine,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_INVALID_SNAPSHOT
    );
    g_assert_null (quarantine);
    g_assert_true (
        g_file_test (
            snapshot,
            G_FILE_TEST_IS_DIR
        )
    );

    g_clear_error (&error);
    g_free (snapshot_parent);
    g_free (snapshot);
    g_free (target);
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

static void
test_distinct_snapshots_coexist (void)
{
    char *root = new_data_root ();
    char *first_staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *second_staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        second_valid_sha ()
    );
    char *first_file = g_build_filename (
        first_staging,
        "STATUS.md",
        NULL
    );
    char *second_file = g_build_filename (
        second_staging,
        "STATUS.md",
        NULL
    );
    char *first_snapshot = NULL;
    char *second_snapshot = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (first_staging, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir_with_parents (second_staging, 0700),
        ==,
        0
    );

    g_assert_true (
        g_file_set_contents (
            first_file,
            "old snapshot\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (
            second_file,
            "new snapshot\n",
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
            first_staging,
            &first_snapshot,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_repository_promote_snapshot (
            root,
            "ewd",
            second_valid_sha (),
            second_staging,
            &second_snapshot,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (first_snapshot, !=, second_snapshot);
    g_assert_true (
        g_file_test (
            first_snapshot,
            G_FILE_TEST_IS_DIR
        )
    );
    g_assert_true (
        g_file_test (
            second_snapshot,
            G_FILE_TEST_IS_DIR
        )
    );

    char *first_promoted_file = g_build_filename (
        first_snapshot,
        "STATUS.md",
        NULL
    );
    char *second_promoted_file = g_build_filename (
        second_snapshot,
        "STATUS.md",
        NULL
    );
    char *first_contents = NULL;
    char *second_contents = NULL;

    g_assert_true (
        g_file_get_contents (
            first_promoted_file,
            &first_contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_get_contents (
            second_promoted_file,
            &second_contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (first_contents, ==, "old snapshot\n");
    g_assert_cmpstr (second_contents, ==, "new snapshot\n");

    g_free (second_contents);
    g_free (first_contents);
    g_free (second_promoted_file);
    g_free (first_promoted_file);
    g_free (second_snapshot);
    g_free (first_snapshot);
    g_free (second_file);
    g_free (first_file);
    g_free (second_staging);
    g_free (first_staging);
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
        "/storage/distinct-snapshots-coexist",
        test_distinct_snapshots_coexist
    );
    g_test_add_func (
        "/storage/quarantine-invalid-snapshot",
        test_invalid_snapshot_quarantine
    );
    g_test_add_func (
        "/storage/quarantine-rejects-symlink",
        test_symlink_snapshot_not_quarantined
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
