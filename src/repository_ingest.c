#include "repository_ingest.h"

#include "archive_extract.h"
#include "repository_manifest.h"
#include "repository_storage.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

GQuark
atm_ingest_error_quark (void)
{
    return g_quark_from_static_string ("atm-ingest-error-quark");
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

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) || S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);
    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static gboolean
prepare_staging_parent (
    const char *staging_path,
    GError **error
)
{
    char *parent = g_path_get_dirname (staging_path);
    gboolean ok = TRUE;

    if (g_mkdir_with_parents (parent, 0700) != 0) {
        g_set_error (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_STAGING,
            "Could not create repository extraction staging parent: %s.",
            g_strerror (errno)
        );
        ok = FALSE;
    }

    g_free (parent);
    return ok;
}

static gboolean
clear_exact_staging_for_retry (
    const char *staging_path,
    GError **error
)
{
    GStatBuf stat_buffer;

    if (g_lstat (staging_path, &stat_buffer) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }

        g_set_error (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_STAGING,
            "Could not inspect repository extraction staging: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (stat_buffer.st_mode) || S_ISLNK (stat_buffer.st_mode)) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_STAGING,
            "Repository extraction staging exists but is not a real directory."
        );
        return FALSE;
    }

    remove_tree_best_effort (staging_path);

    if (g_lstat (staging_path, &stat_buffer) == 0 || errno != ENOENT) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_STAGING,
            "Repository extraction staging could not be cleared for retry."
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_repository_ingest_archive (
    const char *archive_path,
    const char *data_root,
    const char *repository_id,
    const char *expected_acronym,
    const char *expected_display_name,
    const char *sha,
    char **out_version,
    char **out_snapshot_path,
    GError **error
)
{
    AtmArchiveLimits limits = {
        .max_entries = ATM_REPOSITORY_MAX_ARCHIVE_ENTRIES,
        .max_file_bytes = ATM_REPOSITORY_MAX_FILE_BYTES,
        .max_total_bytes = ATM_REPOSITORY_MAX_TOTAL_BYTES
    };
    char *staging_path = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    gboolean promoted = FALSE;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_version != NULL, FALSE);
    g_return_val_if_fail (*out_version == NULL, FALSE);
    g_return_val_if_fail (out_snapshot_path != NULL, FALSE);
    g_return_val_if_fail (*out_snapshot_path == NULL, FALSE);

    if (archive_path == NULL ||
        data_root == NULL ||
        expected_acronym == NULL ||
        expected_display_name == NULL ||
        !repository_id_is_valid (repository_id) ||
        !sha_is_valid (sha)) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_INVALID_ARGUMENT,
            "Repository ingestion received an invalid catalog identity, SHA or path."
        );
        return FALSE;
    }

    GStatBuf archive_stat;
    if (g_lstat (archive_path, &archive_stat) != 0 ||
        !S_ISREG (archive_stat.st_mode) ||
        S_ISLNK (archive_stat.st_mode)) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_INVALID_ARGUMENT,
            "Repository ingestion archive must be a real regular file."
        );
        return FALSE;
    }

    staging_path = atm_repository_extraction_staging_path (
        data_root,
        repository_id,
        sha
    );

    if (!prepare_staging_parent (staging_path, error) ||
        !clear_exact_staging_for_retry (staging_path, error)) {
        goto out;
    }

    if (!atm_archive_extract_snapshot (
            archive_path,
            staging_path,
            &limits,
            NULL,
            NULL,
            error
        )) {
        goto out;
    }

    if (!atm_repository_validate_snapshot (
            staging_path,
            repository_id,
            expected_acronym,
            expected_display_name,
            &version,
            error
        )) {
        goto out;
    }

    if (!atm_repository_promote_snapshot (
            data_root,
            repository_id,
            sha,
            staging_path,
            &snapshot_path,
            error
        )) {
        goto out;
    }

    promoted = TRUE;
    *out_version = g_steal_pointer (&version);
    *out_snapshot_path = g_steal_pointer (&snapshot_path);
    ok = TRUE;

out:
    if (!promoted && staging_path != NULL) {
        remove_tree_best_effort (staging_path);
    }

    g_clear_pointer (&snapshot_path, g_free);
    g_clear_pointer (&version, g_free);
    g_clear_pointer (&staging_path, g_free);
    return ok;
}
