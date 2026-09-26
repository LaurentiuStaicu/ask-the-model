#define _GNU_SOURCE

#include "repository_gc_purge.h"
#include "fault_injection_test_hook.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    AtmRepositoryGcPurgeStats *stats;
    gboolean first_unlink_observed;
} PurgeContext;

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
sha40_lower_is_valid_prefix (
    const char *value
)
{
    if (value == NULL ||
        strlen (value) < 40) {
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
canonical_decimal_segment (
    const char *start,
    const char *end,
    guint64 maximum,
    gboolean allow_zero
)
{
    if (start == NULL ||
        end == NULL ||
        start >= end) {
        return FALSE;
    }

    if (end - start > 1 &&
        start[0] == '0') {
        return FALSE;
    }

    guint64 value = 0;

    for (const char *p = start;
         p < end;
         p++) {
        if (*p < '0' ||
            *p > '9') {
            return FALSE;
        }

        guint64 digit =
            (guint64) (*p - '0');

        if (value >
            (maximum - digit) / 10) {
            return FALSE;
        }

        value =
            value * 10 +
            digit;
    }

    if (!allow_zero &&
        value == 0) {
        return FALSE;
    }

    return value <= maximum;
}

static gboolean
trash_name_is_valid (
    const char *trash_name
)
{
    if (!sha40_lower_is_valid_prefix (
            trash_name
        ) ||
        trash_name[40] != '-') {
        return FALSE;
    }

    const char *time_start =
        trash_name + 41;
    const char *time_end =
        strchr (
            time_start,
            '-'
        );

    if (time_end == NULL ||
        !canonical_decimal_segment (
            time_start,
            time_end,
            G_MAXINT64,
            FALSE
        )) {
        return FALSE;
    }

    const char *pid_start =
        time_end + 1;
    const char *pid_end =
        strchr (
            pid_start,
            '-'
        );

    if (pid_end == NULL ||
        !canonical_decimal_segment (
            pid_start,
            pid_end,
            G_MAXINT,
            FALSE
        )) {
        return FALSE;
    }

    const char *attempt_start =
        pid_end + 1;
    const char *attempt_end =
        trash_name +
        strlen (trash_name);

    if (!canonical_decimal_segment (
            attempt_start,
            attempt_end,
            99,
            TRUE
        )) {
        return FALSE;
    }

    return strchr (
        attempt_start,
        '-'
    ) == NULL;
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
            "Could not open required GC purge directory '%s' without following links: %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    struct stat st;

    if (fstat (
            fd,
            &st
        ) != 0 ||
        !S_ISDIR (
            st.st_mode
        )) {
        int saved_errno =
            errno != 0
                ? errno
                : EINVAL;

        close (fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (
                saved_errno
            ),
            "GC purge namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

    *out_fd = fd;
    return TRUE;
}

static gboolean
open_revalidated_child_directory (
    int parent_fd,
    const char *name,
    const struct stat *path_st,
    int *out_fd,
    GError **error
)
{
    *out_fd = -1;

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
            "Could not open GC purge child directory '%s' without following links: %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    struct stat opened_st;

    if (fstat (
            child_fd,
            &opened_st
        ) != 0) {
        int saved_errno = errno;

        close (child_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (
                saved_errno
            ),
            "Could not inspect opened GC purge child directory '%s': %s",
            name,
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (
            opened_st.st_mode
        ) ||
        opened_st.st_dev !=
            path_st->st_dev ||
        opened_st.st_ino !=
            path_st->st_ino) {
        close (child_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC purge child directory '%s' changed during no-follow revalidation.",
            name
        );
        return FALSE;
    }

    *out_fd = child_fd;
    return TRUE;
}

static gboolean
for_each_directory_entry (
    int directory_fd,
    gboolean (*visitor) (
        int,
        const char *,
        gpointer,
        GError **
    ),
    gpointer user_data,
    GError **error
)
{
    int iteration_fd =
        openat (
            directory_fd,
            ".",
            O_RDONLY |
            O_DIRECTORY |
            O_NOFOLLOW |
            O_CLOEXEC
        );

    if (iteration_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open an independent GC purge directory iterator: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    DIR *directory =
        fdopendir (
            iteration_fd
        );

    if (directory == NULL) {
        int saved_errno = errno;

        close (iteration_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (
                saved_errno
            ),
            "Could not enumerate GC purge directory: %s",
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    gboolean ok = FALSE;

    for (;;) {
        errno = 0;
        struct dirent *item =
            readdir (
                directory
            );

        if (item == NULL) {
            if (errno != 0) {
                int saved_errno = errno;

                g_set_error (
                    error,
                    G_FILE_ERROR,
                    g_file_error_from_errno (
                        saved_errno
                    ),
                    "Could not continue GC purge directory enumeration: %s",
                    g_strerror (saved_errno)
                );
                goto done;
            }

            break;
        }

        if (strcmp (
                item->d_name,
                "."
            ) == 0 ||
            strcmp (
                item->d_name,
                ".."
            ) == 0) {
            continue;
        }

        if (!visitor (
                directory_fd,
                item->d_name,
                user_data,
                error
            )) {
            goto done;
        }
    }

    ok = TRUE;

done:
    closedir (directory);
    return ok;
}

static gboolean
collect_directory_entry_names (
    int directory_fd,
    GPtrArray **out_names,
    GError **error
)
{
    if (out_names == NULL ||
        *out_names != NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC purge name collection received invalid output storage."
        );
        return FALSE;
    }

    int iteration_fd =
        openat (
            directory_fd,
            ".",
            O_RDONLY |
            O_DIRECTORY |
            O_NOFOLLOW |
            O_CLOEXEC
        );

    if (iteration_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open GC purge directory for stable name collection: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    DIR *directory =
        fdopendir (
            iteration_fd
        );

    if (directory == NULL) {
        int saved_errno = errno;

        close (iteration_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (
                saved_errno
            ),
            "Could not enumerate GC purge directory for stable name collection: %s",
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    GPtrArray *names =
        g_ptr_array_new_with_free_func (
            g_free
        );
    gboolean ok = FALSE;

    for (;;) {
        errno = 0;
        struct dirent *item =
            readdir (
                directory
            );

        if (item == NULL) {
            if (errno != 0) {
                int saved_errno = errno;

                g_set_error (
                    error,
                    G_FILE_ERROR,
                    g_file_error_from_errno (
                        saved_errno
                    ),
                    "Could not continue stable GC purge name collection: %s",
                    g_strerror (saved_errno)
                );
                goto done;
            }

            break;
        }

        if (strcmp (
                item->d_name,
                "."
            ) == 0 ||
            strcmp (
                item->d_name,
                ".."
            ) == 0) {
            continue;
        }

        g_ptr_array_add (
            names,
            g_strdup (
                item->d_name
            )
        );
    }

    *out_names = names;
    names = NULL;
    ok = TRUE;

done:
    if (names != NULL) {
        g_ptr_array_unref (names);
    }

    closedir (directory);
    return ok;
}


static gboolean validate_tree_directory (
    int directory_fd,
    GError **error
);

static gboolean
validate_tree_entry (
    int directory_fd,
    const char *name,
    gpointer user_data,
    GError **error
)
{
    (void) user_data;

    struct stat st;

    if (fstatat (
            directory_fd,
            name,
            &st,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not inspect GC purge tree entry '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    if (S_ISREG (
            st.st_mode
        )) {
        return TRUE;
    }

    if (!S_ISDIR (
            st.st_mode
        )) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC purge tree entry '%s' is a symlink or unsupported special object.",
            name
        );
        return FALSE;
    }

    int child_fd = -1;

    if (!open_revalidated_child_directory (
            directory_fd,
            name,
            &st,
            &child_fd,
            error
        )) {
        return FALSE;
    }

    gboolean ok =
        validate_tree_directory (
            child_fd,
            error
        );

    close (child_fd);
    return ok;
}

static gboolean
validate_tree_directory (
    int directory_fd,
    GError **error
)
{
    return for_each_directory_entry (
        directory_fd,
        validate_tree_entry,
        NULL,
        error
    );
}

static void
checkpoint_before_unlink (
    PurgeContext *context
)
{
    if (context->
            first_unlink_observed) {
        return;
    }

    atm_test_fault_checkpoint (
        "gc_purge_before_first_unlink"
    );
}

static void
checkpoint_after_unlink (
    PurgeContext *context
)
{
    if (context->
            first_unlink_observed) {
        return;
    }

    context->
        first_unlink_observed = TRUE;

    atm_test_fault_checkpoint (
        "gc_purge_after_first_unlink"
    );
}

static gboolean purge_tree_directory (
    int directory_fd,
    PurgeContext *context,
    GError **error
);

static gboolean
purge_tree_entry (
    int directory_fd,
    const char *name,
    gpointer user_data,
    GError **error
)
{
    PurgeContext *context =
        user_data;
    struct stat st;

    if (fstatat (
            directory_fd,
            name,
            &st,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not revalidate GC purge tree entry '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    if (S_ISREG (
            st.st_mode
        )) {
        checkpoint_before_unlink (
            context
        );

        if (unlinkat (
                directory_fd,
                name,
                0
            ) != 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not unlink GC trash file '%s': %s",
                name,
                g_strerror (errno)
            );
            return FALSE;
        }

        if (context->stats != NULL) {
            context->stats->
                regular_files_removed++;
        }

        checkpoint_after_unlink (
            context
        );
        return TRUE;
    }

    if (!S_ISDIR (
            st.st_mode
        )) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC purge tree entry '%s' changed to a symlink or unsupported special object.",
            name
        );
        return FALSE;
    }

    int child_fd = -1;

    if (!open_revalidated_child_directory (
            directory_fd,
            name,
            &st,
            &child_fd,
            error
        )) {
        return FALSE;
    }

    gboolean ok =
        purge_tree_directory (
            child_fd,
            context,
            error
        );

    close (child_fd);

    if (!ok) {
        return FALSE;
    }

    checkpoint_before_unlink (
        context
    );

    if (unlinkat (
            directory_fd,
            name,
            AT_REMOVEDIR
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not remove GC trash directory '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    if (context->stats != NULL) {
        context->stats->
            directories_removed++;
    }

    checkpoint_after_unlink (
        context
    );
    return TRUE;
}

static gboolean
purge_tree_directory (
    int directory_fd,
    PurgeContext *context,
    GError **error
)
{
    GPtrArray *names = NULL;

    if (!collect_directory_entry_names (
            directory_fd,
            &names,
            error
        )) {
        return FALSE;
    }

    for (guint i = 0;
         i < names->len;
         i++) {
        const char *name =
            g_ptr_array_index (
                names,
                i
            );

        if (!purge_tree_entry (
                directory_fd,
                name,
                context,
                error
            )) {
            g_ptr_array_unref (names);
            return FALSE;
        }
    }

    g_ptr_array_unref (names);

    if (!fsync_retry (
            directory_fd,
            "Could not fsync GC trash directory after recursive purge",
            error
        )) {
        return FALSE;
    }

    if (context->stats != NULL) {
        context->stats->
            directory_fsync_calls++;
    }

    return TRUE;
}

gboolean
atm_repository_gc_purge_trash_entry (
    const char *data_root,
    const char *repository_id,
    const char *trash_name,
    AtmRepositoryGcPurgeStats *stats,
    GError **error
)
{
    if (data_root == NULL ||
        data_root[0] == '\0' ||
        !repository_id_is_known (
            repository_id
        ) ||
        !trash_name_is_valid (
            trash_name
        )) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC trash purge received invalid arguments or a noncanonical trash identity."
        );
        return FALSE;
    }

    if (stats != NULL) {
        *stats =
            (AtmRepositoryGcPurgeStats) {
                0
            };
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
            "Could not open GC purge data root without following links: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    int repositories_fd = -1;
    int trash_fd = -1;
    int trash_repository_fd = -1;
    int entry_fd = -1;
    gboolean ok = FALSE;

    if (!open_required_child_directory (
            data_fd,
            "Repositories",
            &repositories_fd,
            error
        ) ||
        !open_required_child_directory (
            repositories_fd,
            ".trash",
            &trash_fd,
            error
        ) ||
        !open_required_child_directory (
            trash_fd,
            repository_id,
            &trash_repository_fd,
            error
        )) {
        goto done;
    }

    struct stat entry_st;

    if (fstatat (
            trash_repository_fd,
            trash_name,
            &entry_st,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not inspect selected GC trash entry: %s",
            g_strerror (errno)
        );
        goto done;
    }

    if (!S_ISDIR (
            entry_st.st_mode
        ) ||
        S_ISLNK (
            entry_st.st_mode
        )) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Selected GC trash entry is not a real directory."
        );
        goto done;
    }

    if (!open_revalidated_child_directory (
            trash_repository_fd,
            trash_name,
            &entry_st,
            &entry_fd,
            error
        )) {
        goto done;
    }

    if (!validate_tree_directory (
            entry_fd,
            error
        )) {
        goto done;
    }

    PurgeContext context = {
        .stats = stats,
        .first_unlink_observed = FALSE
    };

    if (!purge_tree_directory (
            entry_fd,
            &context,
            error
        )) {
        goto done;
    }

    close (entry_fd);
    entry_fd = -1;

    atm_test_fault_checkpoint (
        "gc_purge_before_root_rmdir"
    );

    if (unlinkat (
            trash_repository_fd,
            trash_name,
            AT_REMOVEDIR
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not remove emptied GC trash root entry: %s",
            g_strerror (errno)
        );
        goto done;
    }

    if (stats != NULL) {
        stats->directories_removed++;
    }

    atm_test_fault_checkpoint (
        "gc_purge_after_root_rmdir"
    );

    if (!fsync_retry (
            trash_repository_fd,
            "Could not fsync GC trash repository after root removal",
            error
        )) {
        goto done;
    }

    if (stats != NULL) {
        stats->directory_fsync_calls++;
    }

    atm_test_fault_checkpoint (
        "gc_purge_after_parent_fsync"
    );

    ok = TRUE;

done:
    if (entry_fd >= 0) {
        close (entry_fd);
    }
    if (trash_repository_fd >= 0) {
        close (trash_repository_fd);
    }
    if (trash_fd >= 0) {
        close (trash_fd);
    }
    if (repositories_fd >= 0) {
        close (repositories_fd);
    }
    close (data_fd);
    return ok;
}
