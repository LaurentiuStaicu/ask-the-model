#define _GNU_SOURCE

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECKPOINT_TIMEOUT_MS 5000

typedef struct {
    GSubprocess *process;
    int checkpoint_read_fd;
    int control_write_fd;
} FaultChild;

static char *helper_path = NULL;

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
    char *root =
        g_dir_make_tmp (
            "atm-c1-i5-fault-XXXXXX",
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static gboolean
write_one_byte (
    int fd,
    char value
)
{
    for (;;) {
        ssize_t result =
            write (
                fd,
                &value,
                1
            );

        if (result < 0 &&
            errno == EINTR) {
            continue;
        }

        return result == 1;
    }
}

static char *
read_checkpoint_line (
    int fd
)
{
    GString *line =
        g_string_new (NULL);

    while (line->len < 128) {
        struct pollfd poll_fd = {
            .fd = fd,
            .events = POLLIN | POLLHUP
        };
        int poll_result;

        do {
            poll_result =
                poll (
                    &poll_fd,
                    1,
                    CHECKPOINT_TIMEOUT_MS
                );
        } while (
            poll_result < 0 &&
            errno == EINTR
        );

        g_assert_cmpint (
            poll_result,
            >,
            0
        );

        char buffer[64];
        ssize_t count;

        do {
            count =
                read (
                    fd,
                    buffer,
                    sizeof buffer
                );
        } while (
            count < 0 &&
            errno == EINTR
        );

        g_assert_cmpint (
            count,
            >,
            0
        );

        for (ssize_t i = 0;
             i < count;
             i++) {
            if (buffer[i] == '\n') {
                return g_string_free (
                    line,
                    FALSE
                );
            }

            g_string_append_c (
                line,
                buffer[i]
            );
        }
    }

    g_error (
        "GC purge checkpoint token exceeded limit."
    );
    return NULL;
}

static FaultChild
start_child (
    const char *root,
    const char *checkpoint
)
{
    int checkpoint_pipe[2] = {-1, -1};
    int control_pipe[2] = {-1, -1};
    GError *error = NULL;

    g_assert_cmpint (
        pipe (checkpoint_pipe),
        ==,
        0
    );
    g_assert_cmpint (
        pipe (control_pipe),
        ==,
        0
    );

    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new (
            G_SUBPROCESS_FLAGS_NONE
        );

    g_subprocess_launcher_take_fd (
        launcher,
        checkpoint_pipe[1],
        3
    );
    checkpoint_pipe[1] = -1;

    g_subprocess_launcher_take_fd (
        launcher,
        control_pipe[0],
        4
    );
    control_pipe[0] = -1;

    GSubprocess *process =
        g_subprocess_launcher_spawn (
            launcher,
            &error,
            helper_path,
            "--purge",
            root,
            checkpoint,
            NULL
        );

    g_assert_no_error (error);
    g_assert_nonnull (process);

    FaultChild child = {
        .process = process,
        .checkpoint_read_fd =
            checkpoint_pipe[0],
        .control_write_fd =
            control_pipe[1]
    };

    g_object_unref (launcher);
    return child;
}

static void
finish_child (
    FaultChild *child
)
{
    GError *error = NULL;

    g_assert_true (
        write_one_byte (
            child->control_write_fd,
            'C'
        )
    );

    close (
        child->control_write_fd
    );
    child->control_write_fd = -1;

    g_assert_true (
        g_subprocess_wait_check (
            child->process,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    close (
        child->checkpoint_read_fd
    );
    child->checkpoint_read_fd = -1;

    g_clear_object (
        &child->process
    );
}

static void
crash_child (
    FaultChild *child
)
{
    GError *error = NULL;

    g_subprocess_force_exit (
        child->process
    );

    g_assert_true (
        g_subprocess_wait (
            child->process,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        g_subprocess_get_successful (
            child->process
        )
    );

    close (
        child->control_write_fd
    );
    close (
        child->checkpoint_read_fd
    );
    child->control_write_fd = -1;
    child->checkpoint_read_fd = -1;

    g_clear_object (
        &child->process
    );
}

static char *
run_helper_capture (
    const char *verb,
    const char *root
)
{
    GError *error = NULL;
    GSubprocess *process =
        g_subprocess_new (
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE,
            &error,
            helper_path,
            verb,
            root,
            NULL
        );

    g_assert_no_error (error);
    g_assert_nonnull (process);

    char *stdout_text = NULL;
    char *stderr_text = NULL;

    g_assert_true (
        g_subprocess_communicate_utf8 (
            process,
            NULL,
            NULL,
            &stdout_text,
            &stderr_text,
            &error
        )
    );
    g_assert_no_error (error);

    if (!g_subprocess_get_successful (
            process
        )) {
        g_error (
            "GC purge helper %s failed: %s",
            verb,
            stderr_text != NULL
                ? stderr_text
                : "(no stderr)"
        );
    }

    g_free (stderr_text);
    g_object_unref (process);
    return stdout_text;
}

static char *
verify_classification (
    const char *root
)
{
    char *text =
        run_helper_capture (
            "--verify",
            root
        );

    g_strchomp (text);
    return text;
}

static void
resume_and_require_fully_purged (
    const char *root
)
{
    char *resume_output =
        run_helper_capture (
            "--resume",
            root
        );
    g_free (resume_output);

    char *classification =
        verify_classification (
            root
        );

    g_assert_cmpstr (
        classification,
        ==,
        "FULLY_PURGED"
    );

    g_free (classification);
}

static void
run_scenario (
    const char *checkpoint,
    gboolean crash,
    const char *expected,
    gboolean require_resume
)
{
    char *root = new_root ();
    FaultChild child =
        start_child (
            root,
            checkpoint
        );

    char *observed =
        read_checkpoint_line (
            child.checkpoint_read_fd
        );

    g_assert_cmpstr (
        observed,
        ==,
        checkpoint
    );
    g_free (observed);

    if (crash) {
        crash_child (&child);
    } else {
        finish_child (&child);
    }

    char *classification =
        verify_classification (
            root
        );

    g_assert_cmpstr (
        classification,
        ==,
        expected
    );
    g_free (classification);

    if (require_resume) {
        resume_and_require_fully_purged (
            root
        );
    }

    remove_tree (root);
    g_free (root);
}

static void
test_control (void)
{
    run_scenario (
        "gc_purge_after_parent_fsync",
        FALSE,
        "FULLY_PURGED",
        FALSE
    );
}

static void
test_before_first_unlink_crash (void)
{
    run_scenario (
        "gc_purge_before_first_unlink",
        TRUE,
        "TRASH_INTACT",
        TRUE
    );
}

static void
test_after_first_unlink_crash (void)
{
    run_scenario (
        "gc_purge_after_first_unlink",
        TRUE,
        "TRASH_RESIDUAL",
        TRUE
    );
}

static void
test_before_root_rmdir_crash (void)
{
    run_scenario (
        "gc_purge_before_root_rmdir",
        TRUE,
        "TRASH_EMPTY_ROOT",
        TRUE
    );
}

static void
test_after_root_rmdir_crash (void)
{
    run_scenario (
        "gc_purge_after_root_rmdir",
        TRUE,
        "FULLY_PURGED",
        FALSE
    );
}

static void
test_after_parent_fsync_crash (void)
{
    run_scenario (
        "gc_purge_after_parent_fsync",
        TRUE,
        "FULLY_PURGED",
        FALSE
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

    const char *configured =
        g_getenv (
            "ATM_GC_PURGE_FAULT_HELPER"
        );

    g_assert_nonnull (configured);
    g_assert_true (
        g_path_is_absolute (
            configured
        )
    );

    helper_path =
        g_strdup (
            configured
        );

    g_test_add_func (
        "/repository-gc-purge-fault/control",
        test_control
    );
    g_test_add_func (
        "/repository-gc-purge-fault/before-first-unlink-crash",
        test_before_first_unlink_crash
    );
    g_test_add_func (
        "/repository-gc-purge-fault/after-first-unlink-crash",
        test_after_first_unlink_crash
    );
    g_test_add_func (
        "/repository-gc-purge-fault/before-root-rmdir-crash",
        test_before_root_rmdir_crash
    );
    g_test_add_func (
        "/repository-gc-purge-fault/after-root-rmdir-crash",
        test_after_root_rmdir_crash
    );
    g_test_add_func (
        "/repository-gc-purge-fault/after-parent-fsync-crash",
        test_after_parent_fsync_crash
    );

    int result = g_test_run ();

    g_free (helper_path);
    return result;
}
