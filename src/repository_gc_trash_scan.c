#define _GNU_SOURCE

#include "repository_gc_trash_scan.h"
#include "repository_gc_purge.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *KNOWN_REPOSITORIES[] = {
    "ewd",
    "cbd",
    "rmd"
};

static gboolean
repository_id_is_known (
    const char *repository_id
)
{
    for (guint i = 0;
         i < G_N_ELEMENTS (KNOWN_REPOSITORIES);
         i++) {
        if (g_strcmp0 (
                repository_id,
                KNOWN_REPOSITORIES[i]
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
open_required_directory (
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
            "Could not open required GC trash-scan directory '%s' without following links: %s",
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
            "GC trash-scan namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

    *out_fd = fd;
    return TRUE;
}

static gboolean
open_optional_directory (
    int parent_fd,
    const char *name,
    int *out_fd,
    gboolean *out_found,
    GError **error
)
{
    *out_fd = -1;
    *out_found = FALSE;

    struct stat path_st;

    if (fstatat (
            parent_fd,
            name,
            &path_st,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not inspect optional GC trash-scan directory '%s': %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (
            path_st.st_mode
        ) ||
        S_ISLNK (
            path_st.st_mode
        )) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC trash-scan namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

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
            "Could not open optional GC trash-scan directory '%s' without following links: %s",
            name,
            g_strerror (errno)
        );
        return FALSE;
    }

    struct stat opened_st;

    if (fstat (
            fd,
            &opened_st
        ) != 0 ||
        !S_ISDIR (
            opened_st.st_mode
        ) ||
        opened_st.st_dev !=
            path_st.st_dev ||
        opened_st.st_ino !=
            path_st.st_ino) {
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
            "GC trash-scan namespace component '%s' changed during no-follow revalidation.",
            name
        );
        return FALSE;
    }

    *out_fd = fd;
    *out_found = TRUE;
    return TRUE;
}

static gboolean
candidate_precedes (
    gboolean have_candidate,
    const char *candidate_repository,
    const char *candidate_name,
    const char *current_repository,
    const char *current_name
)
{
    if (!have_candidate) {
        return TRUE;
    }

    int repository_order =
        g_strcmp0 (
            current_repository,
            candidate_repository
        );

    if (repository_order != 0) {
        return repository_order < 0;
    }

    return g_strcmp0 (
        current_name,
        candidate_name
    ) < 0;
}

static gboolean
validate_trash_root_entries (
    int trash_fd,
    GError **error
)
{
    int iteration_fd = openat (
        trash_fd,
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
            "Could not open GC trash root for read-only enumeration: %s",
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
            "Could not enumerate GC trash root: %s",
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
                    "Could not continue GC trash-root enumeration: %s",
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

        if (!repository_id_is_known (
                item->d_name
            )) {
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "GC trash root contains unexpected repository namespace '%s'.",
                item->d_name
            );
            goto done;
        }

        struct stat st;

        if (fstatat (
                trash_fd,
                item->d_name,
                &st,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not inspect GC trash repository namespace '%s': %s",
                item->d_name,
                g_strerror (errno)
            );
            goto done;
        }

        if (!S_ISDIR (
                st.st_mode
            ) ||
            S_ISLNK (
                st.st_mode
            )) {
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "GC trash repository namespace '%s' is not a real directory.",
                item->d_name
            );
            goto done;
        }
    }

    ok = TRUE;

done:
    closedir (directory);
    return ok;
}

