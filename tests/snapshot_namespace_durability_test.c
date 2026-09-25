#define _GNU_SOURCE

#include "snapshot_namespace_durability.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static void
remove_tree (
    const char *path
)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0) {
        return;
    }

    if (!S_ISDIR (st.st_mode) ||
        S_ISLNK (st.st_mode)) {
        g_remove (path);
        return;
    }

    GDir *directory =
        g_dir_open (path, 0, NULL);

    if (directory != NULL) {
        const char *name;

        while ((name =
                    g_dir_read_name (
                        directory
                    )) != NULL) {
            char *child =
                g_build_filename (
                    path,
                    name,
                    NULL
                );
            remove_tree (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_rmdir (path);
}

static char *
new_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-snapshot-namespace-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
test_prepare_counts_and_idempotence (void)
{
    char *root = new_root ();
    GError *error = NULL;
    AtmSnapshotNamespaceStats first = { 0 };

    g_assert_true (
        atm_snapshot_namespace_prepare_final_parent (
            root,
            "ewd",
            &first,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        first.directories_created,
        ==,
        3
    );
    g_assert_cmpuint (
        first.directory_fsync_calls,
        ==,
        6
    );

    char *snapshots = g_build_filename (
        root,
        "Repositories",
        "ewd",
        "snapshots",
        NULL
    );
    g_assert_true (
        g_file_test (
            snapshots,
            G_FILE_TEST_IS_DIR
        )
    );

    AtmSnapshotNamespaceStats second = { 0 };

    g_assert_true (
        atm_snapshot_namespace_prepare_final_parent (
            root,
            "ewd",
            &second,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        second.directories_created,
        ==,
        0
    );
    g_assert_cmpuint (
        second.directory_fsync_calls,
        ==,
        6
    );

    g_free (snapshots);
    remove_tree (root);
    g_free (root);
}

static void
test_reject_symlink_component (void)
{
    char *root = new_root ();
    char *target = g_build_filename (
        root,
        "target",
        NULL
    );
    char *repositories = g_build_filename (
        root,
        "Repositories",
        NULL
    );

    g_assert_cmpint (
        g_mkdir (target, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        symlink ("target", repositories),
        ==,
        0
    );

    AtmSnapshotNamespaceStats stats = { 0 };
    GError *error = NULL;

    g_assert_false (
        atm_snapshot_namespace_prepare_final_parent (
            root,
            "ewd",
            &stats,
            &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_free (repositories);
    g_free (target);
    remove_tree (root);
    g_free (root);
}

static void
test_reject_unknown_repository (void)
{
    char *root = new_root ();
    AtmSnapshotNamespaceStats stats = { 0 };
    GError *error = NULL;

    g_assert_false (
        atm_snapshot_namespace_prepare_final_parent (
            root,
            "unknown",
            &stats,
            &error
        )
    );
    g_assert_error (
        error,
        G_FILE_ERROR,
        G_FILE_ERROR_INVAL
    );
    g_clear_error (&error);

    remove_tree (root);
    g_free (root);
}

static void
test_reject_missing_data_root (void)
{
    char *root = new_root ();
    char *missing = g_build_filename (
        root,
        "missing",
        NULL
    );
    AtmSnapshotNamespaceStats stats = { 0 };
    GError *error = NULL;

    g_assert_false (
        atm_snapshot_namespace_prepare_final_parent (
            missing,
            "ewd",
            &stats,
            &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_free (missing);
    remove_tree (root);
    g_free (root);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/snapshot-namespace/prepare-counts-idempotent",
        test_prepare_counts_and_idempotence
    );
    g_test_add_func (
        "/snapshot-namespace/reject-symlink",
        test_reject_symlink_component
    );
    g_test_add_func (
        "/snapshot-namespace/reject-unknown-repository",
        test_reject_unknown_repository
    );
    g_test_add_func (
        "/snapshot-namespace/reject-missing-root",
        test_reject_missing_data_root
    );

    return g_test_run ();
}
