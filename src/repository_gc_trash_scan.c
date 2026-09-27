#define _GNU_SOURCE

#include "repository_gc_trash_scan.h"
#include "repository_gc_purge.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *name;
    char *snapshot_sha;
} AtmRepositoryGcTrashCandidate;

typedef struct {
    char *name;
    char *reason;
} AtmRepositoryGcTrashDiagnostic;

struct AtmRepositoryGcTrashScan {
    GPtrArray *candidates;
    GPtrArray *diagnostics;
};

static gboolean
repository_id_is_known (
    const char *repository_id
)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static void
candidate_free (
    gpointer data
)
{
    AtmRepositoryGcTrashCandidate *candidate = data;

    if (candidate == NULL) {
        return;
    }

    g_free (candidate->name);
    g_free (candidate->snapshot_sha);
    g_free (candidate);
}

static void
diagnostic_free (
    gpointer data
)
{
    AtmRepositoryGcTrashDiagnostic *diagnostic = data;

    if (diagnostic == NULL) {
        return;
    }

    g_free (diagnostic->name);
    g_free (diagnostic->reason);
    g_free (diagnostic);
}

static AtmRepositoryGcTrashScan *
scan_new (void)
{
    AtmRepositoryGcTrashScan *scan =
        g_new0 (
            AtmRepositoryGcTrashScan,
            1
        );

    scan->candidates =
        g_ptr_array_new_with_free_func (
            candidate_free
        );
    scan->diagnostics =
        g_ptr_array_new_with_free_func (
            diagnostic_free
        );

    return scan;
}

void
atm_repository_gc_trash_scan_free (
    AtmRepositoryGcTrashScan *scan
)
{
    if (scan == NULL) {
        return;
    }

    g_clear_pointer (
        &scan->candidates,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &scan->diagnostics,
        g_ptr_array_unref
    );
    g_free (scan);
}

static gint
compare_candidate_ptrs (
    gconstpointer a,
    gconstpointer b
)
{
    const AtmRepositoryGcTrashCandidate *left =
        *(AtmRepositoryGcTrashCandidate * const *) a;
    const AtmRepositoryGcTrashCandidate *right =
        *(AtmRepositoryGcTrashCandidate * const *) b;

    return g_strcmp0 (
        left != NULL ? left->name : NULL,
        right != NULL ? right->name : NULL
    );
}

static gint
compare_diagnostic_ptrs (
    gconstpointer a,
    gconstpointer b
)
{
    const AtmRepositoryGcTrashDiagnostic *left =
        *(AtmRepositoryGcTrashDiagnostic * const *) a;
    const AtmRepositoryGcTrashDiagnostic *right =
        *(AtmRepositoryGcTrashDiagnostic * const *) b;

    gint name_cmp = g_strcmp0 (
        left != NULL ? left->name : NULL,
        right != NULL ? right->name : NULL
    );

    if (name_cmp != 0) {
        return name_cmp;
    }

    return g_strcmp0 (
        left != NULL ? left->reason : NULL,
        right != NULL ? right->reason : NULL
    );
}

static void
add_candidate (
    AtmRepositoryGcTrashScan *scan,
    const char *name
)
{
    AtmRepositoryGcTrashCandidate *candidate =
        g_new0 (
            AtmRepositoryGcTrashCandidate,
            1
        );

    candidate->name = g_strdup (name);
    candidate->snapshot_sha =
        g_strndup (
            name,
            40
        );

    g_ptr_array_add (
        scan->candidates,
        candidate
    );
}

static void
add_diagnostic (
    AtmRepositoryGcTrashScan *scan,
    const char *name,
    const char *reason
)
{
    AtmRepositoryGcTrashDiagnostic *diagnostic =
        g_new0 (
            AtmRepositoryGcTrashDiagnostic,
            1
        );

    diagnostic->name =
        g_strdup (
            name != NULL ? name : ""
        );
    diagnostic->reason =
        g_strdup (
            reason != NULL ? reason : "unknown"
        );

    g_ptr_array_add (
        scan->diagnostics,
        diagnostic
    );
}

