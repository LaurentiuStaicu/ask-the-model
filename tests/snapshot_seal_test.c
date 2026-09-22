#include "snapshot_seal.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void
remove_tree_best_effort (const char *path)
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
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_rmdir (path);
}

static char *
new_root (const char *pattern)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (pattern, &error);

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_file (
    const char *root,
    const char *relative,
    const char *contents,
    int mode
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
    g_assert_cmpint (chmod (path, mode), ==, 0);

    g_free (parent);
    g_free (path);
}

static char *
seal (
    const char *root,
    guint64 *out_files,
    guint64 *out_bytes
)
{
    GError *error = NULL;
    char *sha = NULL;

    g_assert_true (
        atm_snapshot_seal_compute (
            root,
            &sha,
            out_files,
            out_bytes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (sha);
    g_assert_cmpuint (strlen (sha), ==, 64);
    return sha;
}

static void
test_deterministic_creation_order (void)
{
    char *a = new_root ("atm-seal-a-XXXXXX");
    char *b = new_root ("atm-seal-b-XXXXXX");

    write_file (a, "z.txt", "z\n", 0644);
    write_file (a, "dir/b.txt", "b\n", 0644);
    write_file (a, "dir/a.sh", "#!/bin/sh\n", 0755);

    write_file (b, "dir/a.sh", "#!/bin/sh\n", 0755);
    write_file (b, "dir/b.txt", "b\n", 0644);
    write_file (b, "z.txt", "z\n", 0644);

    guint64 files_a = 0;
    guint64 bytes_a = 0;
    guint64 files_b = 0;
    guint64 bytes_b = 0;

    char *seal_a = seal (a, &files_a, &bytes_a);
    char *seal_b = seal (b, &files_b, &bytes_b);

    g_assert_cmpstr (seal_a, ==, seal_b);
    g_assert_cmpuint (files_a, ==, 3);
    g_assert_cmpuint (files_b, ==, 3);
    g_assert_cmpuint (bytes_a, ==, bytes_b);

    g_free (seal_a);
    g_free (seal_b);
    remove_tree_best_effort (a);
    remove_tree_best_effort (b);
    g_free (a);
    g_free (b);
}

static void
test_content_change_changes_seal (void)
{
    char *root = new_root ("atm-seal-content-XXXXXX");
    write_file (root, "file.txt", "first\n", 0644);

    char *before = seal (root, NULL, NULL);
    write_file (root, "file.txt", "second\n", 0644);
    char *after = seal (root, NULL, NULL);

    g_assert_cmpstr (before, !=, after);

    g_free (before);
    g_free (after);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_executable_bit_changes_seal (void)
{
    char *root = new_root ("atm-seal-mode-XXXXXX");
    char *path = g_build_filename (root, "tool.sh", NULL);

    write_file (root, "tool.sh", "#!/bin/sh\n", 0644);
    char *normal = seal (root, NULL, NULL);

    g_assert_cmpint (chmod (path, 0755), ==, 0);
    char *executable = seal (root, NULL, NULL);

    g_assert_cmpstr (normal, !=, executable);

    g_free (normal);
    g_free (executable);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_mtime_does_not_change_seal (void)
{
    char *root = new_root ("atm-seal-mtime-XXXXXX");
    char *path = g_build_filename (root, "stable.txt", NULL);

    write_file (root, "stable.txt", "stable\n", 0644);
    char *before = seal (root, NULL, NULL);

    struct timespec times[2];
    times[0].tv_sec = 123456789;
    times[0].tv_nsec = 0;
    times[1].tv_sec = 123456790;
    times[1].tv_nsec = 0;

    g_assert_cmpint (
        utimensat (
            AT_FDCWD,
            path,
            times,
            0
        ),
        ==,
        0
    );

    char *after = seal (root, NULL, NULL);
    g_assert_cmpstr (before, ==, after);

    g_free (before);
    g_free (after);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_symlink_rejected (void)
{
    GError *error = NULL;
    char *root = new_root ("atm-seal-symlink-XXXXXX");
    char *target = g_build_filename (root, "target.txt", NULL);
    char *link = g_build_filename (root, "link.txt", NULL);

    write_file (root, "target.txt", "target\n", 0644);
    g_assert_cmpint (symlink ("target.txt", link), ==, 0);

    char *sha = NULL;
    g_assert_false (
        atm_snapshot_seal_compute (
            root,
            &sha,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SNAPSHOT_SEAL_ERROR,
        ATM_SNAPSHOT_SEAL_ERROR_UNSAFE_ENTRY
    );
    g_assert_null (sha);

    g_clear_error (&error);
    g_free (target);
    g_free (link);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_add_remove_changes_seal (void)
{
    char *root = new_root ("atm-seal-add-XXXXXX");

    write_file (root, "a.txt", "a\n", 0644);
    char *one = seal (root, NULL, NULL);

    write_file (root, "b.txt", "b\n", 0644);
    char *two = seal (root, NULL, NULL);
    g_assert_cmpstr (one, !=, two);

    char *b = g_build_filename (root, "b.txt", NULL);
    g_assert_cmpint (g_remove (b), ==, 0);
    char *three = seal (root, NULL, NULL);

    g_assert_cmpstr (one, ==, three);

    g_free (one);
    g_free (two);
    g_free (three);
    g_free (b);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/snapshot-seal/deterministic-order",
        test_deterministic_creation_order
    );
    g_test_add_func (
        "/snapshot-seal/content-change",
        test_content_change_changes_seal
    );
    g_test_add_func (
        "/snapshot-seal/executable-bit",
        test_executable_bit_changes_seal
    );
    g_test_add_func (
        "/snapshot-seal/mtime-ignored",
        test_mtime_does_not_change_seal
    );
    g_test_add_func (
        "/snapshot-seal/symlink-rejected",
        test_symlink_rejected
    );
    g_test_add_func (
        "/snapshot-seal/add-remove",
        test_add_remove_changes_seal
    );

    return g_test_run ();
}
