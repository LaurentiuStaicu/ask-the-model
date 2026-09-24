#include "repository_mutation_lease.h"

#include <glib/gstdio.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static char *
new_temp_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-repository-mutation-lease-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
remove_root (char *root)
{
    char *lock_path = g_build_filename (
        root,
        "repository-mutation.lock",
        NULL
    );

    if (g_file_test (lock_path, G_FILE_TEST_IS_DIR)) {
        g_rmdir (lock_path);
    } else {
        g_unlink (lock_path);
    }

    g_free (lock_path);
    g_rmdir (root);
    g_free (root);
}

static void
test_acquire_contention_release (void)
{
    char *root = new_temp_root ();
    GError *error = NULL;
    gint first_fd = -1;
    gint second_fd = -1;
    gboolean contended = FALSE;

    g_assert_true (
        atm_repository_mutation_lease_try_acquire (
            root,
            &first_fd,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (first_fd, >=, 0);
    g_assert_false (contended);

    g_assert_true (
        atm_repository_mutation_lease_try_acquire (
            root,
            &second_fd,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (second_fd, ==, -1);
    g_assert_true (contended);

    atm_repository_mutation_lease_release (first_fd);

    contended = FALSE;
    g_assert_true (
        atm_repository_mutation_lease_try_acquire (
            root,
            &second_fd,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (second_fd, >=, 0);
    g_assert_false (contended);

    atm_repository_mutation_lease_release (second_fd);
    remove_root (root);
}

static void
test_holder_crash_releases_lock (void)
{
    char *root = new_temp_root ();
    int ready_pipe[2];
    pid_t child;
    char ready = 0;
    GError *error = NULL;
    gint parent_fd = -1;
    gboolean contended = FALSE;

    g_assert_cmpint (pipe (ready_pipe), ==, 0);

    child = fork ();
    g_assert_cmpint (child, >=, 0);

    if (child == 0) {
        gint lease_fd = -1;
        gboolean child_contended = FALSE;
        GError *child_error = NULL;

        close (ready_pipe[0]);

        if (!atm_repository_mutation_lease_try_acquire (
                root,
                &lease_fd,
                &child_contended,
                &child_error
            ) ||
            child_contended ||
            lease_fd < 0) {
            _exit (2);
        }

        ready = '1';
        if (write (ready_pipe[1], &ready, 1) != 1) {
            _exit (3);
        }

        pause ();
        _exit (4);
    }

    close (ready_pipe[1]);
    g_assert_cmpint (read (ready_pipe[0], &ready, 1), ==, 1);
    close (ready_pipe[0]);
    g_assert_cmpint (ready, ==, '1');

    g_assert_true (
        atm_repository_mutation_lease_try_acquire (
            root,
            &parent_fd,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (parent_fd, ==, -1);
    g_assert_true (contended);

    g_assert_cmpint (kill (child, SIGKILL), ==, 0);
    g_assert_cmpint (waitpid (child, NULL, 0), ==, child);

    contended = FALSE;
    g_assert_true (
        atm_repository_mutation_lease_try_acquire (
            root,
            &parent_fd,
            &contended,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (parent_fd, >=, 0);
    g_assert_false (contended);

    atm_repository_mutation_lease_release (parent_fd);
    remove_root (root);
}

static void
test_rejects_symlink_lock_path (void)
{
    char *root = new_temp_root ();
    char *target = g_build_filename (
        root,
        "target",
        NULL
    );
    char *lock_path = g_build_filename (
        root,
        "repository-mutation.lock",
        NULL
    );
    GError *error = NULL;
    gint fd = -1;
    gboolean contended = FALSE;

    g_assert_true (
        g_file_set_contents (
            target,
            "not a lock",
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (symlink (target, lock_path), ==, 0);

    g_assert_false (
        atm_repository_mutation_lease_try_acquire (
            root,
            &fd,
            &contended,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_REPOSITORY_MUTATION_LEASE_ERROR,
        ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO
    );
    g_clear_error (&error);
    g_assert_cmpint (fd, ==, -1);
    g_assert_false (contended);

    g_unlink (lock_path);
    g_unlink (target);
    g_free (lock_path);
    g_free (target);
    remove_root (root);
}

static void
test_rejects_directory_lock_path (void)
{
    char *root = new_temp_root ();
    char *lock_path = g_build_filename (
        root,
        "repository-mutation.lock",
        NULL
    );
    GError *error = NULL;
    gint fd = -1;
    gboolean contended = FALSE;

    g_assert_cmpint (g_mkdir (lock_path, 0700), ==, 0);

    g_assert_false (
        atm_repository_mutation_lease_try_acquire (
            root,
            &fd,
            &contended,
            &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_cmpint (fd, ==, -1);
    g_assert_false (contended);

    g_rmdir (lock_path);
    g_free (lock_path);
    remove_root (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/repository-mutation-lease/acquire-contention-release",
        test_acquire_contention_release
    );
    g_test_add_func (
        "/repository-mutation-lease/holder-crash",
        test_holder_crash_releases_lock
    );
    g_test_add_func (
        "/repository-mutation-lease/reject-symlink",
        test_rejects_symlink_lock_path
    );
    g_test_add_func (
        "/repository-mutation-lease/reject-directory",
        test_rejects_directory_lock_path
    );

    return g_test_run ();
}
