#define _GNU_SOURCE

#include "repository_gc_isolation.h"
#include "repository_gc_purge.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static const char *SHA_A =
    "0123456789abcdef0123456789abcdef01234567";
static const char *SHA_B =
    "89abcdef0123456789abcdef0123456789abcdef";

static void
remove_tree (
    const char *path
)
{
    GStatBuf st;

    if (g_lstat (
            path,
            &st
        ) != 0) {
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
            "atm-c1-i5-purge-XXXXXX",
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static char *
snapshot_path (
    const char *root,
    const char *sha
)
{
    return g_build_filename (
        root,
        "Repositories",
        "ewd",
        "snapshots",
        sha,
        NULL
    );
}

static void
create_snapshot_fixture (
    const char *root,
    const char *sha
)
{
    char *snapshot =
        snapshot_path (
            root,
            sha
        );
    char *nested =
        g_build_filename (
            snapshot,
            "nested",
            NULL
        );
    char *file_a =
        g_build_filename (
            snapshot,
            "A.txt",
            NULL
        );
    char *file_b =
        g_build_filename (
            nested,
            "B.txt",
            NULL
        );
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            nested,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            file_a,
            "A\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        g_file_set_contents (
            file_b,
            "B\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (file_b);
    g_free (file_a);
    g_free (nested);
    g_free (snapshot);
}

static char *
isolate_fixture (
    const char *root,
    const char *sha
)
{
    create_snapshot_fixture (
        root,
        sha
    );

    char *trash_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            sha,
            &trash_path,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (trash_path);
    return trash_path;
}

static void
test_purge_removes_only_selected_isolated_tree (void)
{
    char *root = new_root ();
    char *trash_path =
        isolate_fixture (
            root,
            SHA_A
        );
    char *trash_name =
        g_path_get_basename (
            trash_path
        );
    AtmRepositoryGcPurgeStats stats = { 0 };
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            trash_name,
            &stats,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        g_file_test (
            trash_path,
            G_FILE_TEST_EXISTS
        )
    );

    char *source =
        snapshot_path (
            root,
            SHA_A
        );
    g_assert_false (
        g_file_test (
            source,
            G_FILE_TEST_EXISTS
        )
    );

    g_assert_cmpuint (
        stats.regular_files_removed,
        ==,
        2
    );
    g_assert_cmpuint (
        stats.directories_removed,
        ==,
        2
    );
    g_assert_cmpuint (
        stats.directory_fsync_calls,
        ==,
        3
    );

    g_free (source);
    g_free (trash_name);
    g_free (trash_path);
    remove_tree (root);
    g_free (root);
}

static void
test_purge_preserves_other_trash_entry (void)
{
    char *root = new_root ();
    char *first =
        isolate_fixture (
            root,
            SHA_A
        );
    char *second =
        isolate_fixture (
            root,
            SHA_B
        );
    char *first_name =
        g_path_get_basename (
            first
        );
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            first_name,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        g_file_test (
            first,
            G_FILE_TEST_EXISTS
        )
    );
    g_assert_true (
        g_file_test (
            second,
            G_FILE_TEST_IS_DIR
        )
    );

    g_free (first_name);
    g_free (second);
    g_free (first);
    remove_tree (root);
    g_free (root);
}

static void
test_purge_rejects_noncanonical_identity (void)
{
    char *root = new_root ();
    char *trash_path =
        isolate_fixture (
            root,
            SHA_A
        );

    const char *invalid_names[] = {
        ".invalid-not-a-trash-id",
        "A123456789abcdef0123456789abcdef01234567-1-1-0",
        "0123456789abcdef0123456789abcdef01234567-01-1-0",
        "0123456789abcdef0123456789abcdef01234567-1-01-0",
        "0123456789abcdef0123456789abcdef01234567-1-1-00",
        "0123456789abcdef0123456789abcdef01234567-0-1-0",
        "0123456789abcdef0123456789abcdef01234567-1-0-0",
        "0123456789abcdef0123456789abcdef01234567-1-2147483648-0",
        "0123456789abcdef0123456789abcdef01234567-1-1-100",
        "0123456789abcdef0123456789abcdef01234567-9223372036854775808-1-0"
    };

    for (gsize i = 0;
         i < G_N_ELEMENTS (invalid_names);
         i++) {
        GError *error = NULL;

        g_assert_false (
            atm_repository_gc_purge_trash_entry (
                root,
                "ewd",
                invalid_names[i],
                NULL,
                &error
            )
        );
        g_assert_nonnull (error);
        g_clear_error (&error);

        g_assert_true (
            g_file_test (
                trash_path,
                G_FILE_TEST_IS_DIR
            )
        );
    }

    g_free (trash_path);
    remove_tree (root);
    g_free (root);
}

