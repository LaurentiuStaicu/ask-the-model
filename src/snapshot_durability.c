#define _GNU_SOURCE

#include "snapshot_durability.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean
fsync_retry (
    int fd,
    const char *context,
    GError **error
)
{
    for (;;) {
        if (fsync (fd) == 0) {
            return TRUE;
        }

        if (errno == EINTR) {
            continue;
        }

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "%s: %s",
            context,
            g_strerror (errno)
        );
        return FALSE;
    }
}

static gboolean
sync_tree_directory (
    int directory_fd,
    AtmSnapshotDurabilityStats *stats,
    GError **error
)
{
    int scan_fd = dup (directory_fd);

    if (scan_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not duplicate snapshot durability directory: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    DIR *directory = fdopendir (scan_fd);

    if (directory == NULL) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not enumerate snapshot durability tree: %s",
            g_strerror (errno)
        );
        close (scan_fd);
        return FALSE;
    }

    struct dirent *item;

    for (;;) {
        errno = 0;
        item = readdir (directory);

        if (item == NULL) {
            if (errno != 0) {
                int read_errno = errno;

                g_set_error (
                    error,
                    G_FILE_ERROR,
                    g_file_error_from_errno (read_errno),
                    "Could not enumerate snapshot durability directory: %s",
                    g_strerror (read_errno)
                );
                closedir (directory);
                return FALSE;
            }

            break;
        }

        struct stat st;

        if (strcmp (item->d_name, ".") == 0 ||
            strcmp (item->d_name, "..") == 0) {
            continue;
        }

        if (fstatat (
                directory_fd,
                item->d_name,
                &st,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not inspect snapshot durability entry: %s",
                g_strerror (errno)
            );
            closedir (directory);
            return FALSE;
        }

        if (S_ISLNK (st.st_mode) ||
            (!S_ISREG (st.st_mode) &&
             !S_ISDIR (st.st_mode))) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Snapshot durability tree contains an unsupported entry."
            );
            closedir (directory);
            return FALSE;
        }

        if (S_ISDIR (st.st_mode)) {
            int child_fd = openat (
                directory_fd,
                item->d_name,
                O_RDONLY | O_DIRECTORY |
                    O_NOFOLLOW | O_CLOEXEC
            );

            if (child_fd < 0) {
                g_set_error (
                    error,
                    G_FILE_ERROR,
                    g_file_error_from_errno (errno),
                    "Could not open snapshot durability directory: %s",
                    g_strerror (errno)
                );
                closedir (directory);
                return FALSE;
            }

            gboolean ok = sync_tree_directory (
                child_fd,
                stats,
                error
            );
            close (child_fd);

            if (!ok) {
                closedir (directory);
                return FALSE;
            }

            continue;
        }

        int file_fd = openat (
            directory_fd,
            item->d_name,
            O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC
        );

        if (file_fd < 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not open snapshot durability file: %s",
                g_strerror (errno)
            );
            closedir (directory);
            return FALSE;
        }

        struct stat opened_st;

        if (fstat (file_fd, &opened_st) != 0) {
            int stat_errno = errno;

            close (file_fd);
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (stat_errno),
                "Could not revalidate snapshot durability file: %s",
                g_strerror (stat_errno)
            );
            closedir (directory);
            return FALSE;
        }

        if (!S_ISREG (opened_st.st_mode)) {
            close (file_fd);
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Snapshot durability file changed to an unsupported entry."
            );
            closedir (directory);
            return FALSE;
        }

        gboolean ok = fsync_retry (
            file_fd,
            "Could not fsync snapshot durability file",
            error
        );
        close (file_fd);

        if (!ok) {
            closedir (directory);
            return FALSE;
        }

        if (stats != NULL) {
            stats->file_fsync_calls++;
        }
    }

    closedir (directory);

    if (!fsync_retry (
            directory_fd,
            "Could not fsync snapshot durability directory",
            error
        )) {
        return FALSE;
    }

    if (stats != NULL) {
        stats->directory_fsync_calls++;
    }

    return TRUE;
}

gboolean
atm_snapshot_durability_sync_tree (
    const char *snapshot_root,
    AtmSnapshotDurabilityStats *stats,
    GError **error
)
{
    g_return_val_if_fail (snapshot_root != NULL, FALSE);

    int root_fd = open (
        snapshot_root,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (root_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open snapshot durability root: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    gboolean ok = sync_tree_directory (
        root_fd,
        stats,
        error
    );

    close (root_fd);
    return ok;
}

gboolean
atm_snapshot_durability_sync_parent (
    const char *snapshot_path,
    AtmSnapshotDurabilityStats *stats,
    GError **error
)
{
    g_return_val_if_fail (snapshot_path != NULL, FALSE);

    char *parent = g_path_get_dirname (
        snapshot_path
    );
    int parent_fd = open (
        parent,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );
    g_free (parent);

    if (parent_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open promoted snapshot parent: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    gboolean ok = fsync_retry (
        parent_fd,
        "Could not fsync promoted snapshot parent",
        error
    );

    if (ok && stats != NULL) {
        stats->parent_fsync_calls++;
    }

    close (parent_fd);
    return ok;
}
