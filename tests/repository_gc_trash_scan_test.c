#define _GNU_SOURCE

#include "repository_gc_trash_scan.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define SHA_C "cccccccccccccccccccccccccccccccccccccccc"

static void
remove_tree (
    const char *path
)
{
    struct stat st;

    if (g_lstat (
            path,
            &st
        ) != 0) {
        return;
    }

    if (S_ISDIR (
            st.st_mode
        ) &&
        !S_ISLNK (
            st.st_mode
        )) {
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
                g_autofree char *child =
                    g_build_filename (
                        path,
                        name,
                        NULL
                    );

                remove_tree (
                    child
                );
            }

            g_dir_close (
                directory
            );
        }

        g_rmdir (
            path
        );
        return;
    }

    g_remove (
        path
    );
}

static char *
new_root (void)
{
    GError *error = NULL;
    char *root =
        g_dir_make_tmp (
            "atm-gc-trash-discovery-XXXXXX",
            &error
        );

    g_assert_no_error (
        error
    );
    g_assert_nonnull (
        root
    );

    g_autofree char *repositories =
        g_build_filename (
            root,
            "Repositories",
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            repositories,
            0700
        ),
        ==,
        0
    );

    return root;
}

static void
make_trash_directory (
    const char *root,
    const char *repository_id,
    const char *trash_name
)
{
    g_autofree char *path =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            repository_id,
            trash_name,
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            path,
            0700
        ),
        ==,
        0
    );
}

static void
test_missing_trash_is_empty (void)
{
    g_autofree char *root =
        new_root ();
    gboolean found = TRUE;
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &found,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_no_error (
        error
    );
    g_assert_false (
        found
    );
    g_assert_cmpstr (
        repository_id,
        ==,
        ""
    );
    g_assert_cmpstr (
        snapshot_sha,
        ==,
        ""
    );
    g_assert_cmpstr (
        trash_name,
        ==,
        ""
    );

    g_free (
        repository_id
    );
    g_free (
        snapshot_sha
    );
    g_free (
        trash_name
    );
    remove_tree (
        root
    );
}

static void
test_repository_then_basename_selection (void)
{
    g_autofree char *root =
        new_root ();

    /*
     * Repository ID is the primary key, so cbd wins over ewd/rmd even though
     * its timestamp is newer. Within cbd, basename ordering makes SHA_A win.
     */
    make_trash_directory (
        root,
        "ewd",
        SHA_A "-1-1-0"
    );
    make_trash_directory (
        root,
        "rmd",
        SHA_A "-1-1-0"
    );
    make_trash_directory (
        root,
        "cbd",
        SHA_B "-1-1-0"
    );
    make_trash_directory (
        root,
        "cbd",
        SHA_A "-999-9-0"
    );

    gboolean found = FALSE;
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &found,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_no_error (
        error
    );
    g_assert_true (
        found
    );
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
        SHA_A "-999-9-0"
    );

    g_free (
        repository_id
    );
    g_free (
        snapshot_sha
    );
    g_free (
        trash_name
    );
    remove_tree (
        root
    );
}

static void
test_malformed_identity_fails_closed (void)
{
    g_autofree char *root =
        new_root ();

    make_trash_directory (
        root,
        "ewd",
        "not-a-canonical-trash-name"
    );

    gboolean found = FALSE;
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &found,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_error (
        error,
        G_FILE_ERROR,
        G_FILE_ERROR_INVAL
    );

    g_clear_error (
        &error
    );
    g_free (
        repository_id
    );
    g_free (
        snapshot_sha
    );
    g_free (
        trash_name
    );
    remove_tree (
        root
    );
}

static void
test_symlink_object_fails_closed (void)
{
    g_autofree char *root =
        new_root ();
    g_autofree char *repo_dir =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            "ewd",
            NULL
        );
    g_autofree char *target =
        g_build_filename (
            root,
            "target",
            NULL
        );
    g_autofree char *link_path =
        g_build_filename (
            repo_dir,
            SHA_A "-1-1-0",
            NULL
        );

    g_assert_cmpint (
        g_mkdir_with_parents (
            repo_dir,
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
            link_path
        ),
        ==,
        0
    );

    gboolean found = FALSE;
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &found,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_error (
        error,
        G_FILE_ERROR,
        G_FILE_ERROR_INVAL
    );

    g_clear_error (
        &error
    );
    g_free (
        repository_id
    );
    g_free (
        snapshot_sha
    );
    g_free (
        trash_name
    );
    remove_tree (
        root
    );
}

static void
test_unknown_repository_namespace_fails_closed (void)
{
    g_autofree char *root =
        new_root ();
    g_autofree char *unknown =
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

    gboolean found = FALSE;
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &found,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_error (
        error,
        G_FILE_ERROR,
        G_FILE_ERROR_INVAL
    );

    g_clear_error (
        &error
    );
    g_free (
        repository_id
    );
    g_free (
        snapshot_sha
    );
    g_free (
        trash_name
    );
    remove_tree (
        root
    );
}

static void
test_symlink_trash_root_fails_closed (void)
{
    g_autofree char *root =
        new_root ();
    g_autofree char *target =
        g_build_filename (
            root,
            "real-trash",
            NULL
        );
    g_autofree char *trash_link =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            NULL
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
            trash_link
        ),
        ==,
        0
    );

    gboolean found = FALSE;
    char *repository_id = NULL;
    char *snapshot_sha = NULL;
    char *trash_name = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_select_canonical_trash_candidate (
            root,
            &found,
            &repository_id,
            &snapshot_sha,
            &trash_name,
            &error
        )
    );
    g_assert_error (
        error,
        G_FILE_ERROR,
        G_FILE_ERROR_INVAL
    );

    g_clear_error (
        &error
    );
    g_free (
        repository_id
    );
    g_free (
        snapshot_sha
    );
    g_free (
        trash_name
    );
    remove_tree (
        root
    );
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
        "/repository-gc-trash-discovery/missing-trash-empty",
        test_missing_trash_is_empty
    );
    g_test_add_func (
        "/repository-gc-trash-discovery/repository-then-basename",
        test_repository_then_basename_selection
    );
    g_test_add_func (
        "/repository-gc-trash-discovery/malformed-identity-fail-closed",
        test_malformed_identity_fails_closed
    );
    g_test_add_func (
        "/repository-gc-trash-discovery/symlink-object-fail-closed",
        test_symlink_object_fails_closed
    );
    g_test_add_func (
        "/repository-gc-trash-discovery/unknown-repository-fail-closed",
        test_unknown_repository_namespace_fails_closed
    );
    g_test_add_func (
        "/repository-gc-trash-discovery/symlink-trash-root-fail-closed",
        test_symlink_trash_root_fails_closed
    );

    return g_test_run ();
}
