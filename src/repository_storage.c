#include "repository_storage.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

GQuark
atm_storage_error_quark (void)
{
    return g_quark_from_static_string ("atm-storage-error-quark");
}

static gboolean
repository_id_is_valid (const char *repository_id)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static gboolean
sha_is_valid (const char *sha)
{
    if (sha == NULL || strlen (sha) != 40) {
        return FALSE;
    }

    for (gsize i = 0; i < 40; i++) {
        if (!g_ascii_isxdigit (sha[i])) {
            return FALSE;
        }
    }

    return TRUE;
}

char *
atm_repository_snapshot_path (
    const char *data_root,
    const char *repository_id,
    const char *sha
)
{
    g_return_val_if_fail (data_root != NULL, NULL);
    g_return_val_if_fail (repository_id != NULL, NULL);
    g_return_val_if_fail (sha != NULL, NULL);

    return g_build_filename (
        data_root,
        "repositories",
        repository_id,
        "snapshots",
        sha,
        NULL
    );
}

char *
atm_repository_extraction_staging_path (
    const char *data_root,
    const char *repository_id,
    const char *sha
)
{
    char *name;

    g_return_val_if_fail (data_root != NULL, NULL);
    g_return_val_if_fail (repository_id != NULL, NULL);
    g_return_val_if_fail (sha != NULL, NULL);

    name = g_strdup_printf ("%s.part", sha);

    char *path = g_build_filename (
        data_root,
        "repository-staging",
        repository_id,
        name,
        NULL
    );

    g_free (name);
    return path;
}

gboolean
atm_repository_promote_snapshot (
    const char *data_root,
    const char *repository_id,
    const char *sha,
    const char *staging_path,
    char **out_snapshot_path,
    GError **error
)
{
    char *expected_staging = NULL;
    char *snapshot_path = NULL;
    char *snapshot_parent = NULL;
    GStatBuf stat_buffer;
    gboolean ok = FALSE;

    g_return_val_if_fail (data_root != NULL, FALSE);
    g_return_val_if_fail (repository_id != NULL, FALSE);
    g_return_val_if_fail (sha != NULL, FALSE);
    g_return_val_if_fail (staging_path != NULL, FALSE);
    g_return_val_if_fail (out_snapshot_path != NULL, FALSE);
    g_return_val_if_fail (*out_snapshot_path == NULL, FALSE);

    if (!repository_id_is_valid (repository_id)) {
        g_set_error_literal (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_INVALID_ID,
            "Repository ID is not part of the v1 catalog."
        );
        goto out;
    }

    if (!sha_is_valid (sha)) {
        g_set_error_literal (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_INVALID_SHA,
            "Snapshot SHA must contain exactly 40 hexadecimal characters."
        );
        goto out;
    }

    expected_staging = atm_repository_extraction_staging_path (
        data_root,
        repository_id,
        sha
    );

    if (g_strcmp0 (staging_path, expected_staging) != 0) {
        g_set_error_literal (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_INVALID_STAGING,
            "Snapshot promotion source is not the expected AtM staging path."
        );
        goto out;
    }

    if (g_lstat (staging_path, &stat_buffer) != 0 ||
        !S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_set_error_literal (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_INVALID_STAGING,
            "Snapshot staging path must be a real directory."
        );
        goto out;
    }

    snapshot_path = atm_repository_snapshot_path (
        data_root,
        repository_id,
        sha
    );

    if (g_lstat (snapshot_path, &stat_buffer) == 0) {
        g_set_error_literal (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_EXISTS,
            "A snapshot for this repository and SHA already exists."
        );
        goto out;
    }

    if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_IO,
            "Could not inspect final snapshot path: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    snapshot_parent = g_path_get_dirname (snapshot_path);
    if (g_mkdir_with_parents (snapshot_parent, 0700) != 0) {
        g_set_error (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_IO,
            "Could not create snapshot parent directory: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (g_rename (staging_path, snapshot_path) != 0) {
        g_set_error (
            error,
            ATM_STORAGE_ERROR,
            ATM_STORAGE_ERROR_IO,
            "Could not atomically promote validated snapshot: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    *out_snapshot_path = g_steal_pointer (&snapshot_path);
    ok = TRUE;

out:
    g_clear_pointer (&snapshot_parent, g_free);
    g_clear_pointer (&snapshot_path, g_free);
    g_clear_pointer (&expected_staging, g_free);
    return ok;
}
