#include "retrieval_index_lifecycle.h"

#include "cff_version.h"
#include "coordination_lease.h"
#include "repository_sources.h"
#include "retrieval_index.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

GQuark
atm_retrieval_lifecycle_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-lifecycle-error-quark"
    );
}

#ifdef ATM_TEST_INDEX_SINGLE_FLIGHT
static gint test_single_flight_build_entries = 0;

void
atm_retrieval_index_single_flight_test_reset (void)
{
    g_atomic_int_set (
        &test_single_flight_build_entries,
        0
    );
}

gint
atm_retrieval_index_single_flight_test_build_entries (void)
{
    return g_atomic_int_get (
        &test_single_flight_build_entries
    );
}
#endif

static gboolean
single_flight_repository_id_is_valid (
    const char *repository_id
)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static gboolean
single_flight_snapshot_sha_is_valid (
    const char *snapshot_sha
)
{
    if (snapshot_sha == NULL ||
        strlen (snapshot_sha) != 40) {
        return FALSE;
    }

    for (gsize i = 0; i < 40; i++) {
        if (!g_ascii_isxdigit (snapshot_sha[i]) ||
            g_ascii_isupper (snapshot_sha[i])) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
require_coordination_directory (
    const char *path,
    GError **error
)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not inspect retrieval-index coordination directory: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode) ||
        stat_buffer.st_uid != geteuid ()) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Retrieval-index coordination path is not a qualified user-owned directory."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
prepare_single_flight_lock_path (
    const char *state_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_lock_path,
    GError **error
)
{
    char *lock_root = NULL;
    char *repository_root = NULL;
    char *filename = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_lock_path != NULL, FALSE);
    g_return_val_if_fail (*out_lock_path == NULL, FALSE);

    if (state_root == NULL || state_root[0] == '\0' ||
        !single_flight_repository_id_is_valid (repository_id) ||
        !single_flight_snapshot_sha_is_valid (snapshot_sha)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Retrieval-index coordination identity is invalid."
        );
        goto out;
    }

    lock_root = g_build_filename (
        state_root,
        "retrieval-index-locks",
        NULL
    );
    repository_root = g_build_filename (
        lock_root,
        repository_id,
        NULL
    );

    if (g_mkdir_with_parents (
            repository_root,
            0700
        ) != 0) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not create retrieval-index coordination directory: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!require_coordination_directory (
            lock_root,
            error
        ) ||
        !require_coordination_directory (
            repository_root,
            error
        )) {
        goto out;
    }

    filename = g_strdup_printf (
        "%s.lock",
        snapshot_sha
    );
    *out_lock_path = g_build_filename (
        repository_root,
        filename,
        NULL
    );
    ok = TRUE;

out:
    g_free (filename);
    g_free (repository_root);
    g_free (lock_root);
    return ok;
}

static gboolean
try_reuse_valid_index (
    const char *index_path,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    gboolean *out_reused,
    GError **error
)
{
    GStatBuf stat_buffer;
    GError *validation_error = NULL;

    g_return_val_if_fail (out_reused != NULL, FALSE);
    *out_reused = FALSE;

    if (g_lstat (index_path, &stat_buffer) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }

        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not inspect retrieval index cache path: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (!S_ISREG (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        return TRUE;
    }

    if (atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &validation_error
        )) {
        *out_reused = TRUE;
        return TRUE;
    }

    g_clear_error (&validation_error);
    return TRUE;
}

static gboolean
remove_abandoned_staging (
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
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not inspect retrieval-index staging residue: %s.",
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
            "Refusing to remove retrieval-index staging that is not a real regular file."
        );
        return FALSE;
    }

    if (g_remove (staging_path) != 0) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not remove abandoned retrieval-index staging: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    return TRUE;
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

gboolean
atm_retrieval_index_ensure_for_snapshot_coordinated (
    const char *state_root,
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
    char *lock_path = NULL;
    char *staging_path = NULL;
    gint lease_fd = -1;
    gboolean contended = FALSE;
    gint64 wait_us = 0;
    gboolean reused = FALSE;
    gboolean ok = FALSE;
    GError *lease_error = NULL;

    g_return_val_if_fail (state_root != NULL, FALSE);
    g_return_val_if_fail (cache_root != NULL, FALSE);
    g_return_val_if_fail (snapshot_root != NULL, FALSE);
    g_return_val_if_fail (repository_id != NULL, FALSE);
    g_return_val_if_fail (snapshot_sha != NULL, FALSE);
    g_return_val_if_fail (out_index_path != NULL, FALSE);
    g_return_val_if_fail (*out_index_path == NULL, FALSE);
    g_return_val_if_fail (out_repository_version != NULL, FALSE);
    g_return_val_if_fail (*out_repository_version == NULL, FALSE);
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!single_flight_repository_id_is_valid (
            repository_id
        ) ||
        !single_flight_snapshot_sha_is_valid (
            snapshot_sha
        )) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Retrieval-index single-flight identity is invalid."
        );
        goto out;
    }

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

    if (!try_reuse_valid_index (
            index_path,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &reused,
            error
        )) {
        goto out;
    }

    if (reused) {
        *out_index_path =
            g_steal_pointer (&index_path);
        *out_repository_version =
            g_steal_pointer (&version);
        *out_result = ATM_RETRIEVAL_ENSURE_REUSED;
        ok = TRUE;
        goto out;
    }

    if (!prepare_single_flight_lock_path (
            state_root,
            repository_id,
            snapshot_sha,
            &lock_path,
            error
        )) {
        goto out;
    }

    if (!atm_coordination_lease_acquire (
            lock_path,
            FALSE,
            &lease_fd,
            &contended,
            &wait_us,
            &lease_error
        )) {
        g_set_error (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Could not join retrieval-index build flight: %s",
            lease_error != NULL
                ? lease_error->message
                : "coordination lease failed"
        );
        goto out;
    }

    if (contended || lease_fd < 0) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_LIFECYCLE_ERROR,
            ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
            "Retrieval-index build flight returned no exclusive lease."
        );
        goto out;
    }

    g_debug (
        "AtM: retrieval-index single-flight repo=%s sha=%s wait_us=%" G_GINT64_FORMAT,
        repository_id,
        snapshot_sha,
        wait_us
    );

    reused = FALSE;
    if (!try_reuse_valid_index (
            index_path,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &reused,
            error
        )) {
        goto out;
    }

    if (reused) {
        *out_index_path =
            g_steal_pointer (&index_path);
        *out_repository_version =
            g_steal_pointer (&version);
        *out_result = ATM_RETRIEVAL_ENSURE_REUSED;
        ok = TRUE;
        goto out;
    }

    if (!remove_invalid_index (
            index_path,
            error
        )) {
        goto out;
    }

    staging_path =
        atm_retrieval_index_staging_path (
            cache_root,
            repository_id,
            snapshot_sha
        );

    if (!remove_abandoned_staging (
            staging_path,
            error
        )) {
        goto out;
    }

#ifdef ATM_TEST_INDEX_SINGLE_FLIGHT
    g_atomic_int_inc (
        &test_single_flight_build_entries
    );
#endif

    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    if (!atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            repository_id,
            snapshot_sha,
            out_index_path,
            out_repository_version,
            out_result,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    if (lease_fd >= 0) {
        atm_coordination_lease_release (
            lease_fd
        );
    }

    g_clear_error (&lease_error);
    g_free (staging_path);
    g_free (lock_path);
    g_free (version);
    g_free (index_path);
    return ok;
}

