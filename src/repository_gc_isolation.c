#define _GNU_SOURCE

#include "repository_gc_isolation.h"
#include "fault_injection_test_hook.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean
repository_id_is_known (
    const char *repository_id
)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static gboolean
sha40_lower_is_valid (
    const char *value
)
{
    if (value == NULL ||
        strlen (value) != 40) {
        return FALSE;
    }

    for (gsize i = 0; i < 40; i++) {
        char c = value[i];

        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f'))) {
            return FALSE;
        }
    }

    return TRUE;
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
open_required_child_directory (
    int parent_fd,
    const char *name,
    int *out_fd,
    GError **error
)
{
    *out_fd = -1;

    int fd = openat (
        parent_fd,
        name,
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC
    );

    if (fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open required GC namespace directory '%s' without following links: %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    struct stat st;

    if (fstat (fd, &st) != 0) {
        int saved_errno = errno;

        close (fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (saved_errno),
            "Could not inspect required GC namespace directory '%s': %s",
            name,
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (st.st_mode)) {
        close (fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Required GC namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

    *out_fd = fd;
    return TRUE;
}

static gboolean
open_or_create_durable_child_directory (
    int parent_fd,
    const char *name,
    int *out_fd,
    AtmRepositoryGcIsolationStats *stats,
    GError **error
)
{
    *out_fd = -1;

    if (mkdirat (
            parent_fd,
            name,
            0700
        ) != 0 &&
        errno != EEXIST) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create GC trash namespace directory '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    int child_fd = openat (
        parent_fd,
        name,
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC
    );

    if (child_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open GC trash namespace directory '%s' without following links: %s",
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
            "Could not inspect GC trash namespace directory '%s': %s",
            name,
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (st.st_mode)) {
        close (child_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC trash namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

    if (!fsync_retry (
            child_fd,
            "Could not fsync GC trash namespace child directory",
            error
        )) {
        close (child_fd);
        return FALSE;
    }

    if (stats != NULL) {
        stats->namespace_fsync_calls++;
    }

    if (!fsync_retry (
            parent_fd,
            "Could not fsync GC trash namespace parent directory",
            error
        )) {
        close (child_fd);
        return FALSE;
    }

    if (stats != NULL) {
        stats->namespace_fsync_calls++;
    }

    *out_fd = child_fd;
    return TRUE;
}

static gboolean
validate_snapshot_source (
    int snapshots_fd,
    const char *snapshot_sha,
    int *out_snapshot_fd,
    GError **error
)
{
    *out_snapshot_fd = -1;

    struct stat path_st;

    if (fstatat (
            snapshots_fd,
            snapshot_sha,
            &path_st,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not inspect GC isolation source snapshot: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (path_st.st_mode) ||
        S_ISLNK (path_st.st_mode)) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC isolation source snapshot is not a real directory."
        );
        return FALSE;
    }

    int snapshot_fd = openat (
        snapshots_fd,
        snapshot_sha,
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC
    );

    if (snapshot_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open GC isolation source snapshot without following links: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    struct stat opened_st;

    if (fstat (
            snapshot_fd,
            &opened_st
        ) != 0) {
        int saved_errno = errno;

        close (snapshot_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (saved_errno),
            "Could not inspect opened GC isolation source snapshot: %s",
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (opened_st.st_mode) ||
        opened_st.st_dev != path_st.st_dev ||
        opened_st.st_ino != path_st.st_ino) {
        close (snapshot_fd);
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC isolation source snapshot changed during structural revalidation."
        );
        return FALSE;
    }

    *out_snapshot_fd = snapshot_fd;
    return TRUE;
}

gboolean
atm_repository_gc_isolate_snapshot_to_trash (
    const char *data_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_trash_path,
    AtmRepositoryGcIsolationStats *stats,
    GError **error
)
{
    if (data_root == NULL ||
        data_root[0] == '\0' ||
        !repository_id_is_known (
            repository_id
        ) ||
        !sha40_lower_is_valid (
            snapshot_sha
        ) ||
        out_trash_path == NULL ||
        *out_trash_path != NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC snapshot isolation received invalid arguments."
        );
        return FALSE;
    }

    if (stats != NULL) {
        *stats = (AtmRepositoryGcIsolationStats) { 0 };
    }

    int data_fd = open (
        data_root,
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC
    );

    if (data_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open GC isolation data root without following links: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    int repositories_fd = -1;
    int repository_fd = -1;
    int snapshots_fd = -1;
    int source_snapshot_fd = -1;
    int trash_fd = -1;
    int trash_repository_fd = -1;
    char *trash_name = NULL;
    char *trash_path = NULL;
    gboolean renamed = FALSE;
    gboolean ok = FALSE;

    if (!open_required_child_directory (
            data_fd,
            "Repositories",
            &repositories_fd,
            error
        ) ||
        !open_required_child_directory (
            repositories_fd,
            repository_id,
            &repository_fd,
            error
        ) ||
        !open_required_child_directory (
            repository_fd,
            "snapshots",
            &snapshots_fd,
            error
        ) ||
        !validate_snapshot_source (
            snapshots_fd,
            snapshot_sha,
            &source_snapshot_fd,
            error
        ) ||
        !open_or_create_durable_child_directory (
            repositories_fd,
            ".trash",
            &trash_fd,
            stats,
            error
        ) ||
        !open_or_create_durable_child_directory (
            trash_fd,
            repository_id,
            &trash_repository_fd,
            stats,
            error
        )) {
        goto done;
    }

    gint64 isolation_time = g_get_real_time ();

    for (guint attempt = 0;
         attempt < 100;
         attempt++) {
        g_clear_pointer (
            &trash_name,
            g_free
        );

        trash_name = g_strdup_printf (
            "%s-%" G_GINT64_FORMAT "-%ld-%u",
            snapshot_sha,
            isolation_time,
            (long) getpid (),
            attempt
        );

        atm_test_fault_checkpoint (
            "gc_isolate_pre_rename"
        );

        if (renameat2 (
                snapshots_fd,
                snapshot_sha,
                trash_repository_fd,
                trash_name,
                RENAME_NOREPLACE
            ) == 0) {
            renamed = TRUE;
            break;
        }

        if (errno == EEXIST) {
            continue;
        }

        if (errno == EXDEV) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (EXDEV),
                "GC isolation source and trash destination are not on the same mounted filesystem."
            );
        } else {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not atomically isolate snapshot to GC trash: %s",
                g_strerror (errno)
            );
        }
        goto done;
    }

    if (!renamed) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_EXIST,
            "Could not allocate a unique non-replacing GC trash identity."
        );
        goto done;
    }

    atm_test_fault_checkpoint (
        "gc_isolate_post_rename"
    );

    atm_test_fault_checkpoint (
        "gc_isolate_before_destination_fsync"
    );

    if (!fsync_retry (
            trash_repository_fd,
            "Could not fsync GC isolation destination directory",
            error
        )) {
        goto done;
    }

    if (stats != NULL) {
        stats->isolation_fsync_calls++;
    }

    atm_test_fault_checkpoint (
        "gc_isolate_after_destination_fsync"
    );

    atm_test_fault_checkpoint (
        "gc_isolate_before_source_fsync"
    );

    if (!fsync_retry (
            snapshots_fd,
            "Could not fsync GC isolation source directory",
            error
        )) {
        goto done;
    }

    if (stats != NULL) {
        stats->isolation_fsync_calls++;
    }

    atm_test_fault_checkpoint (
        "gc_isolate_after_source_fsync"
    );

    trash_path = g_build_filename (
        data_root,
        "Repositories",
        ".trash",
        repository_id,
        trash_name,
        NULL
    );

    *out_trash_path =
        g_steal_pointer (
            &trash_path
        );
    ok = TRUE;

done:
    g_free (trash_path);
    g_free (trash_name);

    if (trash_repository_fd >= 0) {
        close (trash_repository_fd);
    }
    if (trash_fd >= 0) {
        close (trash_fd);
    }
    if (source_snapshot_fd >= 0) {
        close (source_snapshot_fd);
    }
    if (snapshots_fd >= 0) {
        close (snapshots_fd);
    }
    if (repository_fd >= 0) {
        close (repository_fd);
    }
    if (repositories_fd >= 0) {
        close (repositories_fd);
    }
    close (data_fd);

    return ok;
}
