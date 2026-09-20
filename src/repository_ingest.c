#include "repository_ingest.h"

#include "archive_extract.h"
#include "repository_manifest.h"
#include "repository_storage.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <sys/stat.h>

GQuark
atm_ingest_error_quark (void)
{
    return g_quark_from_static_string ("atm-ingest-error-quark");
}

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (path == NULL || g_lstat (path, &stat_buffer) != 0) {
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

gboolean
atm_repository_ingest_archive_cancellable (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    GCancellable *cancellable,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    GStatBuf archive_stat;
    GStatBuf staging_stat;
    char *staging_path = NULL;
    char *staging_parent = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    gboolean staging_created = FALSE;
    gboolean ok = FALSE;
    AtmArchiveLimits limits = {
        .max_entries = ATM_INGEST_MAX_ENTRIES,
        .max_file_bytes = ATM_INGEST_MAX_FILE_BYTES,
        .max_total_bytes = ATM_INGEST_MAX_TOTAL_BYTES
    };

    g_return_val_if_fail (data_root != NULL, FALSE);
    g_return_val_if_fail (archive_path != NULL, FALSE);
    g_return_val_if_fail (repository_id != NULL, FALSE);
    g_return_val_if_fail (repository_acronym != NULL, FALSE);
    g_return_val_if_fail (repository_display_name != NULL, FALSE);
    g_return_val_if_fail (sha != NULL, FALSE);
    g_return_val_if_fail (out_version != NULL, FALSE);
    g_return_val_if_fail (*out_version == NULL, FALSE);
    g_return_val_if_fail (out_snapshot_path != NULL, FALSE);
    g_return_val_if_fail (*out_snapshot_path == NULL, FALSE);

    if (out_entries != NULL) {
        *out_entries = 0;
    }

    if (out_total_bytes != NULL) {
        *out_total_bytes = 0;
    }

    if (cancellable != NULL &&
        g_cancellable_set_error_if_cancelled (cancellable, error)) {
        goto out;
    }

    if (g_lstat (archive_path, &archive_stat) != 0 ||
        !S_ISREG (archive_stat.st_mode) ||
        S_ISLNK (archive_stat.st_mode)) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_INVALID_ARCHIVE,
            "Repository archive must be a real regular file."
        );
        goto out;
    }

    staging_path = atm_repository_extraction_staging_path (
        data_root,
        repository_id,
        sha
    );

    if (staging_path == NULL) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Could not calculate repository extraction staging path."
        );
        goto out;
    }

    if (g_lstat (staging_path, &staging_stat) == 0) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_STAGING_EXISTS,
            "Repository extraction staging already exists."
        );
        goto out;
    }

    if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Could not inspect repository extraction staging: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    staging_parent = g_path_get_dirname (staging_path);
    if (g_mkdir_with_parents (staging_parent, 0700) != 0) {
        g_set_error (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Could not create repository extraction staging parent: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!atm_archive_extract_snapshot_cancellable (
            archive_path,
            staging_path,
            &limits,
            cancellable,
            &entries,
            &total_bytes,
            error
        )) {
        goto out;
    }

    staging_created = TRUE;

    if (cancellable != NULL &&
        g_cancellable_set_error_if_cancelled (cancellable, error)) {
        goto out;
    }

    if (!atm_repository_validate_snapshot (
            staging_path,
            repository_id,
            repository_acronym,
            repository_display_name,
            &version,
            error
        )) {
        goto out;
    }

    if (cancellable != NULL &&
        g_cancellable_set_error_if_cancelled (cancellable, error)) {
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

    staging_created = FALSE;

    *out_version = g_steal_pointer (&version);
    *out_snapshot_path = g_steal_pointer (&snapshot_path);

    if (out_entries != NULL) {
        *out_entries = entries;
    }

    if (out_total_bytes != NULL) {
        *out_total_bytes = total_bytes;
    }

    ok = TRUE;

out:
    if (!ok && staging_created) {
        remove_tree_best_effort (staging_path);
    }

    g_clear_pointer (&snapshot_path, g_free);
    g_clear_pointer (&version, g_free);
    g_clear_pointer (&staging_parent, g_free);
    g_clear_pointer (&staging_path, g_free);
    return ok;
}

gboolean
atm_repository_ingest_archive (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    return atm_repository_ingest_archive_cancellable (
        data_root,
        archive_path,
        repository_id,
        repository_acronym,
        repository_display_name,
        sha,
        NULL,
        out_version,
        out_snapshot_path,
        out_entries,
        out_total_bytes,
        error
    );
}