static gboolean
open_child_directory (
    int parent_fd,
    const char *name,
    gboolean missing_is_empty,
    int *out_fd,
    gboolean *out_missing,
    GError **error
)
{
    *out_fd = -1;
    *out_missing = FALSE;

    int fd = openat (
        parent_fd,
        name,
        O_RDONLY |
        O_DIRECTORY |
        O_NOFOLLOW |
        O_CLOEXEC
    );

    if (fd < 0) {
        if (missing_is_empty &&
            errno == ENOENT) {
            *out_missing = TRUE;
            return TRUE;
        }

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open GC trash namespace component '%s' without following links: %s",
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
            "Could not inspect GC trash namespace component '%s': %s",
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
            "GC trash namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

    *out_fd = fd;
    return TRUE;
}

static gboolean
entry_is_real_directory (
    int trash_repository_fd,
    const char *name,
    gboolean *out_real_directory,
    GError **error
)
{
    struct stat st;

    if (fstatat (
            trash_repository_fd,
            name,
            &st,
            AT_SYMLINK_NOFOLLOW
        ) != 0) {
        int saved_errno = errno;

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (saved_errno),
            "Could not inspect GC trash entry '%s': %s",
            name,
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    *out_real_directory =
        S_ISDIR (st.st_mode) &&
        !S_ISLNK (st.st_mode);
    return TRUE;
}

static gboolean
scan_trash_repository (
    int trash_repository_fd,
    AtmRepositoryGcTrashScan *scan,
    GError **error
)
{
    int duplicate_fd = dup (
        trash_repository_fd
    );

    if (duplicate_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not duplicate GC trash directory descriptor: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    DIR *directory = fdopendir (
        duplicate_fd
    );

    if (directory == NULL) {
        int saved_errno = errno;

        close (duplicate_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (saved_errno),
            "Could not enumerate GC trash directory: %s",
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    gboolean ok = FALSE;

    for (;;) {
        errno = 0;
        struct dirent *item =
            readdir (directory);

        if (item == NULL) {
            if (errno != 0) {
                int saved_errno = errno;

                g_set_error (
                    error,
                    G_FILE_ERROR,
                    g_file_error_from_errno (saved_errno),
                    "Could not continue GC trash directory enumeration: %s",
                    g_strerror (saved_errno)
                );
                goto done;
            }

            break;
        }

        if (strcmp (item->d_name, ".") == 0 ||
            strcmp (item->d_name, "..") == 0) {
            continue;
        }

        gboolean real_directory = FALSE;

        if (!entry_is_real_directory (
                trash_repository_fd,
                item->d_name,
                &real_directory,
                error
            )) {
            goto done;
        }

        gboolean canonical =
            atm_repository_gc_trash_name_is_canonical (
                item->d_name
            );

        if (canonical &&
            real_directory) {
            add_candidate (
                scan,
                item->d_name
            );
            continue;
        }

        add_diagnostic (
            scan,
            item->d_name,
            real_directory
                ? "malformed-trash-name"
                : "not-real-directory"
        );
    }

    g_ptr_array_sort (
        scan->candidates,
        compare_candidate_ptrs
    );
    g_ptr_array_sort (
        scan->diagnostics,
        compare_diagnostic_ptrs
    );
    ok = TRUE;

done:
    closedir (directory);
    return ok;
}

gboolean
atm_repository_gc_trash_scan (
    const char *data_root,
    const char *repository_id,
    AtmRepositoryGcTrashScan **out_scan,
    GError **error
)
{
    if (data_root == NULL ||
        data_root[0] == '\0' ||
        !repository_id_is_known (
            repository_id
        ) ||
        out_scan == NULL ||
        *out_scan != NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "GC trash scan received invalid arguments."
        );
        return FALSE;
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
            "Could not open GC trash data root without following links: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    AtmRepositoryGcTrashScan *scan =
        scan_new ();
    int repositories_fd = -1;
    int trash_fd = -1;
    int trash_repository_fd = -1;
    gboolean missing = FALSE;
    gboolean ok = FALSE;

    if (!open_child_directory (
            data_fd,
            "Repositories",
            TRUE,
            &repositories_fd,
            &missing,
            error
        )) {
        goto done;
    }

    if (missing) {
        ok = TRUE;
        goto done;
    }

    if (!open_child_directory (
            repositories_fd,
            ".trash",
            TRUE,
            &trash_fd,
            &missing,
            error
        )) {
        goto done;
    }

    if (missing) {
        ok = TRUE;
        goto done;
    }

    if (!open_child_directory (
            trash_fd,
            repository_id,
            TRUE,
            &trash_repository_fd,
            &missing,
            error
        )) {
        goto done;
    }

    if (missing) {
        ok = TRUE;
        goto done;
    }

    ok = scan_trash_repository (
        trash_repository_fd,
        scan,
        error
    );

done:
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

    if (!ok) {
        atm_repository_gc_trash_scan_free (
            scan
        );
        return FALSE;
    }

    *out_scan = scan;
    return TRUE;
}

gsize
atm_repository_gc_trash_scan_candidate_count (
    const AtmRepositoryGcTrashScan *scan
)
{
    return scan != NULL &&
        scan->candidates != NULL
            ? scan->candidates->len
            : 0;
}

const char *
atm_repository_gc_trash_scan_candidate_name (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->candidates == NULL ||
        index >= scan->candidates->len) {
        return NULL;
    }

    AtmRepositoryGcTrashCandidate *candidate =
        g_ptr_array_index (
            scan->candidates,
            index
        );

    return candidate != NULL
        ? candidate->name
        : NULL;
}

const char *
atm_repository_gc_trash_scan_candidate_sha (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->candidates == NULL ||
        index >= scan->candidates->len) {
        return NULL;
    }

    AtmRepositoryGcTrashCandidate *candidate =
        g_ptr_array_index (
            scan->candidates,
            index
        );

    return candidate != NULL
        ? candidate->snapshot_sha
        : NULL;
}

gsize
atm_repository_gc_trash_scan_diagnostic_count (
    const AtmRepositoryGcTrashScan *scan
)
{
    return scan != NULL &&
        scan->diagnostics != NULL
            ? scan->diagnostics->len
            : 0;
}

const char *
atm_repository_gc_trash_scan_diagnostic_name (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->diagnostics == NULL ||
        index >= scan->diagnostics->len) {
        return NULL;
    }

    AtmRepositoryGcTrashDiagnostic *diagnostic =
        g_ptr_array_index (
            scan->diagnostics,
            index
        );

    return diagnostic != NULL
        ? diagnostic->name
        : NULL;
}

const char *
atm_repository_gc_trash_scan_diagnostic_reason (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->diagnostics == NULL ||
        index >= scan->diagnostics->len) {
        return NULL;
    }

    AtmRepositoryGcTrashDiagnostic *diagnostic =
        g_ptr_array_index (
            scan->diagnostics,
            index
        );

    return diagnostic != NULL
        ? diagnostic->reason
        : NULL;
}