static void
test_purge_rejects_symlink_before_any_delete (void)
{
    char *root = new_root ();
    char *trash_path =
        isolate_fixture (
            root,
            SHA_A
        );
    char *trash_name =
        g_path_get_basename (
            trash_path
        );
    char *outside =
        g_build_filename (
            root,
            "outside",
            NULL
        );
    char *link =
        g_build_filename (
            trash_path,
            "link",
            NULL
        );
    char *file_a =
        g_build_filename (
            trash_path,
            "A.txt",
            NULL
        );
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir (
            outside,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        symlink (
            outside,
            link
        ),
        ==,
        0
    );

    g_assert_false (
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            trash_name,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_assert_true (
        g_file_test (
            trash_path,
            G_FILE_TEST_IS_DIR
        )
    );
    g_assert_true (
        g_file_test (
            file_a,
            G_FILE_TEST_IS_REGULAR
        )
    );
    g_assert_true (
        g_file_test (
            link,
            G_FILE_TEST_IS_SYMLINK
        )
    );
    g_assert_true (
        g_file_test (
            outside,
            G_FILE_TEST_IS_DIR
        )
    );

    g_free (file_a);
    g_free (link);
    g_free (outside);
    g_free (trash_name);
    g_free (trash_path);
    remove_tree (root);
    g_free (root);
}

static void
test_purge_rejects_symlink_trash_namespace (void)
{
    char *root = new_root ();
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
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            repositories,
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
            trash
        ),
        ==,
        0
    );

    g_assert_false (
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            "0123456789abcdef0123456789abcdef01234567-1-1-0",
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_assert_true (
        g_file_test (
            trash,
            G_FILE_TEST_IS_SYMLINK
        )
    );
    g_assert_true (
        g_file_test (
            target,
            G_FILE_TEST_IS_DIR
        )
    );

    g_free (trash);
    g_free (target);
    g_free (repositories);
    remove_tree (root);
    g_free (root);
}

static void
test_purge_rejects_non_directory_trash_entry (void)
{
    char *root = new_root ();
    char *trash_repo =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            "ewd",
            NULL
        );
    const char *name =
        "0123456789abcdef0123456789abcdef01234567-1-1-0";
    char *entry =
        g_build_filename (
            trash_repo,
            name,
            NULL
        );
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            trash_repo,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            entry,
            "not a directory",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            name,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_assert_true (
        g_file_test (
            entry,
            G_FILE_TEST_IS_REGULAR
        )
    );

    g_free (entry);
    g_free (trash_repo);
    remove_tree (root);
    g_free (root);
}

static void
test_purge_wide_directory_completes_from_stable_name_set (void)
{
    char *root = new_root ();
    char *trash_path =
        isolate_fixture (
            root,
            SHA_A
        );
    char *trash_name =
        g_path_get_basename (
            trash_path
        );
    char *wide =
        g_build_filename (
            trash_path,
            "wide",
            NULL
        );
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir (
            wide,
            0700
        ),
        ==,
        0
    );

    for (guint i = 0;
         i < 512;
         i++) {
        char *name =
            g_strdup_printf (
                "entry-%03u.txt",
                i
            );
        char *path =
            g_build_filename (
                wide,
                name,
                NULL
            );

        g_assert_true (
            g_file_set_contents (
                path,
                "x",
                1,
                &error
            )
        );
        g_assert_no_error (error);

        g_free (path);
        g_free (name);
    }

    AtmRepositoryGcPurgeStats stats = { 0 };

    g_assert_true (
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            trash_name,
            &stats,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        g_file_test (
            trash_path,
            G_FILE_TEST_EXISTS
        )
    );
    g_assert_cmpuint (
        stats.regular_files_removed,
        ==,
        514
    );
    g_assert_cmpuint (
        stats.directories_removed,
        ==,
        3
    );
    g_assert_cmpuint (
        stats.directory_fsync_calls,
        ==,
        4
    );

    g_free (wide);
    g_free (trash_name);
    g_free (trash_path);
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
        "/repository-gc-purge/selected-tree-only",
        test_purge_removes_only_selected_isolated_tree
    );
    g_test_add_func (
        "/repository-gc-purge/preserve-other-trash",
        test_purge_preserves_other_trash_entry
    );
    g_test_add_func (
        "/repository-gc-purge/reject-noncanonical-id",
        test_purge_rejects_noncanonical_identity
    );
    g_test_add_func (
        "/repository-gc-purge/reject-symlink-before-delete",
        test_purge_rejects_symlink_before_any_delete
    );
    g_test_add_func (
        "/repository-gc-purge/reject-symlink-trash-root",
        test_purge_rejects_symlink_trash_namespace
    );
    g_test_add_func (
        "/repository-gc-purge/reject-nondirectory-entry",
        test_purge_rejects_non_directory_trash_entry
    );
    g_test_add_func (
        "/repository-gc-purge/wide-directory-complete",
        test_purge_wide_directory_completes_from_stable_name_set
    );

    return g_test_run ();
}
