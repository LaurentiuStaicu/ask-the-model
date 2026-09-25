#include "repository_generation_lease.h"

#include <glib/gstdio.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static char *
new_temp_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-generation-lease-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static char *
lease_root_path (
    const char *root
)
{
    return g_build_filename (
        root,
        "repository-generation-leases",
        NULL
    );
}

static char *
lease_path (
    const char *root,
    gint64 generation_id
)
{
    char *lease_root = lease_root_path (root);
    char *filename = g_strdup_printf (
        "%" G_GINT64_FORMAT ".lock",
        generation_id
    );
    char *path = g_build_filename (
        lease_root,
        filename,
        NULL
    );

    g_free (filename);
    g_free (lease_root);
    return path;
}

static void
remove_root (
    char *root
)
{
    char *lease_root = lease_root_path (root);

    if (g_file_test (
            lease_root,
            G_FILE_TEST_IS_DIR
        )) {
        GError *error = NULL;
        GDir *directory = g_dir_open (
            lease_root,
            0,
            &error
        );

        if (directory != NULL) {
            const char *name;

            while ((name = g_dir_read_name (
                        directory
                    )) != NULL) {
                char *path = g_build_filename (
                    lease_root,
                    name,
                    NULL
                );
                g_unlink (path);
                g_free (path);
            }

            g_dir_close (directory);
        }

        g_clear_error (&error);
        g_rmdir (lease_root);
    }

    g_free (lease_root);
    g_rmdir (root);
    g_free (root);
}

static void
test_two_readers_and_gc_exclusion (void)
{
    char *root = new_temp_root ();
    GError *error = NULL;
    gint reader_a = -1;
    gint reader_b = -1;
    gint exclusive = -1;
    gboolean contended = FALSE;

    g_assert_true (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            7,
            &reader_a,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (reader_a, >=, 0);

    g_assert_true (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            7,
            &reader_b,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (reader_b, >=, 0);

    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            7,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (contended);
    g_assert_cmpint (exclusive, ==, -1);

    atm_repository_generation_lease_release (
        reader_a
    );

    contended = FALSE;
    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            7,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (contended);
    g_assert_cmpint (exclusive, ==, -1);

    atm_repository_generation_lease_release (
        reader_b
    );

    contended = FALSE;
    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            7,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (exclusive, >=, 0);

    atm_repository_generation_lease_release (
        exclusive
    );
    remove_root (root);
}

static void
test_exclusive_blocks_new_reader (void)
{
    char *root = new_temp_root ();
    GError *error = NULL;
    gint exclusive = -1;
    gint reader = -1;
    gboolean contended = FALSE;

    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            8,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (exclusive, >=, 0);

    g_assert_true (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            8,
            &reader,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (contended);
    g_assert_cmpint (reader, ==, -1);

    atm_repository_generation_lease_release (
        exclusive
    );

    contended = FALSE;
    g_assert_true (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            8,
            &reader,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (reader, >=, 0);

    atm_repository_generation_lease_release (
        reader
    );
    remove_root (root);
}

static void
test_reader_crash_releases_lock (void)
{
    char *root = new_temp_root ();
    int ready_pipe[2];
    pid_t child;
    char ready = 0;
    GError *error = NULL;
    gint exclusive = -1;
    gboolean contended = FALSE;

    g_assert_cmpint (pipe (ready_pipe), ==, 0);

    child = fork ();
    g_assert_cmpint (child, >=, 0);

    if (child == 0) {
        gint reader = -1;
        gboolean child_contended = FALSE;
        GError *child_error = NULL;

        close (ready_pipe[0]);

        if (!atm_repository_generation_lease_try_acquire_shared (
                root,
                9,
                &reader,
                &child_contended,
                &child_error
            ) ||
            child_contended ||
            reader < 0) {
            _exit (2);
        }

        ready = '1';
        if (write (
                ready_pipe[1],
                &ready,
                1
            ) != 1) {
            _exit (3);
        }

        pause ();
        _exit (4);
    }

    close (ready_pipe[1]);
    g_assert_cmpint (
        read (
            ready_pipe[0],
            &ready,
            1
        ),
        ==,
        1
    );
    close (ready_pipe[0]);
    g_assert_cmpint (ready, ==, '1');

    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            9,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (contended);
    g_assert_cmpint (exclusive, ==, -1);

    g_assert_cmpint (kill (child, SIGKILL), ==, 0);
    g_assert_cmpint (
        waitpid (child, NULL, 0),
        ==,
        child
    );

    contended = FALSE;
    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            9,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (exclusive, >=, 0);

    atm_repository_generation_lease_release (
        exclusive
    );
    remove_root (root);
}

static void
test_different_generations_are_independent (void)
{
    char *root = new_temp_root ();
    GError *error = NULL;
    gint reader = -1;
    gint exclusive = -1;
    gboolean contended = FALSE;

    g_assert_true (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            10,
            &reader,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (reader, >=, 0);

    g_assert_true (
        atm_repository_generation_lease_try_acquire_exclusive (
            root,
            11,
            &exclusive,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (exclusive, >=, 0);

    atm_repository_generation_lease_release (
        exclusive
    );
    atm_repository_generation_lease_release (
        reader
    );
    remove_root (root);
}

static void
test_zero_generation_creates_no_lock_io (void)
{
    char *root = new_temp_root ();
    char *lease_root = lease_root_path (root);
    GError *error = NULL;
    gint fd = -1;
    gboolean contended = FALSE;

    g_assert_false (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            0,
            &fd,
            &contended,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_REPOSITORY_GENERATION_LEASE_ERROR,
        ATM_REPOSITORY_GENERATION_LEASE_ERROR_INVALID_GENERATION
    );
    g_clear_error (&error);
    g_assert_cmpint (fd, ==, -1);
    g_assert_false (contended);
    g_assert_false (
        g_file_test (
            lease_root,
            G_FILE_TEST_EXISTS
        )
    );

    g_free (lease_root);
    remove_root (root);
}

static void
test_lock_files_persist_after_release (void)
{
    char *root = new_temp_root ();
    char *path = lease_path (root, 12);
    GError *error = NULL;
    gint fd = -1;
    gboolean contended = FALSE;

    g_assert_true (
        atm_repository_generation_lease_try_acquire_shared (
            root,
            12,
            &fd,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (fd, >=, 0);

    atm_repository_generation_lease_release (
        fd
    );

    g_assert_true (
        g_file_test (
            path,
            G_FILE_TEST_IS_REGULAR
        )
    );

    g_free (path);
    remove_root (root);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/repository-generation-lease/two-readers-gc-exclusion",
        test_two_readers_and_gc_exclusion
    );
    g_test_add_func (
        "/repository-generation-lease/exclusive-blocks-reader",
        test_exclusive_blocks_new_reader
    );
    g_test_add_func (
        "/repository-generation-lease/crash-release",
        test_reader_crash_releases_lock
    );
    g_test_add_func (
        "/repository-generation-lease/different-generations",
        test_different_generations_are_independent
    );
    g_test_add_func (
        "/repository-generation-lease/zero-no-io",
        test_zero_generation_creates_no_lock_io
    );
    g_test_add_func (
        "/repository-generation-lease/persistent-lock-file",
        test_lock_files_persist_after_release
    );

    return g_test_run ();
}
