#include "retrieval_index_lifecycle.h"

#include "cff_version.h"
#include "repository_sources.h"
#include "retrieval_index.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <sys/stat.h>

GQuark
atm_retrieval_lifecycle_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-lifecycle-error-quark"
    );
}

static gboolean
read_snapshot_version (
    const char *snapshot_root,
    char **out_version,
    GError **error
)
{
    char *path = g_build_filename (
        snapshot_root,
        "CITATION.cff",
        NULL
    );
    char *contents = NULL;
    gsize length = 0;
    GError *local_error = NULL;
    gboolean ok = FALSE;

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_VERSION,
            "Could not read snapshot CITATION.cff: %s",
            local_error != NULL
                ? local_error->message
                : "unknown file error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    if (!atm_cff_extract_version (
            (const guint8 *) contents,
            length,
            out_version,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_VERSION,
            "Could not read snapshot repository version: %s",
            local_error != NULL
                ? local_error->message
                : "unknown CFF error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    ok = TRUE;

out:
    g_free (contents);
    g_free (path);
    return ok;
}

static gboolean
remove_invalid_index (
    const char *index_path,
    GError **error
)
{
    GStatBuf stat_buffer;

    if (g_lstat (index_path, &stat_buffer) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }

        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not inspect invalid retrieval index: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISREG (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Refusing to replace a retrieval-index cache path that is not a real regular file."
        );
        return FALSE;
    }

    if (g_remove (index_path) != 0) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not remove invalid retrieval index: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_retrieval_index_ensure_for_snapshot (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_index_path,
    char **out_repository_version,
    AtmRetrievalEnsureResult *out_result,
    GError **error
)
{
    char *index_path = NULL;
    char *version = NULL;
    AtmSourceCatalog *catalog = NULL;
    GDateTime *now = NULL;
    char *created_at_utc = NULL;
    AtmRetrievalIndexMetadata metadata = { 0 };
    GStatBuf stat_buffer;
    GError *validation_error = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (cache_root != NULL, FALSE);
    g_return_val_if_fail (snapshot_root != NULL, FALSE);
    g_return_val_if_fail (repository_id != NULL, FALSE);
    g_return_val_if_fail (snapshot_sha != NULL, FALSE);
    g_return_val_if_fail (out_index_path != NULL, FALSE);
    g_return_val_if_fail (*out_index_path == NULL, FALSE);
    g_return_val_if_fail (out_repository_version != NULL, FALSE);
    g_return_val_if_fail (*out_repository_version == NULL, FALSE);
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!read_snapshot_version (
            snapshot_root,
            &version,
            error
        )) {
        goto out;
    }

    index_path = atm_retrieval_index_path (
        cache_root,
        repository_id,
        snapshot_sha
    );

    if (g_lstat (index_path, &stat_buffer) == 0) {
        if (S_ISREG (stat_buffer.st_mode) &&
            !S_ISLNK (stat_buffer.st_mode) &&
            atm_retrieval_index_validate_snapshot_sources (
                index_path,
                snapshot_root,
                repository_id,
                snapshot_sha,
                &validation_error
            )) {
            *out_index_path = g_steal_pointer (&index_path);
            *out_repository_version = g_steal_pointer (&version);
            *out_result = ATM_RETRIEVAL_ENSURE_REUSED;
            ok = TRUE;
            goto out;
        }

        g_clear_error (&validation_error);

        if (!remove_invalid_index (
                index_path,
                error
            )) {
            goto out;
        }
    } else if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not inspect retrieval index cache path: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!atm_repository_source_catalog_build (
            snapshot_root,
            repository_id,
            &catalog,
            error
        )) {
        goto out;
    }

    now = g_date_time_new_now_utc ();
    created_at_utc = g_date_time_format_iso8601 (now);

    metadata.repository_id = repository_id;
    metadata.repository_version = version;
    metadata.snapshot_sha = snapshot_sha;
    metadata.manifest_schema_version = 1;
    metadata.manifest_sha256 = catalog->manifest_sha256;
    metadata.created_at_utc = created_at_utc;

    g_clear_pointer (&index_path, g_free);

    if (!atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            error
        )) {
        goto out;
    }

    if (!atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            repository_id,
            snapshot_sha,
            error
        )) {
        g_remove (index_path);
        goto out;
    }

    *out_index_path = g_steal_pointer (&index_path);
    *out_repository_version = g_steal_pointer (&version);
    *out_result = ATM_RETRIEVAL_ENSURE_REBUILT;
    ok = TRUE;

out:
    g_clear_error (&validation_error);
    g_clear_pointer (&created_at_utc, g_free);
    g_clear_pointer (&now, g_date_time_unref);
    g_clear_pointer (&catalog, atm_source_catalog_free);
    g_clear_pointer (&version, g_free);
    g_clear_pointer (&index_path, g_free);
    return ok;
}
