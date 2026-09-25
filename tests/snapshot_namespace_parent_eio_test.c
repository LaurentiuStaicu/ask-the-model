#define _GNU_SOURCE

#include "snapshot_namespace_durability.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static guint fsync_calls = 0;

int
atm_m11b_test_fsync (
    int fd
)
{
    (void) fd;
    fsync_calls++;

    if (fsync_calls == 4) {
        errno = EIO;
        return -1;
    }

    return 0;
}

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

int
main (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-m11b-parent-eio-XXXXXX",
        &error
    );

    if (root == NULL) {
        g_printerr (
            "Could not create M11b parent-EIO root: %s\n",
            error != NULL
                ? error->message
                : "unknown"
        );
        g_clear_error (&error);
        return 2;
    }

    AtmSnapshotNamespaceStats stats = { 0 };
    gboolean ok =
        atm_snapshot_namespace_prepare_final_parent (
            root,
            "ewd",
            &stats,
            &error
        );

    gboolean error_context_match =
        error != NULL &&
        g_strrstr (
            error->message,
            "Could not fsync snapshot namespace parent directory"
        ) != NULL;
    gboolean error_is_eio =
        error != NULL &&
        error->domain == G_FILE_ERROR &&
        error->code == G_FILE_ERROR_IO;
    gboolean qualified =
        !ok &&
        fsync_calls == 4 &&
        error_context_match &&
        error_is_eio &&
        stats.directories_created == 2 &&
        stats.directory_fsync_calls == 3;

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"probe_id\":\"atm-a1-m11b-namespace-parent-fsync-eio-v1\","
        "\"injection\":\"fsync-return-eio-on-call-4\","
        "\"fsync_calls\":%u,"
        "\"directories_created_before_failure\":%" G_GUINT64_FORMAT ","
        "\"successful_fsync_calls_before_failure\":%" G_GUINT64_FORMAT ","
        "\"error_is_eio\":%s,"
        "\"error_context_match\":%s,"
        "\"qualified\":%s"
        "}\n",
        fsync_calls,
        stats.directories_created,
        stats.directory_fsync_calls,
        error_is_eio ? "true" : "false",
        error_context_match ? "true" : "false",
        qualified ? "true" : "false"
    );

    g_clear_error (&error);
    remove_tree (root);
    g_free (root);

    return qualified ? 0 : 1;
}
