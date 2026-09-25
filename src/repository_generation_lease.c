#include "repository_generation_lease.h"

#include "coordination_lease.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

GQuark
atm_repository_generation_lease_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-repository-generation-lease-error-quark"
    );
}

static AtmRepositoryGenerationLeaseError
map_coordination_error (
    const GError *error
)
{
    if (error != NULL &&
        error->domain == ATM_COORDINATION_LEASE_ERROR &&
        error->code ==
            ATM_COORDINATION_LEASE_ERROR_INVALID_OBJECT) {
        return ATM_REPOSITORY_GENERATION_LEASE_ERROR_INVALID_OBJECT;
    }

    return ATM_REPOSITORY_GENERATION_LEASE_ERROR_IO;
}

static gboolean
require_lease_directory (
    const char *path,
    GError **error
)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        g_set_error (
            error,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR_IO,
            "Could not inspect repository-generation lease directory: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode) ||
        stat_buffer.st_uid != geteuid () ||
        (stat_buffer.st_mode &
         (S_IWGRP | S_IWOTH)) != 0) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR_INVALID_OBJECT,
            "Repository-generation lease directory is not a qualified user-owned directory."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
ensure_lease_directory (
    const char *state_root,
    char **out_root,
    GError **error
)
{
    char *root;

    g_return_val_if_fail (out_root != NULL, FALSE);
    g_return_val_if_fail (*out_root == NULL, FALSE);

    root = g_build_filename (
        state_root,
        "repository-generation-leases",
        NULL
    );

    if (g_mkdir (root, 0700) != 0 &&
        errno != EEXIST) {
        g_set_error (
            error,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR_IO,
            "Could not create repository-generation lease directory: %s.",
            g_strerror (errno)
        );
        g_free (root);
        return FALSE;
    }

    if (!require_lease_directory (
            root,
            error
        )) {
        g_free (root);
        return FALSE;
    }

    *out_root = root;
    return TRUE;
}

static gboolean
acquire_generation_lease (
    const char *state_root,
    gint64 generation_id,
    AtmCoordinationLeaseMode mode,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
)
{
    char *root = NULL;
    char *filename = NULL;
    char *path = NULL;
    GError *local_error = NULL;
    gint64 wait_us = 0;
    gboolean ok = FALSE;

    g_return_val_if_fail (state_root != NULL, FALSE);
    g_return_val_if_fail (state_root[0] != '\0', FALSE);
    g_return_val_if_fail (out_fd != NULL, FALSE);
    g_return_val_if_fail (out_contended != NULL, FALSE);

    *out_fd = -1;
    *out_contended = FALSE;

    if (generation_id <= 0) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR_INVALID_GENERATION,
            "Repository-generation lease requires a positive generation identifier."
        );
        return FALSE;
    }

    if (!ensure_lease_directory (
            state_root,
            &root,
            error
        )) {
        return FALSE;
    }

    filename = g_strdup_printf (
        "%" G_GINT64_FORMAT ".lock",
        generation_id
    );
    path = g_build_filename (
        root,
        filename,
        NULL
    );

    ok = atm_coordination_lease_acquire_mode (
        path,
        mode,
        TRUE,
        out_fd,
        out_contended,
        &wait_us,
        &local_error
    );

    if (!ok) {
        g_set_error (
            error,
            ATM_REPOSITORY_GENERATION_LEASE_ERROR,
            map_coordination_error (local_error),
            "%s",
            local_error != NULL
                ? local_error->message
                : "Repository-generation coordination failed."
        );
    }

    g_clear_error (&local_error);
    g_free (path);
    g_free (filename);
    g_free (root);
    return ok;
}

gboolean
atm_repository_generation_lease_try_acquire_shared (
    const char *state_root,
    gint64 generation_id,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
)
{
    return acquire_generation_lease (
        state_root,
        generation_id,
        ATM_COORDINATION_LEASE_MODE_SHARED,
        out_fd,
        out_contended,
        error
    );
}

gboolean
atm_repository_generation_lease_try_acquire_exclusive (
    const char *state_root,
    gint64 generation_id,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
)
{
    return acquire_generation_lease (
        state_root,
        generation_id,
        ATM_COORDINATION_LEASE_MODE_EXCLUSIVE,
        out_fd,
        out_contended,
        error
    );
}

void
atm_repository_generation_lease_release (
    gint fd
)
{
    atm_coordination_lease_release (fd);
}
