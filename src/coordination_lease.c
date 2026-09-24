#define _GNU_SOURCE

#include "coordination_lease.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

GQuark
atm_coordination_lease_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-coordination-lease-error-quark"
    );
}

static void
set_errno_error (
    GError **error,
    AtmCoordinationLeaseError code,
    const char *context
)
{
    g_set_error (
        error,
        ATM_COORDINATION_LEASE_ERROR,
        code,
        "%s: %s",
        context,
        g_strerror (errno)
    );
}

static gboolean
path_matches_opened_file (
    const char *path,
    const struct stat *opened_stat,
    const char *context,
    GError **error
)
{
    struct stat path_stat;

    if (lstat (path, &path_stat) != 0) {
        set_errno_error (
            error,
            ATM_COORDINATION_LEASE_ERROR_IO,
            context
        );
        return FALSE;
    }

    if (!S_ISREG (path_stat.st_mode) ||
        S_ISLNK (path_stat.st_mode) ||
        path_stat.st_dev != opened_stat->st_dev ||
        path_stat.st_ino != opened_stat->st_ino) {
        g_set_error_literal (
            error,
            ATM_COORDINATION_LEASE_ERROR,
            ATM_COORDINATION_LEASE_ERROR_INVALID_OBJECT,
            "Coordination lease path does not identify the qualified opened file."
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_coordination_lease_acquire (
    const char *path,
    gboolean nonblocking,
    gint *out_fd,
    gboolean *out_contended,
    gint64 *out_wait_us,
    GError **error
)
{
    struct stat opened_stat;
    gint fd = -1;
    gint operation = LOCK_EX;
    gint64 wait_started_us;
    gboolean ok = FALSE;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (path[0] != '\0', FALSE);
    g_return_val_if_fail (out_fd != NULL, FALSE);
    g_return_val_if_fail (out_contended != NULL, FALSE);
    g_return_val_if_fail (out_wait_us != NULL, FALSE);

    *out_fd = -1;
    *out_contended = FALSE;
    *out_wait_us = 0;

    fd = open (
        path,
        O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
        0600
    );

    if (fd < 0) {
        set_errno_error (
            error,
            ATM_COORDINATION_LEASE_ERROR_IO,
            "Could not open coordination lease"
        );
        goto out;
    }

    if (fstat (fd, &opened_stat) != 0) {
        set_errno_error (
            error,
            ATM_COORDINATION_LEASE_ERROR_IO,
            "Could not inspect coordination lease"
        );
        goto out;
    }

    if (!S_ISREG (opened_stat.st_mode) ||
        opened_stat.st_uid != geteuid () ||
        opened_stat.st_nlink != 1) {
        g_set_error_literal (
            error,
            ATM_COORDINATION_LEASE_ERROR,
            ATM_COORDINATION_LEASE_ERROR_INVALID_OBJECT,
            "Coordination lease path is not a qualified user-owned regular file."
        );
        goto out;
    }

    if (!path_matches_opened_file (
            path,
            &opened_stat,
            "Could not validate coordination lease path",
            error
        )) {
        goto out;
    }

    if (nonblocking) {
        operation |= LOCK_NB;
    }

    wait_started_us = g_get_monotonic_time ();

    for (;;) {
        if (flock (fd, operation) == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        *out_wait_us =
            g_get_monotonic_time () - wait_started_us;

        if (nonblocking &&
            (errno == EWOULDBLOCK || errno == EAGAIN)) {
            *out_contended = TRUE;
            ok = TRUE;
            goto out;
        }

        set_errno_error (
            error,
            ATM_COORDINATION_LEASE_ERROR_IO,
            "Could not acquire coordination lease"
        );
        goto out;
    }

    *out_wait_us =
        g_get_monotonic_time () - wait_started_us;

    if (!path_matches_opened_file (
            path,
            &opened_stat,
            "Could not revalidate acquired coordination lease path",
            error
        )) {
        goto out;
    }

    *out_fd = fd;
    fd = -1;
    ok = TRUE;

out:
    if (fd >= 0) {
        close (fd);
    }

    return ok;
}

void
atm_coordination_lease_release (
    gint fd
)
{
    if (fd < 0) {
        return;
    }

    (void) flock (fd, LOCK_UN);
    (void) close (fd);
}
