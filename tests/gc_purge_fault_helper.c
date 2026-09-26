#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define _GNU_SOURCE

#include "repository_gc_isolation.h"
#include "repository_gc_purge.h"
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

    return g_lstat (path, &st) == 0 &&
        S_ISDIR (st.st_mode) &&
        !S_ISLNK (st.st_mode);
}

static char *
snapshot_path_for (
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

static char *
trash_repo_for (
    const char *root
)
{
    return g_build_filename (
        root,
        "Repositories",
        ".trash",
        "ewd",
        NULL
    );
}

static gboolean
create_snapshot_fixture (
    const char *root,
    GError **error
)
{
    char *source = snapshot_path_for (root);
    char *nested = g_build_filename (
        source,
        "nested",
        NULL
    );
    char *file_a = g_build_filename (
        source,
        "A.txt",
        NULL
    );
    char *file_b = g_build_filename (
        nested,
        "B.txt",
        NULL
    );

    gboolean ok =
        g_mkdir_with_parents (
            nested,
            0700
        ) == 0 &&
        g_file_set_contents (
            file_a,
            "A\n",
            -1,
            error
        ) &&
        g_file_set_contents (
            file_b,
            "B\n",
            -1,
            error
        );

    if (!ok && error != NULL &&
        *error == NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create GC purge replay fixture."
        );
    }

    g_free (file_b);
    g_free (file_a);
    g_free (nested);
    g_free (source);
    return ok;
}

static gboolean
find_trash_entry (
    const char *root,
    char **out_name,
    char **out_path,
    GError **error
)
{
    char *trash_repo = trash_repo_for (root);
    GDir *directory =
        g_dir_open (
            trash_repo,
            0,
            NULL
        );

    if (directory == NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_NOENT,
            "GC purge replay trash repository is missing."
        );
        g_free (trash_repo);
        return FALSE;
    }

    char *found_name = NULL;
    char *found_path = NULL;
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

        if (!path_is_real_directory (
                entry
            )) {
            g_free (entry);
            continue;
        }

        if (found_name != NULL) {
            g_free (entry);
            g_free (found_path);
            g_free (found_name);
            g_dir_close (directory);
            g_free (trash_repo);
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "GC purge replay found multiple isolated entries."
            );
            return FALSE;
        }

        found_name = g_strdup (name);
        found_path = entry;
    }

    g_dir_close (directory);
    g_free (trash_repo);

    if (found_name == NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_NOENT,
            "GC purge replay found no isolated entry."
        );
        return FALSE;
    }

    *out_name = found_name;
    *out_path = found_path;
    return TRUE;
}

static gboolean
run_purge (
    const char *root,
    const char *checkpoint,
    GError **error
)
{
    if (!create_snapshot_fixture (
            root,
            error
        )) {
        return FALSE;
    }

    char *trash_path = NULL;

    if (!atm_repository_gc_isolate_snapshot_to_trash (
            root,
            "ewd",
            TEST_SHA,
            &trash_path,
            NULL,
            error
        )) {
        g_free (trash_path);
        return FALSE;
    }

    char *trash_name =
        g_path_get_basename (
            trash_path
        );

    atm_test_fault_configure (
        checkpoint,
        CHECKPOINT_FD,
        CONTROL_FD
    );

    gboolean ok =
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            trash_name,
            NULL,
            error
        );

    g_free (trash_name);
    g_free (trash_path);
    return ok;
}

static gboolean
resume_purge (
    const char *root,
    GError **error
)
{
    char *trash_name = NULL;
    char *trash_path = NULL;

    if (!find_trash_entry (
            root,
            &trash_name,
            &trash_path,
            error
        )) {
        return FALSE;
    }

    gboolean ok =
        atm_repository_gc_purge_trash_entry (
            root,
            "ewd",
            trash_name,
            NULL,
            error
        );

    g_free (trash_path);
    g_free (trash_name);
    return ok;
}

static gboolean
inspect_tree (
    const char *path,
    guint *regular_files,
    guint *directories,
    gboolean *valid
)
{
    GDir *directory =
        g_dir_open (
            path,
            0,
            NULL
        );

    if (directory == NULL) {
        *valid = FALSE;
        return FALSE;
    }

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
        GStatBuf st;

        if (g_lstat (
                child,
                &st
            ) != 0) {
            *valid = FALSE;
            g_free (child);
            break;
        }

        if (S_ISREG (
                st.st_mode
            )) {
            (*regular_files)++;
        } else if (S_ISDIR (
                       st.st_mode
                   ) &&
                   !S_ISLNK (
                       st.st_mode
                   )) {
            (*directories)++;

            if (!inspect_tree (
                    child,
                    regular_files,
                    directories,
                    valid
                )) {
                g_free (child);
                break;
            }
        } else {
            *valid = FALSE;
            g_free (child);
            break;
        }

        g_free (child);
    }

    g_dir_close (directory);
    return *valid;
}

static gboolean
verify_state (
    const char *root,
    GError **error
)
{
    char *source =
        snapshot_path_for (
            root
        );
    char *trash_repo =
        trash_repo_for (
            root
        );

    if (path_is_real_directory (
            source
        )) {
        g_free (trash_repo);
        g_free (source);
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "GC purge replay unexpectedly restored the normal snapshot source."
        );
        return FALSE;
    }

    guint isolated_count = 0;
    char *isolated_path = NULL;
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
                g_free (isolated_path);
                isolated_path = entry;
            } else {
                g_free (entry);
            }
        }

        g_dir_close (directory);
    }

    const char *classification = "INVALID";

    if (isolated_count == 0) {
        classification = "FULLY_PURGED";
    } else if (isolated_count == 1) {
        guint regular_files = 0;
        guint directories = 0;
        gboolean valid = TRUE;

        inspect_tree (
            isolated_path,
            &regular_files,
            &directories,
            &valid
        );

        if (!valid) {
            classification = "INVALID";
        } else if (regular_files == 2 &&
                   directories == 1) {
            classification = "TRASH_INTACT";
        } else if (regular_files == 0 &&
                   directories == 0) {
            classification = "TRASH_EMPTY_ROOT";
        } else {
            classification = "TRASH_RESIDUAL";
        }
    }

    g_print ("%s\n", classification);

    g_free (isolated_path);
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
            "GC purge replay observed an invalid namespace state."
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
        "gc-purge-fault-helper: %s\n",
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
            "--purge"
        ) == 0) {
        ok = run_purge (
            argv[2],
            argv[3],
            &error
        );
    } else if (argc == 3 &&
               g_strcmp0 (
                   argv[1],
                   "--resume"
               ) == 0) {
        ok = resume_purge (
            argv[2],
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
            "Usage: gc-purge-fault-helper "
            "--purge ROOT CHECKPOINT | "
            "--resume ROOT | "
            "--verify ROOT\n"
        );
        return 64;
    }

    if (!ok) {
        return report_error (error);
    }

    g_clear_error (&error);
    return 0;
}