static gboolean
scan_repository_trash (
    int trash_fd,
    const char *repository_id,
    gboolean *have_candidate,
    char **candidate_repository,
    char **candidate_name,
    GError **error
)
{
    int repository_fd = -1;
    gboolean repository_found = FALSE;

    if (!open_optional_directory (
            trash_fd,
            repository_id,
            &repository_fd,
            &repository_found,
            error
        )) {
        return FALSE;
    }

    if (!repository_found) {
        return TRUE;
    }

    int iteration_fd = openat (
        repository_fd,
        ".",
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC
    );

    if (iteration_fd < 0) {
        int saved_errno = errno;
        close (repository_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (
                saved_errno
            ),
            "Could not open GC trash repository '%s' for enumeration: %s",
            repository_id,
            g_strerror (saved_errno)
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
        close (repository_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (
                saved_errno
            ),
            "Could not enumerate GC trash repository '%s': %s",
            repository_id,
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
                    "Could not continue GC trash repository enumeration for '%s': %s",
                    repository_id,
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

        struct stat st;

        if (fstatat (
                repository_fd,
                item->d_name,
                &st,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not inspect GC trash object '%s/%s': %s",
                repository_id,
                item->d_name,
                g_strerror (errno)
            );
            goto done;
        }

        if (!S_ISDIR (
                st.st_mode
            ) ||
            S_ISLNK (
                st.st_mode
            )) {
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "GC trash object '%s/%s' is not a real directory.",
                repository_id,
                item->d_name
            );
            goto done;
        }

        if (!atm_repository_gc_trash_name_parse (
                item->d_name,
                NULL,
                NULL,
                NULL
            )) {
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "GC trash object '%s/%s' has a noncanonical I4 identity.",
                repository_id,
                item->d_name
            );
            goto done;
        }

        if (candidate_precedes (
                *have_candidate,
                *candidate_repository,
                *candidate_name,
                repository_id,
                item->d_name
            )) {
            g_free (
                *candidate_repository
            );
            g_free (
                *candidate_name
            );

            *candidate_repository =
                g_strdup (
                    repository_id
                );
            *candidate_name =
                g_strdup (
                    item->d_name
                );
            *have_candidate = TRUE;
        }
    }

    ok = TRUE;

done:
    closedir (directory);
    close (repository_fd);
    return ok;
}

gboolean
atm_repository_gc_select_canonical_trash_candidate (
    const char *data_root,
    gboolean *out_found,
    char **out_repository_id,
    char **out_snapshot_sha,
    char **out_trash_name,
    GError **error
)
{
    if (out_found == NULL ||
        out_repository_id == NULL ||
        out_snapshot_sha == NULL ||
        out_trash_name == NULL ||
        data_root == NULL ||
        data_root[0] == '\0') {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC trash discovery received invalid arguments."
        );
        return FALSE;
    }

    *out_found = FALSE;
    *out_repository_id =
        g_strdup ("");
    *out_snapshot_sha =
        g_strdup ("");
    *out_trash_name =
        g_strdup ("");

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
            "Could not open GC trash-discovery data root without following links: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    int repositories_fd = -1;

    if (!open_required_directory (
            data_fd,
            "Repositories",
            &repositories_fd,
            error
        )) {
        close (data_fd);
        return FALSE;
    }

    int trash_fd = -1;
    gboolean trash_found = FALSE;

    if (!open_optional_directory (
            repositories_fd,
            ".trash",
            &trash_fd,
            &trash_found,
            error
        )) {
        close (repositories_fd);
        close (data_fd);
        return FALSE;
    }

    if (!trash_found) {
        close (repositories_fd);
        close (data_fd);
        return TRUE;
    }

    if (!validate_trash_root_entries (
            trash_fd,
            error
        )) {
        close (trash_fd);
        close (repositories_fd);
        close (data_fd);
        return FALSE;
    }

    gboolean have_candidate = FALSE;
    char *candidate_repository = NULL;
    char *candidate_name = NULL;
    gboolean ok = TRUE;

    for (guint i = 0;
         i < G_N_ELEMENTS (KNOWN_REPOSITORIES);
         i++) {
        if (!scan_repository_trash (
                trash_fd,
                KNOWN_REPOSITORIES[i],
                &have_candidate,
                &candidate_repository,
                &candidate_name,
                error
            )) {
            ok = FALSE;
            break;
        }
    }

    if (ok &&
        have_candidate) {
        g_free (
            *out_repository_id
        );
        g_free (
            *out_snapshot_sha
        );
        g_free (
            *out_trash_name
        );

        *out_repository_id =
            g_steal_pointer (
                &candidate_repository
            );
        *out_snapshot_sha =
            g_strndup (
                candidate_name,
                40
            );
        *out_trash_name =
            g_steal_pointer (
                &candidate_name
            );
        *out_found = TRUE;
    }

    g_free (
        candidate_repository
    );
    g_free (
        candidate_name
    );

    close (trash_fd);
    close (repositories_fd);
    close (data_fd);
    return ok;
}
