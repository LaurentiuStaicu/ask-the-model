#define _GNU_SOURCE

#include "repository_gc_purge.h"
#include "repository_gc_trash_scan.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *SHA_A =
    "1111111111111111111111111111111111111111";
static const char *SHA_B =
    "2222222222222222222222222222222222222222";

static char *
new_temp_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-c1-i8-trash-scan-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
remove_tree (
    const char *path
)
{
    struct stat st;

    if (lstat (path, &st) != 0) {
        return;
    }

    if (!S_ISDIR (st.st_mode) ||
        S_ISLNK (st.st_mode)) {
        g_remove (path);
        return;
    }

    GDir *dir = g_dir_open (
        path,
        0,
        NULL
    );

    if (dir != NULL) {
        const char *name;

        while ((name = g_dir_read_name (dir)) != NULL) {
            char *child =
                g_build_filename (
                    path,
                    name,
                    NULL
                );
            remove_tree (child);
            g_free (child);
        }

        g_dir_close (dir);
    }

    g_rmdir (path);
}

static char *
trash_repo_path (
    const char *root,
    const char *repository_id
)
{
    return g_build_filename (
        root,
        "Repositories",
        ".trash",
        repository_id,
        NULL
    );
}

static char *
canonical_name (
    const char *sha,
    guint64 timestamp,
    guint pid,
    guint attempt
)
{
    return g_strdup_printf (
        "%s-%" G_GUINT64_FORMAT "-%u-%u",
        sha,
        timestamp,
        pid,
        attempt
    );
}

static void
test_missing_trash_is_empty_and_read_only (void)
{
    char *root = new_temp_root ();
    char *trash =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            NULL
        );

    AtmRepositoryGcTrashScan *scan = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_trash_scan (
            root,
            "cbd",
            &scan,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (scan);
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_candidate_count (
            scan
        ),
        ==,
        0
    );
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_diagnostic_count (
            scan
        ),
        ==,
        0
    );
    g_assert_false (
        g_file_test (
            trash,
            G_FILE_TEST_EXISTS
        )
    );

    atm_repository_gc_trash_scan_free (scan);
    g_free (trash);
    remove_tree (root);
    g_free (root);
}

static void
test_canonical_candidates_sorted_and_sha_extracted (void)
{
    char *root = new_temp_root ();
    char *repo = trash_repo_path (
        root,
        "cbd"
    );
    g_assert_cmpint (
        g_mkdir_with_parents (
            repo,
            0700
        ),
        ==,
        0
    );

    char *later =
        canonical_name (
            SHA_B,
            200,
            22,
            1
        );
    char *earlier =
        canonical_name (
            SHA_A,
            100,
            11,
            0
        );
    char *later_path =
        g_build_filename (
            repo,
            later,
            NULL
        );
    char *earlier_path =
        g_build_filename (
            repo,
            earlier,
            NULL
        );

    g_assert_cmpint (
        g_mkdir (
            later_path,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir (
            earlier_path,
            0700
        ),
        ==,
        0
    );

    AtmRepositoryGcTrashScan *scan = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_trash_scan (
            root,
            "cbd",
            &scan,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_candidate_count (
            scan
        ),
        ==,
        2
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_candidate_name (
            scan,
            0
        ),
        ==,
        earlier
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_candidate_sha (
            scan,
            0
        ),
        ==,
        SHA_A
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_candidate_name (
            scan,
            1
        ),
        ==,
        later
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_candidate_sha (
            scan,
            1
        ),
        ==,
        SHA_B
    );

    atm_repository_gc_trash_scan_free (scan);
    g_free (later_path);
    g_free (earlier_path);
    g_free (later);
    g_free (earlier);
    g_free (repo);
    remove_tree (root);
    g_free (root);
}

static void
test_malformed_identity_is_diagnostic (void)
{
    char *root = new_temp_root ();
    char *repo = trash_repo_path (
        root,
        "rmd"
    );
    g_assert_cmpint (
        g_mkdir_with_parents (
            repo,
            0700
        ),
        ==,
        0
    );

    char *bad =
        g_strdup_printf (
            "%s-0-1-0",
            SHA_A
        );
    char *bad_path =
        g_build_filename (
            repo,
            bad,
            NULL
        );
    g_assert_cmpint (
        g_mkdir (
            bad_path,
            0700
        ),
        ==,
        0
    );

    AtmRepositoryGcTrashScan *scan = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_trash_scan (
            root,
            "rmd",
            &scan,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_candidate_count (
            scan
        ),
        ==,
        0
    );
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_diagnostic_count (
            scan
        ),
        ==,
        1
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_diagnostic_name (
            scan,
            0
        ),
        ==,
        bad
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_diagnostic_reason (
            scan,
            0
        ),
        ==,
        "malformed-trash-name"
    );

    atm_repository_gc_trash_scan_free (scan);
    g_free (bad_path);
    g_free (bad);
    g_free (repo);
    remove_tree (root);
    g_free (root);
}

