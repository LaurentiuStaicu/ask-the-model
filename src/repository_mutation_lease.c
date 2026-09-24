#define _GNU_SOURCE

#include "repository_mutation_lease.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

GQuark
atm_repository_mutation_lease_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-repository-mutation-lease-error-quark"
    );
}

static void
set_errno_error (
    GError **error,
    AtmRepositoryMutationLeaseError code,
    const char *context
)
{
    g_set_error (
        error,
        ATM_REPOSITORY_MUTATION_LEASE_ERROR,
        code,
        "%s: %s",
        context,
        g_strerror (errno)
    );
}

gboolean
atm_repository_mutation_lease_try_acquire (
    const char *state_root,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
)
{
    char *path = NULL;
    struct stat opened_stat;
    struct stat path_stat;
    gint fd = -1;
    gboolean ok = FALSE;

    g_return_val_if_fail (state_root != NULL, FALSE);
    g_return_val_if_fail (state_root[0] != '\0', FALSE);
    g_return_val_if_fail (out_fd != NULL, FALSE);
    g_return_val_if_fail (out_contended != NULL, FALSE);

    *out_fd = -1;
    *out_contended = FALSE;

    path = g_build_filename (
        state_root,
        "repository-mutation.lock",
        NULL
    );

    fd = open (
        path,
        O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
        0600
    );

    if (fd < 0) {
        set_errno_error (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO,
            "Could not open repository mutation lease"
        );
        goto out;
    }

    if (fstat (fd, &opened_stat) != 0) {
        set_errno_error (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO,
            "Could not inspect repository mutation lease"
        );
        goto out;
    }

    if (!S_ISREG (opened_stat.st_mode) ||
        opened_stat.st_uid != geteuid () ||
        opened_stat.st_nlink != 1) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR_INVALID_OBJECT,
            "Repository mutation lease path is not a qualified user-owned regular file."
        );
        goto out;
    }

    if (lstat (path, &path_stat) != 0) {
        set_errno_error (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO,
            "Could not revalidate repository mutation lease path"
        );
        goto out;
    }

    if (!S_ISREG (path_stat.st_mode) ||
        S_ISLNK (path_stat.st_mode) ||
        path_stat.st_dev != opened_stat.st_dev ||
        path_stat.st_ino != opened_stat.st_ino) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR_INVALID_OBJECT,
            "Repository mutation lease path changed during qualification."
        );
        goto out;
    }

    if (flock (fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            *out_contended = TRUE;
            ok = TRUE;
            goto out;
        }

        set_errno_error (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO,
            "Could not acquire repository mutation lease"
        );
        goto out;
    }

    *out_fd = fd;
    fd = -1;
    ok = TRUE;

out:
    if (fd >= 0) {
        close (fd);
    }

    g_free (path);
    return ok;
}

void
atm_repository_mutation_lease_release (
    gint fd
)
{
    if (fd < 0) {
        return;
    }

    (void) flock (fd, LOCK_UN);
    (void) close (fd);
}
