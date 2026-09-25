#include "repository_gc_isolation.h"
#include "fault_injection_support.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECKPOINT_FD 3
#define CONTROL_FD 4

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

static gboolean
path_is_real_directory (
    const char *path
)
{
    GStatBuf st;

    return g_lstat (
            path,
            &st
        ) == 0 &&
        S_ISDIR (st.st_mode) &&
        !S_ISLNK (st.st_mode);
}

static char *
source_path_for (
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

static gboolean
create_fixture (
    const char *root,
    GError **error
)
{
    char *source =
        source_path_for (
            root
        );
    char *payload =
        g_build_filename (
            source,
            "STATUS.md",
            NULL
        );

    gboolean ok =
        g_mkdir_with_parents (
            source,
            0700
        ) == 0 &&
        g_file_set_contents (
            payload,
            "# gc isolate replay fixture\n",
            -1,
            error
        );

    if (!ok && error != NULL &&
        *error == NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create GC isolation replay fixture."
        );
    }

    g_free (payload);
    g_free (source);
    return ok;
}

static gboolean
run_isolation (
    const char *root,
    const char *checkpoint,
    GError **error
)
{
    if (!create_fixture (
            root,
            error
        )) {
        return FALSE;
    }

    atm_test_fault_configure (
        checkpoint,
        CHECKPOINT_FD,
        CONTROL_FD
    );

    char *trash_path = NULL;
    gboolean ok =
        atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &trash_path,
            NULL,
            error
        );

    g_free (trash_path);
    return ok;
}

static gboolean
verify_state (
    const char *root,
    GError **error
)
{
    char *source =
        source_path_for (
            root
        );
    char *trash_repo =
        g_build_filename (
            root,
            "Repositories",
            ".trash",
            "ewd",
            NULL
        );

    gboolean source_present =
        path_is_real_directory (
            source
        );
    guint isolated_count = 0;
    gboolean isolated_payload_valid = FALSE;

    GDir *directory =
        g_dir_open (
            trash_repo,
            0,
            NULL
        );

    if (directory != NULL) {
        const char *name;

        while ((name =
                    g_dir_read_name (
                        directory
                    )) != NULL) {
            if (!g_str_has_prefix (
                    name,
                    TEST_SHA
                ) ||
                name[strlen (TEST_SHA)] != '-') {
                continue;
            }

            char *entry =
                g_build_filename (
                    trash_repo,
                    name,
                    NULL
                );

            if (path_is_real_directory (
                    entry
                )) {
                isolated_count++;

                char *payload =
                    g_build_filename (
                        entry,
                        "STATUS.md",
                        NULL
                    );
                char *contents = NULL;

                if (g_file_get_contents (
                        payload,
                        &contents,
                        NULL,
                        NULL
                    ) &&
                    g_strcmp0 (
                        contents,
                        "# gc isolate replay fixture\n"
                    ) == 0) {
                    isolated_payload_valid = TRUE;
                }

                g_free (contents);
                g_free (payload);
            }

            g_free (entry);
        }

        g_dir_close (directory);
    }

    const char *classification;

    if (source_present &&
        isolated_count == 0) {
        classification = "SOURCE_PRESENT";
    } else if (!source_present &&
               isolated_count == 1 &&
               isolated_payload_valid) {
        classification = "TRASH_ISOLATED";
    } else {
        classification = "INVALID";
    }

    g_print ("%s\n", classification);

    g_free (trash_repo);
    g_free (source);

    if (g_strcmp0 (
            classification,
            "INVALID"
        ) == 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "GC isolation replay observed an invalid namespace state."
        );
        return FALSE;
    }

    return TRUE;
}

static int
report_error (
    GError *error
)
{
    g_printerr (
        "gc-isolation-fault-helper: %s\n",
        error != NULL
            ? error->message
            : "unknown error"
    );
    g_clear_error (&error);
    return 2;
}

int
main (
    int argc,
    char **argv
)
{
    GError *error = NULL;
    gboolean ok = FALSE;

    if (argc == 4 &&
        g_strcmp0 (
            argv[1],
            "--isolate"
        ) == 0) {
        ok = run_isolation (
            argv[2],
            argv[3],
            &error
        );
    } else if (argc == 3 &&
               g_strcmp0 (
                   argv[1],
                   "--verify"
               ) == 0) {
        ok = verify_state (
            argv[2],
            &error
        );
    } else {
        g_printerr (
            "Usage: gc-isolation-fault-helper "
            "--isolate ROOT CHECKPOINT | "
            "--verify ROOT\n"
        );
        return 64;
    }

    if (!ok) {
        return report_error (
            error
        );
    }

    g_clear_error (&error);
    return 0;
}