static void
test_symlink_entry_is_repair_diagnostic (void)
{
    char *root = new_temp_root ();
    char *repo = trash_repo_path (
        root,
        "ewd"
    );
    g_assert_cmpint (
        g_mkdir_with_parents (
            repo,
            0700
        ),
        ==,
        0
    );

    char *target =
        g_build_filename (
            root,
            "target",
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

    char *name =
        canonical_name (
            SHA_A,
            100,
            1,
            0
        );
    char *link_path =
        g_build_filename (
            repo,
            name,
            NULL
        );
    g_assert_cmpint (
        symlink (
            target,
            link_path
        ),
        ==,
        0
    );

    AtmRepositoryGcTrashScan *scan = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_gc_trash_scan (
            root,
            "ewd",
            &scan,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_candidate_count (
            scan
        ),
        ==,
        0
    );
    g_assert_cmpuint (
        atm_repository_gc_trash_scan_diagnostic_count (
            scan
        ),
        ==,
        1
    );
    g_assert_cmpstr (
        atm_repository_gc_trash_scan_diagnostic_reason (
            scan,
            0
        ),
        ==,
        "not-real-directory"
    );

    atm_repository_gc_trash_scan_free (scan);
    g_free (link_path);
    g_free (name);
    g_free (target);
    g_free (repo);
    remove_tree (root);
    g_free (root);
}

static void
test_symlink_trash_root_fails_closed (void)
{
    char *root = new_temp_root ();
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

    AtmRepositoryGcTrashScan *scan = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_trash_scan (
            root,
            "cbd",
            &scan,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (scan);

    g_clear_error (&error);
    g_free (trash);
    g_free (target);
    g_free (repositories);
    remove_tree (root);
    g_free (root);
}

static void
test_identity_predicate_matches_i4_grammar (void)
{
    char *valid =
        canonical_name (
            SHA_A,
            1,
            1,
            99
        );
    char *attempt_too_large =
        canonical_name (
            SHA_A,
            1,
            1,
            100
        );

    g_assert_true (
        atm_repository_gc_trash_name_is_canonical (
            valid
        )
    );
    g_assert_false (
        atm_repository_gc_trash_name_is_canonical (
            attempt_too_large
        )
    );
    g_assert_false (
        atm_repository_gc_trash_name_is_canonical (
            "not-a-trash-name"
        )
    );

    g_free (attempt_too_large);
    g_free (valid);
}

static void
test_unknown_repository_is_rejected (void)
{
    char *root = new_temp_root ();
    AtmRepositoryGcTrashScan *scan = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_gc_trash_scan (
            root,
            "unknown",
            &scan,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (scan);

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
        "/repository-gc-trash-scan/missing-read-only",
        test_missing_trash_is_empty_and_read_only
    );
    g_test_add_func (
        "/repository-gc-trash-scan/canonical-sorted",
        test_canonical_candidates_sorted_and_sha_extracted
    );
    g_test_add_func (
        "/repository-gc-trash-scan/malformed-diagnostic",
        test_malformed_identity_is_diagnostic
    );
    g_test_add_func (
        "/repository-gc-trash-scan/symlink-entry",
        test_symlink_entry_is_repair_diagnostic
    );
    g_test_add_func (
        "/repository-gc-trash-scan/symlink-trash-root",
        test_symlink_trash_root_fails_closed
    );
    g_test_add_func (
        "/repository-gc-trash-scan/identity-grammar",
        test_identity_predicate_matches_i4_grammar
    );
    g_test_add_func (
        "/repository-gc-trash-scan/unknown-repository",
        test_unknown_repository_is_rejected
    );

    return g_test_run ();
}
