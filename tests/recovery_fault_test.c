#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <errno.h>
#include <poll.h>
#include <string.h>
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
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (
        path,
        0,
        &error
    );

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (
                    directory
                )) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );

            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static char *
new_test_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-recovery-fault-test-XXXXXX",
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
        ssize_t result = write (
            fd,
            &value,
            1
        );

        if (result < 0 && errno == EINTR) {
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
    GString *line = g_string_new (NULL);

    while (line->len < 128) {
        struct pollfd poll_fd = {
            .fd = fd,
            .events = POLLIN | POLLHUP
        };
        int poll_result;

        do {
            poll_result = poll (
                &poll_fd,
                1,
                CHECKPOINT_TIMEOUT_MS
            );
        } while (
            poll_result < 0 &&
            errno == EINTR
        );

        g_assert_cmpint (poll_result, >, 0);

        char buffer[64];
        ssize_t count;

        do {
            count = read (
                fd,
                buffer,
                sizeof buffer
            );
        } while (
            count < 0 &&
            errno == EINTR
        );

        g_assert_cmpint (count, >, 0);

        for (ssize_t i = 0; i < count; i++) {
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
        "Fault checkpoint token exceeded limit."
    );
    return NULL;
}

static FaultChild
start_fault_child (
    const char *mode,
    const char *root,
    const char *checkpoint
)
{
    int checkpoint_pipe[2] = {-1, -1};
    int control_pipe[2] = {-1, -1};
    GError *error = NULL;
    GSubprocessLauncher *launcher = NULL;
    GSubprocess *process = NULL;
    FaultChild child = {
        .process = NULL,
        .checkpoint_read_fd = -1,
        .control_write_fd = -1
    };

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

    launcher = g_subprocess_launcher_new (
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

    process = g_subprocess_launcher_spawn (
        launcher,
        &error,
        helper_path,
        mode,
        root,
        checkpoint,
        NULL
    );

    g_assert_no_error (error);
    g_assert_nonnull (process);

    child.process = process;
    child.checkpoint_read_fd =
        checkpoint_pipe[0];
    child.control_write_fd =
        control_pipe[1];

    g_object_unref (launcher);
    return child;
}

static void
finish_control_child (
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

    close (child->control_write_fd);
    child->control_write_fd = -1;

    g_assert_true (
        g_subprocess_wait_check (
            child->process,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    close (child->checkpoint_read_fd);
    child->checkpoint_read_fd = -1;

    g_clear_object (&child->process);
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

    close (child->control_write_fd);
    close (child->checkpoint_read_fd);
    child->control_write_fd = -1;
    child->checkpoint_read_fd = -1;

    g_clear_object (&child->process);
}

static char *
verify_classification (
    const char *verify_mode,
    const char *root
)
{
    GError *error = NULL;
    GSubprocess *process = g_subprocess_new (
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &error,
        helper_path,
        verify_mode,
        root,
        NULL
    );
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    JsonParser *parser = NULL;
    JsonNode *root_node;
    JsonObject *object;
    char *classification = NULL;

    g_assert_no_error (error);
    g_assert_nonnull (process);

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

    if (!g_subprocess_get_successful (process)) {
        g_error (
            "Verifier failed: %s",
            stderr_text != NULL
                ? stderr_text
                : "(no stderr)"
        );
    }

    parser = json_parser_new ();

    g_assert_true (
        json_parser_load_from_data (
            parser,
            stdout_text,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    root_node = json_parser_get_root (parser);
    g_assert_true (
        JSON_NODE_HOLDS_OBJECT (
            root_node
        )
    );

    object = json_node_get_object (
        root_node
    );

    g_assert_cmpint (
        json_object_get_int_member (
            object,
            "schema_version"
        ),
        ==,
        1
    );

    classification = g_strdup (
        json_object_get_string_member (
            object,
            "classification"
        )
    );

    g_object_unref (parser);
    g_free (stderr_text);
    g_free (stdout_text);
    g_object_unref (process);

    return classification;
}

static void
run_scenario (
    const char *mode,
    const char *verify_mode,
    const char *checkpoint,
    gboolean crash,
    const char *expected_classification
)
{
    char *root = new_test_root ();
    FaultChild child = start_fault_child (
        mode,
        root,
        checkpoint
    );
    char *observed_checkpoint =
        read_checkpoint_line (
            child.checkpoint_read_fd
        );

    g_assert_cmpstr (
        observed_checkpoint,
        ==,
        checkpoint
    );

    g_free (observed_checkpoint);

    if (crash) {
        crash_child (&child);
    } else {
        finish_control_child (&child);
    }

    char *classification =
        verify_classification (
            verify_mode,
            root
        );

    g_assert_cmpstr (
        classification,
        ==,
        expected_classification
    );

    g_free (classification);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_snapshot_control (void)
{
    run_scenario (
        "--snapshot",
        "--verify-snapshot",
        "snapshot_post_rename",
        FALSE,
        "NEW_AUTHORITY_VALID"
    );
}

static void
test_snapshot_pre_rename_crash (void)
{
    run_scenario (
        "--snapshot",
        "--verify-snapshot",
        "snapshot_pre_rename",
        TRUE,
        "NO_AUTHORITY_VALID"
    );
}

static void
test_snapshot_post_rename_crash (void)
{
    run_scenario (
        "--snapshot",
        "--verify-snapshot",
        "snapshot_post_rename",
        TRUE,
        "RECOVERY_REQUIRED"
    );
}

static void
test_index_control (void)
{
    run_scenario (
        "--index",
        "--verify-index",
        "index_post_rename",
        FALSE,
        "DERIVED_CACHE_RECOVERABLE"
    );
}

static void
test_index_validated_closed_crash (void)
{
    run_scenario (
        "--index",
        "--verify-index",
        "index_validated_closed",
        TRUE,
        "RECOVERY_REQUIRED"
    );
}

static void
test_index_pre_rename_crash (void)
{
    run_scenario (
        "--index",
        "--verify-index",
        "index_pre_rename",
        TRUE,
        "RECOVERY_REQUIRED"
    );
}

static void
test_index_post_rename_crash (void)
{
    run_scenario (
        "--index",
        "--verify-index",
        "index_post_rename",
        TRUE,
        "DERIVED_CACHE_RECOVERABLE"
    );
}

int
main (int argc, char **argv)
{
    const char *configured_helper;

    g_test_init (&argc, &argv, NULL);

    configured_helper = g_getenv (
        "ATM_RECOVERY_FAULT_HELPER"
    );
    g_assert_nonnull (configured_helper);
    g_assert_true (
        g_path_is_absolute (
            configured_helper
        )
    );

    helper_path = g_strdup (
        configured_helper
    );

    g_test_add_func (
        "/recovery/snapshot/control",
        test_snapshot_control
    );
    g_test_add_func (
        "/recovery/snapshot/pre-rename-crash",
        test_snapshot_pre_rename_crash
    );
    g_test_add_func (
        "/recovery/snapshot/post-rename-crash",
        test_snapshot_post_rename_crash
    );
    g_test_add_func (
        "/recovery/index/control",
        test_index_control
    );
    g_test_add_func (
        "/recovery/index/validated-closed-crash",
        test_index_validated_closed_crash
    );
    g_test_add_func (
        "/recovery/index/pre-rename-crash",
        test_index_pre_rename_crash
    );
    g_test_add_func (
        "/recovery/index/post-rename-crash",
        test_index_post_rename_crash
    );

    int result = g_test_run ();

    g_free (helper_path);
    return result;
}
