#define _GNU_SOURCE

#include "snapshot_namespace_durability.h"
#include "fault_injection_test_hook.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean
repository_id_is_valid (
    const char *repository_id
)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

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
open_and_sync_component (
    int parent_fd,
    const char *name,
    const char *pre_child_fsync_checkpoint,
    const char *pre_parent_fsync_checkpoint,
    int *out_child_fd,
    AtmSnapshotNamespaceStats *stats,
    GError **error
)
{
    gboolean created = FALSE;

    if (mkdirat (
            parent_fd,
            name,
            0700
        ) == 0) {
        created = TRUE;
    } else if (errno != EEXIST) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create snapshot namespace directory '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    int child_fd = openat (
        parent_fd,
        name,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (child_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open snapshot namespace directory '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    struct stat st;

    if (fstat (child_fd, &st) != 0) {
        int saved_errno = errno;
        close (child_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (saved_errno),
            "Could not inspect snapshot namespace directory '%s': %s",
            name,
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (st.st_mode)) {
        close (child_fd);
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Snapshot namespace component is not a real directory."
        );
        return FALSE;
    }

    if (created && stats != NULL) {
        stats->directories_created++;
    }

    if (pre_child_fsync_checkpoint != NULL) {
        atm_test_fault_checkpoint (
            pre_child_fsync_checkpoint
        );
    }

    if (!fsync_retry (
            child_fd,
            "Could not fsync snapshot namespace child directory",
            error
        )) {
        close (child_fd);
        return FALSE;
    }

    if (stats != NULL) {
        stats->directory_fsync_calls++;
    }

    if (pre_parent_fsync_checkpoint != NULL) {
        atm_test_fault_checkpoint (
            pre_parent_fsync_checkpoint
        );
    }

    if (!fsync_retry (
            parent_fd,
            "Could not fsync snapshot namespace parent directory",
            error
        )) {
        close (child_fd);
        return FALSE;
    }

    if (stats != NULL) {
        stats->directory_fsync_calls++;
    }

    *out_child_fd = child_fd;
    return TRUE;
}

gboolean
atm_snapshot_namespace_prepare_final_parent (
    const char *data_root,
    const char *repository_id,
    AtmSnapshotNamespaceStats *stats,
    GError **error
)
{
    g_return_val_if_fail (data_root != NULL, FALSE);
    g_return_val_if_fail (data_root[0] != '\0', FALSE);
    g_return_val_if_fail (repository_id != NULL, FALSE);

    if (!repository_id_is_valid (repository_id)) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Snapshot namespace repository ID is not part of the v1 catalog."
        );
        return FALSE;
    }

    int root_fd = open (
        data_root,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (root_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open trusted snapshot data root: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    int repositories_fd = -1;
    int repository_fd = -1;
    int snapshots_fd = -1;
    gboolean ok = FALSE;

    if (!open_and_sync_component (
            root_fd,
            "Repositories",
            NULL,
            NULL,
            &repositories_fd,
            stats,
            error
        ) ||
        !open_and_sync_component (
            repositories_fd,
            repository_id,
            "namespace_repository_before_child_fsync",
            "namespace_repository_before_parent_fsync",
            &repository_fd,
            stats,
            error
        ) ||
        !open_and_sync_component (
            repository_fd,
            "snapshots",
            NULL,
            NULL,
            &snapshots_fd,
            stats,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    if (snapshots_fd >= 0) {
        close (snapshots_fd);
    }
    if (repository_fd >= 0) {
        close (repository_fd);
    }
    if (repositories_fd >= 0) {
        close (repositories_fd);
    }
    close (root_fd);
    return ok;
}
