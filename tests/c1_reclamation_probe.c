#define _GNU_SOURCE

#include "capacity_measurement.h"
#include "repository_gc_purge.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TRASH_SHA "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define ACTIVE_SHA "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define TRASH_NAME TRASH_SHA "-1-1-0"

static gboolean
write_allocated_file (
    const char *path,
    gsize bytes,
    guint8 fill,
    GError **error
)
{
    int fd = g_open (
        path,
        O_CREAT |
        O_EXCL |
        O_WRONLY |
        O_CLOEXEC,
        0600
    );

    if (fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create reclamation fixture file '%s': %s",
            path,
            g_strerror (errno)
        );
        return FALSE;
    }

    guint8 buffer[65536];
    memset (
        buffer,
        fill,
        sizeof buffer
    );

    gsize remaining = bytes;

    while (remaining > 0) {
        gsize requested =
            MIN (
                remaining,
                sizeof buffer
            );
        ssize_t written =
            write (
                fd,
                buffer,
                requested
            );

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not write reclamation fixture file '%s': %s",
                path,
                g_strerror (errno)
            );
            close (fd);
            return FALSE;
        }

        if (written == 0) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "Reclamation fixture write made no progress."
            );
            close (fd);
            return FALSE;
        }

        remaining -=
            (gsize) written;
    }

    while (fsync (fd) != 0) {
        if (errno == EINTR) {
            continue;
        }

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not fsync reclamation fixture file '%s': %s",
            path,
            g_strerror (errno)
        );
        close (fd);
        return FALSE;
    }

    if (close (fd) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not close reclamation fixture file '%s': %s",
            path,
            g_strerror (errno)
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
fsync_directory (
    const char *path,
    GError **error
)
{
    int fd = g_open (
        path,
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC,
        0
    );

    if (fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open reclamation fixture directory '%s': %s",
            path,
            g_strerror (errno)
        );
        return FALSE;
    }

    while (fsync (fd) != 0) {
        if (errno == EINTR) {
            continue;
        }

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not fsync reclamation fixture directory '%s': %s",
            path,
            g_strerror (errno)
        );
        close (fd);
        return FALSE;
    }

    if (close (fd) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not close reclamation fixture directory '%s': %s",
            path,
            g_strerror (errno)
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
ensure_directory (
    const char *path,
    GError **error
)
{
    if (g_mkdir_with_parents (
            path,
            0700
        ) == 0) {
        return TRUE;
    }

    g_set_error (
        error,
        G_FILE_ERROR,
        g_file_error_from_errno (errno),
        "Could not create reclamation fixture directory '%s': %s",
        path,
        g_strerror (errno)
    );
    return FALSE;
}

int
main (
    int argc,
    char **argv
)
{
    if (argc != 3) {
        g_printerr (
            "usage: %s DATA_ROOT FILESYSTEM_LABEL\n",
            argv[0]
        );
        return 64;
    }

    const char *data_root = argv[1];
    const char *filesystem_label = argv[2];
    GError *error = NULL;

    g_autofree char *repositories =
        g_build_filename (
            data_root,
            "Repositories",
            NULL
        );
    g_autofree char *trash_repo =
        g_build_filename (
            repositories,
            ".trash",
            "ewd",
            NULL
        );
    g_autofree char *trash_path =
        g_build_filename (
            trash_repo,
            TRASH_NAME,
            NULL
        );
    g_autofree char *trash_nested =
        g_build_filename (
            trash_path,
            "nested",
            NULL
        );
    g_autofree char *trash_file_a =
        g_build_filename (
            trash_path,
            "payload-a.bin",
            NULL
        );
    g_autofree char *trash_file_b =
        g_build_filename (
            trash_nested,
            "payload-b.bin",
            NULL
        );
    g_autofree char *active_path =
        g_build_filename (
            repositories,
            "ewd",
            "snapshots",
            ACTIVE_SHA,
            NULL
        );
    g_autofree char *active_file =
        g_build_filename (
            active_path,
            "keep.bin",
            NULL
        );

    if (!ensure_directory (
            trash_nested,
            &error
        ) ||
        !ensure_directory (
            active_path,
            &error
        ) ||
        !write_allocated_file (
            trash_file_a,
            8 * 1024 * 1024,
            0xa5,
            &error
        ) ||
        !write_allocated_file (
            trash_file_b,
            3 * 1024 * 1024,
            0x5a,
            &error
        ) ||
        !write_allocated_file (
            active_file,
            1024 * 1024,
            0x3c,
            &error
        ) ||
        !fsync_directory (
            trash_nested,
            &error
        ) ||
        !fsync_directory (
            trash_path,
            &error
        ) ||
        !fsync_directory (
            trash_repo,
            &error
        ) ||
        !fsync_directory (
            active_path,
            &error
        )) {
        g_printerr (
            "%s\n",
            error != NULL
                ? error->message
                : "Could not prepare reclamation fixture."
        );
        g_clear_error (
            &error
        );
        return 1;
    }

    AtmTreeCapacityMeasurement tree_before;
    AtmFilesystemCapacityMeasurement fs_before;

    if (!atm_capacity_measure_tree (
            trash_path,
            &tree_before,
            &error
        ) ||
        !atm_capacity_measure_filesystem (
            data_root,
            &fs_before,
            &error
        )) {
        g_printerr (
            "%s\n",
            error != NULL
                ? error->message
                : "Could not measure reclamation fixture."
        );
        g_clear_error (
            &error
        );
        return 1;
    }

    if (tree_before.logical_regular_bytes !=
            11 * 1024 * 1024 ||
        tree_before.allocated_tree_bytes == 0 ||
        tree_before.regular_files != 2 ||
        tree_before.directories != 1) {
        g_printerr (
            "Unexpected pre-purge trash-tree measurement.\n"
        );
        return 1;
    }

    AtmRepositoryGcPurgeStats purge_stats;

    if (!atm_repository_gc_purge_trash_entry (
            data_root,
            "ewd",
            TRASH_NAME,
            &purge_stats,
            &error
        )) {
        g_printerr (
            "%s\n",
            error != NULL
                ? error->message
                : "Qualified I5 purge failed."
        );
        g_clear_error (
            &error
        );
        return 1;
    }

    if (g_file_test (
            trash_path,
            G_FILE_TEST_EXISTS
        ) ||
        !g_file_test (
            active_file,
            G_FILE_TEST_IS_REGULAR
        )) {
        g_printerr (
            "I5 reclamation fixture violated namespace boundaries.\n"
        );
        return 1;
    }

    AtmFilesystemCapacityMeasurement fs_after;

    if (!atm_capacity_measure_filesystem (
            data_root,
            &fs_after,
            &error
        )) {
        g_printerr (
            "%s\n",
            error != NULL
                ? error->message
                : "Could not measure filesystem after purge."
        );
        g_clear_error (
            &error
        );
        return 1;
    }

    if (fs_after.device_id !=
        fs_before.device_id) {
        g_printerr (
            "Reclamation measurement crossed filesystem identity.\n"
        );
        return 1;
    }

    gint64 available_delta = 0;

    if (fs_after.available_bytes >=
        fs_before.available_bytes) {
        guint64 positive_delta =
            fs_after.available_bytes -
            fs_before.available_bytes;

        if (positive_delta >
            G_MAXINT64) {
            g_printerr (
                "Filesystem available-byte delta overflowed signed evidence range.\n"
            );
            return 1;
        }

        available_delta =
            (gint64) positive_delta;
    } else {
        guint64 negative_delta =
            fs_before.available_bytes -
            fs_after.available_bytes;

        if (negative_delta >
            G_MAXINT64) {
            g_printerr (
                "Filesystem available-byte delta overflowed signed evidence range.\n"
            );
            return 1;
        }

        available_delta =
            -((gint64) negative_delta);
    }

    if (available_delta <= 0) {
        g_printerr (
            "Controlled reclamation fixture observed no positive filesystem free-space delta.\n"
        );
        return 1;
    }

    gint64 inode_delta = 0;

    if (fs_before.inode_budget_known &&
        fs_after.inode_budget_known) {
        if (fs_after.available_inodes >=
            fs_before.available_inodes) {
            guint64 positive_delta =
                fs_after.available_inodes -
                fs_before.available_inodes;

            inode_delta =
                positive_delta <= G_MAXINT64
                    ? (gint64) positive_delta
                    : G_MAXINT64;
        } else {
            guint64 negative_delta =
                fs_before.available_inodes -
                fs_after.available_inodes;

            inode_delta =
                negative_delta <= G_MAXINT64
                    ? -((gint64) negative_delta)
                    : G_MININT64;
        }
    }

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"filesystem\":\"%s\","
        "\"device_id\":%" G_GUINT64_FORMAT ","
        "\"fragment_size\":%" G_GUINT64_FORMAT ","
        "\"trash_logical_regular_bytes_before\":%" G_GUINT64_FORMAT ","
        "\"trash_allocated_tree_bytes_before\":%" G_GUINT64_FORMAT ","
        "\"trash_entries_before\":%" G_GUINT64_FORMAT ","
        "\"trash_regular_files_before\":%" G_GUINT64_FORMAT ","
        "\"trash_directories_before\":%" G_GUINT64_FORMAT ","
        "\"available_bytes_before\":%" G_GUINT64_FORMAT ","
        "\"available_bytes_after\":%" G_GUINT64_FORMAT ","
        "\"available_bytes_delta\":%" G_GINT64_FORMAT ","
        "\"inode_budget_known\":%s,"
        "\"available_inodes_before\":%" G_GUINT64_FORMAT ","
        "\"available_inodes_after\":%" G_GUINT64_FORMAT ","
        "\"available_inodes_delta\":%" G_GINT64_FORMAT ","
        "\"purge_regular_files_removed\":%" G_GUINT64_FORMAT ","
        "\"purge_directories_removed\":%" G_GUINT64_FORMAT ","
        "\"purge_directory_fsync_calls\":%" G_GUINT64_FORMAT ","
        "\"trash_absent_after\":true,"
        "\"normal_snapshot_preserved\":true"
        "}\n",
        filesystem_label,
        fs_before.device_id,
        fs_before.fragment_size,
        tree_before.logical_regular_bytes,
        tree_before.allocated_tree_bytes,
        tree_before.entries,
        tree_before.regular_files,
        tree_before.directories,
        fs_before.available_bytes,
        fs_after.available_bytes,
        available_delta,
        fs_before.inode_budget_known
            ? "true"
            : "false",
        fs_before.available_inodes,
        fs_after.available_inodes,
        inode_delta,
        purge_stats.regular_files_removed,
        purge_stats.directories_removed,
        purge_stats.directory_fsync_calls
    );

    return 0;
}
