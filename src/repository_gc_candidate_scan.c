#define _GNU_SOURCE

#include "repository_gc_candidate_scan.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *name;
    char *reason;
} AtmRepositoryGcCandidateDiagnostic;

struct AtmRepositoryGcCandidateScan {
    GPtrArray *candidate_shas;
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

static void
diagnostic_free (
    gpointer data
)
{
    AtmRepositoryGcCandidateDiagnostic *diagnostic =
        data;

    if (diagnostic == NULL) {
        return;
    }

    g_free (diagnostic->name);
    g_free (diagnostic->reason);
    g_free (diagnostic);
}

static AtmRepositoryGcCandidateScan *
scan_new (void)
{
    AtmRepositoryGcCandidateScan *scan =
        g_new0 (
            AtmRepositoryGcCandidateScan,
            1
        );

    scan->candidate_shas =
        g_ptr_array_new_with_free_func (
            g_free
        );
    scan->diagnostics =
        g_ptr_array_new_with_free_func (
            diagnostic_free
        );
    return scan;
}

void
atm_repository_gc_candidate_scan_free (
    AtmRepositoryGcCandidateScan *scan
)
{
    if (scan == NULL) {
        return;
    }

    g_clear_pointer (
        &scan->candidate_shas,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &scan->diagnostics,
        g_ptr_array_unref
    );
    g_free (scan);
}

static gint
compare_string_ptrs (
    gconstpointer a,
    gconstpointer b
)
{
    const char *left =
        *(const char * const *) a;
    const char *right =
        *(const char * const *) b;

    return g_strcmp0 (
        left,
        right
    );
}

static gint
compare_diagnostic_ptrs (
    gconstpointer a,
    gconstpointer b
)
{
    const AtmRepositoryGcCandidateDiagnostic *left =
        *(AtmRepositoryGcCandidateDiagnostic * const *) a;
    const AtmRepositoryGcCandidateDiagnostic *right =
        *(AtmRepositoryGcCandidateDiagnostic * const *) b;

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
add_diagnostic (
    AtmRepositoryGcCandidateScan *scan,
    const char *name,
    const char *reason
)
{
    AtmRepositoryGcCandidateDiagnostic *diagnostic =
        g_new0 (
            AtmRepositoryGcCandidateDiagnostic,
            1
        );

    diagnostic->name = g_strdup (
        name != NULL ? name : ""
    );
    diagnostic->reason = g_strdup (
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
    g_return_val_if_fail (out_fd != NULL, FALSE);
    g_return_val_if_fail (out_missing != NULL, FALSE);

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
            "Could not open GC snapshot namespace component '%s' without following links: %s",
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
            "Could not inspect GC snapshot namespace component '%s': %s",
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
            "GC snapshot namespace component '%s' is not a real directory.",
            name
        );
        return FALSE;
    }

    *out_fd = fd;
    return TRUE;
}

static gboolean
scan_snapshot_directory (
    int snapshots_fd,
    AtmRepositoryGcCandidateScan *scan,
    GError **error
)
{
    int directory_fd = dup (snapshots_fd);

    if (directory_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not duplicate GC snapshot directory descriptor: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    DIR *directory = fdopendir (
        directory_fd
    );

    if (directory == NULL) {
        int saved_errno = errno;

        close (directory_fd);
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (saved_errno),
            "Could not enumerate GC snapshot directory: %s",
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
                    "Could not continue GC snapshot directory enumeration: %s",
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

        if (!sha40_lower_is_valid (
                item->d_name
            )) {
            add_diagnostic (
                scan,
                item->d_name,
                "unexpected-basename"
            );
            continue;
        }

        struct stat st;

        if (fstatat (
                snapshots_fd,
                item->d_name,
                &st,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            int saved_errno = errno;

            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (saved_errno),
                "Could not inspect GC snapshot candidate '%s': %s",
                item->d_name,
                g_strerror (saved_errno)
            );
            goto done;
        }

        if (!S_ISDIR (st.st_mode) ||
            S_ISLNK (st.st_mode)) {
            add_diagnostic (
                scan,
                item->d_name,
                "not-real-directory"
            );
            continue;
        }

        g_ptr_array_add (
            scan->candidate_shas,
            g_strdup (
                item->d_name
            )
        );
    }

    g_ptr_array_sort (
        scan->candidate_shas,
        compare_string_ptrs
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
atm_repository_gc_candidate_scan (
    const char *data_root,
    const char *repository_id,
    AtmRepositoryGcCandidateScan **out_scan,
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
            "GC candidate scan received invalid arguments."
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
            "Could not open GC data root without following links: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    AtmRepositoryGcCandidateScan *scan =
        scan_new ();
    int repositories_fd = -1;
    int repository_fd = -1;
    int snapshots_fd = -1;
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
            repository_id,
            TRUE,
            &repository_fd,
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
            repository_fd,
            "snapshots",
            TRUE,
            &snapshots_fd,
            &missing,
            error
        )) {
        goto done;
    }

    if (missing) {
        ok = TRUE;
        goto done;
    }

    ok = scan_snapshot_directory (
        snapshots_fd,
        scan,
        error
    );

done:
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

    if (!ok) {
        atm_repository_gc_candidate_scan_free (
            scan
        );
        return FALSE;
    }

    *out_scan = scan;
    return TRUE;
}

gsize
atm_repository_gc_candidate_scan_candidate_count (
    const AtmRepositoryGcCandidateScan *scan
)
{
    return scan != NULL &&
        scan->candidate_shas != NULL
            ? scan->candidate_shas->len
            : 0;
}

const char *
atm_repository_gc_candidate_scan_candidate_sha (
    const AtmRepositoryGcCandidateScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->candidate_shas == NULL ||
        index >= scan->candidate_shas->len) {
        return NULL;
    }

    return g_ptr_array_index (
        scan->candidate_shas,
        index
    );
}

gsize
atm_repository_gc_candidate_scan_diagnostic_count (
    const AtmRepositoryGcCandidateScan *scan
)
{
    return scan != NULL &&
        scan->diagnostics != NULL
            ? scan->diagnostics->len
            : 0;
}

const char *
atm_repository_gc_candidate_scan_diagnostic_name (
    const AtmRepositoryGcCandidateScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->diagnostics == NULL ||
        index >= scan->diagnostics->len) {
        return NULL;
    }

    AtmRepositoryGcCandidateDiagnostic *diagnostic =
        g_ptr_array_index (
            scan->diagnostics,
            index
        );

    return diagnostic != NULL
        ? diagnostic->name
        : NULL;
}

const char *
atm_repository_gc_candidate_scan_diagnostic_reason (
    const AtmRepositoryGcCandidateScan *scan,
    gsize index
)
{
    if (scan == NULL ||
        scan->diagnostics == NULL ||
        index >= scan->diagnostics->len) {
        return NULL;
    }

    AtmRepositoryGcCandidateDiagnostic *diagnostic =
        g_ptr_array_index (
            scan->diagnostics,
            index
        );

    return diagnostic != NULL
        ? diagnostic->reason
        : NULL;
}
