#define _GNU_SOURCE

#include "repository_gc_isolation.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

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
        g_dir_open (
            path,
            0,
            NULL
        );

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
    char *root =
        g_dir_make_tmp (
            "atm-c1-i4-isolation-XXXXXX",
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static char *
snapshot_path (
    const char *root
)
{
    return g_build_filename (
        root,
        "Repositories",
        "ewd",
        "snapshots",
        TEST_SHA,
        NULL
    );
}

static void
create_snapshot_fixture (
    const char *root
)
{
    char *snapshot = snapshot_path (root);
    char *file = g_build_filename (
        snapshot,
        "STATUS.md",
        NULL
    );
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            snapshot,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            file,
            "# isolated fixture\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (file);
    g_free (snapshot);
}

static void
assert_real_directory (
    const char *path
)
{
    GStatBuf st;

    g_assert_cmpint (
        g_lstat (
            path,
            &st
        ),
        ==,
        0
    );
    g_assert_true (
        S_ISDIR (st.st_mode)
    );
    g_assert_false (
        S_ISLNK (st.st_mode)
    );
}

static void
test_isolate_moves_snapshot_and_preserves_payload (void)
{
    char *root = new_root ();
    create_snapshot_fixture (root);

    char *source = snapshot_path (root);
    char *trash_path = NULL;
    AtmRepositoryGcIsolationStats stats = { 0 };
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &trash_path,
            &stats,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (trash_path);
    g_assert_false (
        g_file_test (
            source,
            G_FILE_TEST_EXISTS
        )
    );
    assert_real_directory (
        trash_path
    );

    char *expected_prefix =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            "ewd",
            TEST_SHA,
            NULL
        );
    g_assert_true (
        g_str_has_prefix (
            trash_path,
            expected_prefix
        )
    );

    char *payload =
        g_build_filename (
            trash_path,
            "STATUS.md",
            NULL
        );
    char *contents = NULL;

    g_assert_true (
        g_file_get_contents (
            payload,
            &contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        contents,
        ==,
        "# isolated fixture\n"
    );
    g_assert_cmpuint (
        stats.namespace_fsync_calls,
        ==,
        4
    );
    g_assert_cmpuint (
        stats.isolation_fsync_calls,
        ==,
        2
    );

    g_free (contents);
    g_free (payload);
    g_free (expected_prefix);
    g_free (trash_path);
    g_free (source);
    remove_tree (root);
    g_free (root);
}

static void
test_isolate_never_overwrites_existing_trash_identity (void)
{
    char *root = new_root ();
    GError *error = NULL;
    char *first = NULL;
    char *second = NULL;

    create_snapshot_fixture (root);

    g_assert_true (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &first,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    create_snapshot_fixture (root);

    g_assert_true (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &second,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        first,
        !=,
        second
    );
    assert_real_directory (first);
    assert_real_directory (second);

    g_free (second);
    g_free (first);
    remove_tree (root);
    g_free (root);
}

static void
test_isolate_rejects_symlink_source (void)
{
    char *root = new_root ();
    char *snapshots =
        g_build_filename (
            root,
            "Repositories",
            "ewd",
            "snapshots",
            NULL
        );
    char *target =
        g_build_filename (
            root,
            "target",
            NULL
        );
    char *source =
        g_build_filename (
            snapshots,
            TEST_SHA,
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            snapshots,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir (
            target,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        symlink (
            target,
            source
        ),
        ==,
        0
    );

    char *trash_path = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &trash_path,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (trash_path);
    g_assert_true (
        g_file_test (
            source,
            G_FILE_TEST_IS_SYMLINK
        )
    );
    g_clear_error (&error);

    g_free (source);
    g_free (target);
    g_free (snapshots);
    remove_tree (root);
    g_free (root);
}

static void
test_isolate_rejects_symlink_trash_root (void)
{
    char *root = new_root ();
    create_snapshot_fixture (root);

    char *repositories =
        g_build_filename (
            root,
            "Repositories",
            NULL
        );
    char *target =
        g_build_filename (
            root,
            "trash-target",
            NULL
        );
    char *trash =
        g_build_filename (
            repositories,
            ".trash",
            NULL
        );
    char *source = snapshot_path (root);

    g_assert_cmpint (
        g_mkdir (
            target,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        symlink (
            target,
            trash
        ),
        ==,
        0
    );

    char *trash_path = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &trash_path,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (trash_path);
    assert_real_directory (source);
    g_clear_error (&error);

    g_free (source);
    g_free (trash);
    g_free (target);
    g_free (repositories);
    remove_tree (root);
    g_free (root);
}

static void
test_isolate_rejects_invalid_identity (void)
{
    char *root = new_root ();
    char *trash_path = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "unknown",
            TEST_SHA,
            &trash_path,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (trash_path);
    g_clear_error (&error);

    g_assert_false (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
            &trash_path,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (trash_path);
    g_clear_error (&error);

    remove_tree (root);
    g_free (root);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (
        &argc,
        &argv,
        NULL
    );

    g_test_add_func (
        "/repository-gc-isolation/move-preserve-payload",
        test_isolate_moves_snapshot_and_preserves_payload
    );
    g_test_add_func (
        "/repository-gc-isolation/no-overwrite",
        test_isolate_never_overwrites_existing_trash_identity
    );
    g_test_add_func (
        "/repository-gc-isolation/reject-symlink-source",
        test_isolate_rejects_symlink_source
    );
    g_test_add_func (
        "/repository-gc-isolation/reject-symlink-trash-root",
        test_isolate_rejects_symlink_trash_root
    );
    g_test_add_func (
        "/repository-gc-isolation/reject-invalid-identity",
        test_isolate_rejects_invalid_identity
    );

    return g_test_run ();
}
