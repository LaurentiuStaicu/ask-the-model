#define _GNU_SOURCE

#include "repository_gc_purge.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static const char *SHA_A =
    "0123456789abcdef0123456789abcdef01234567";
static const char *SHA_B =
    "89abcdef0123456789abcdef0123456789abcdef";
static const char *TRASH_A =
    "0123456789abcdef0123456789abcdef01234567-999-123-0";
static const char *TRASH_B =
    "89abcdef0123456789abcdef0123456789abcdef-1-123-0";

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
            "atm-c1-i8a-trash-discovery-XXXXXX",
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static char *
trash_path (
    const char *root,
    const char *repository_id,
    const char *trash_name
)
{
    return g_build_filename (
        root,
        "Repositories",
        ".trash",
        repository_id,
        trash_name,
        NULL
    );
}

static void
create_trash_directory (
    const char *root,
    const char *repository_id,
    const char *trash_name
)
{
    char *path =
        trash_path (
            root,
            repository_id,
            trash_name
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            path,
            0700
        ),
        ==,
        0
    );

    g_free (path);
}

static void
test_empty_namespace_returns_no_candidate (void)
{
    char *root = new_root ();
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_null (repository_id);
    g_assert_null (snapshot_sha);
    g_assert_null (trash_name);

    remove_tree (root);
    g_free (root);
}

static void
test_deterministic_repository_then_name_selection (void)
{
    char *root = new_root ();

    create_trash_directory (
        root,
        "ewd",
        TRASH_A
    );
    create_trash_directory (
        root,
        "cbd",
        TRASH_B
    );
    create_trash_directory (
        root,
        "cbd",
        TRASH_A
    );

    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        repository_id,
        ==,
        "cbd"
    );
    g_assert_cmpstr (
        snapshot_sha,
        ==,
        SHA_A
    );
    g_assert_cmpstr (
        trash_name,
        ==,
        TRASH_A
    );

    char *selected_path =
        trash_path (
            root,
            repository_id,
            trash_name
        );
    g_assert_true (
        g_file_test (
            selected_path,
            G_FILE_TEST_IS_DIR
        )
    );

    g_free (selected_path);
    g_free (trash_name);
    g_free (snapshot_sha);
    g_free (repository_id);
    remove_tree (root);
    g_free (root);
}

static void
test_unknown_repository_namespace_fails_closed (void)
{
    char *root = new_root ();
    char *unknown =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            "unknown",
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            unknown,
            0700
        ),
        ==,
        0
    );

    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (repository_id);
    g_assert_null (snapshot_sha);
    g_assert_null (trash_name);

    g_clear_error (&error);
    g_free (unknown);
    remove_tree (root);
    g_free (root);
}

static void
test_noncanonical_entry_fails_closed (void)
{
    char *root = new_root ();

    create_trash_directory (
        root,
        "cbd",
        "not-a-canonical-trash-entry"
    );

    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (repository_id);
    g_assert_null (snapshot_sha);
    g_assert_null (trash_name);

    g_clear_error (&error);
    remove_tree (root);
    g_free (root);
}

static void
test_symlink_entry_fails_closed (void)
{
    char *root = new_root ();
    char *repository =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            "cbd",
            NULL
        );
    char *outside =
        g_build_filename (
            root,
            "outside",
            NULL
        );
    char *link =
        g_build_filename (
            repository,
            TRASH_A,
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            repository,
            0700
        ),
        ==,
        0
    );
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

    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (repository_id);
    g_assert_null (snapshot_sha);
    g_assert_null (trash_name);

    g_clear_error (&error);
    g_free (link);
    g_free (outside);
    g_free (repository);
    remove_tree (root);
    g_free (root);
}

static void
test_symlink_repository_namespace_fails_closed (void)
{
    char *root = new_root ();
    char *trash =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            NULL
        );
    char *outside =
        g_build_filename (
            root,
            "outside",
            NULL
        );
    char *link =
        g_build_filename (
            trash,
            "cbd",
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            trash,
            0700
        ),
        ==,
        0
    );
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

    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (repository_id);
    g_assert_null (snapshot_sha);
    g_assert_null (trash_name);

    g_clear_error (&error);
    g_free (link);
    g_free (outside);
    g_free (trash);
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
        "/repository-gc-i8a/empty",
        test_empty_namespace_returns_no_candidate
    );
    g_test_add_func (
        "/repository-gc-i8a/deterministic-selection",
        test_deterministic_repository_then_name_selection
    );
    g_test_add_func (
        "/repository-gc-i8a/unknown-repository",
        test_unknown_repository_namespace_fails_closed
    );
    g_test_add_func (
        "/repository-gc-i8a/noncanonical-entry",
        test_noncanonical_entry_fails_closed
    );
    g_test_add_func (
        "/repository-gc-i8a/symlink-entry",
        test_symlink_entry_fails_closed
    );
    g_test_add_func (
        "/repository-gc-i8a/symlink-repository",
        test_symlink_repository_namespace_fails_closed
    );

    return g_test_run ();
}
