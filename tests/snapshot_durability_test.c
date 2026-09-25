#include "snapshot_durability.h"

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

    if (!S_ISDIR (st.st_mode) || S_ISLNK (st.st_mode)) {
        g_remove (path);
        return;
    }

    GDir *directory = g_dir_open (path, 0, NULL);

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (
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
new_fixture_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-snapshot-durability-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
test_tree_counts (void)
{
    char *root = new_fixture_root ();
    char *nested = g_build_filename (
        root,
        "nested",
        NULL
    );
    char *first = g_build_filename (
        root,
        "first.txt",
        NULL
    );
    char *second = g_build_filename (
        nested,
        "second.txt",
        NULL
    );

    g_assert_cmpint (
        g_mkdir (nested, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            first,
            "first\n",
            -1,
            NULL
        )
    );
    g_assert_true (
        g_file_set_contents (
            second,
            "second\n",
            -1,
            NULL
        )
    );

    AtmSnapshotDurabilityStats stats = { 0 };
    GError *error = NULL;

    g_assert_true (
        atm_snapshot_durability_sync_tree (
            root,
            &stats,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        stats.file_fsync_calls,
        ==,
        2
    );
    g_assert_cmpuint (
        stats.directory_fsync_calls,
        ==,
        2
    );
    g_assert_cmpuint (
        stats.parent_fsync_calls,
        ==,
        0
    );

    remove_tree (root);
    g_free (second);
    g_free (first);
    g_free (nested);
    g_free (root);
}

static void
test_parent_count (void)
{
    char *root = new_fixture_root ();
    char *snapshot = g_build_filename (
        root,
        "snapshot",
        NULL
    );

    g_assert_cmpint (
        g_mkdir (snapshot, 0700),
        ==,
        0
    );

    AtmSnapshotDurabilityStats stats = { 0 };
    GError *error = NULL;

    g_assert_true (
        atm_snapshot_durability_sync_parent (
            snapshot,
            &stats,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        stats.parent_fsync_calls,
        ==,
        1
    );

    remove_tree (root);
    g_free (snapshot);
    g_free (root);
}

static void
test_reject_symlink (void)
{
    char *root = new_fixture_root ();
    char *target = g_build_filename (
        root,
        "target.txt",
        NULL
    );
    char *link = g_build_filename (
        root,
        "link.txt",
        NULL
    );

    g_assert_true (
        g_file_set_contents (
            target,
            "target\n",
            -1,
            NULL
        )
    );
    g_assert_cmpint (
        symlink ("target.txt", link),
        ==,
        0
    );

    AtmSnapshotDurabilityStats stats = { 0 };
    GError *error = NULL;

    g_assert_false (
        atm_snapshot_durability_sync_tree (
            root,
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
    g_free (link);
    g_free (target);
    g_free (root);
}

static void
test_reject_fifo (void)
{
    char *root = new_fixture_root ();
    char *fifo = g_build_filename (
        root,
        "pipe",
        NULL
    );

    g_assert_cmpint (
        mkfifo (fifo, 0600),
        ==,
        0
    );

    AtmSnapshotDurabilityStats stats = { 0 };
    GError *error = NULL;

    g_assert_false (
        atm_snapshot_durability_sync_tree (
            root,
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
    g_free (fifo);
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
        "/snapshot-durability/tree-counts",
        test_tree_counts
    );
    g_test_add_func (
        "/snapshot-durability/parent-count",
        test_parent_count
    );
    g_test_add_func (
        "/snapshot-durability/reject-symlink",
        test_reject_symlink
    );
    g_test_add_func (
        "/snapshot-durability/reject-fifo",
        test_reject_fifo
    );

    return g_test_run ();
}
