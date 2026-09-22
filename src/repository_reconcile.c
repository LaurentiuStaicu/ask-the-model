#include "repository_reconcile.h"

#include "repository_manifest.h"
#include "retrieval_index_lifecycle.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

GQuark
atm_repository_reconcile_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-repository-reconcile-error-quark"
    );
}

static gboolean
sha_is_valid (const char *sha)
{
    if (sha == NULL || strlen (sha) != 40) {
        return FALSE;
    }

    for (gsize i = 0; i < 40; i++) {
        if (!g_ascii_isxdigit (sha[i]) ||
            (g_ascii_isalpha (sha[i]) &&
             !g_ascii_islower (sha[i]))) {
            return FALSE;
        }
    }

    return TRUE;
}

static AtmRepositoryReconcileResult *
new_result (
    AtmRepositoryReconcileStatus status,
    const char *reason_code,
    const char *detail
)
{
    AtmRepositoryReconcileResult *result =
        g_new0 (AtmRepositoryReconcileResult, 1);

    result->status = status;
    result->reason_code = g_strdup (reason_code);
    result->detail = g_strdup (detail);
    return result;
}

void
atm_repository_reconcile_result_free (
    AtmRepositoryReconcileResult *result
)
{
    if (result == NULL) {
        return;
    }

    g_free (result->reason_code);
    g_free (result->detail);
    g_free (result->repository_version);
    g_free (result->index_path);
    g_free (result);
}

static gboolean
validate_arguments (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *snapshot_sha,
    const char *persisted_version,
    GError **error
)
{
    char *basename = NULL;
    gboolean ok = FALSE;

    if (cache_root == NULL ||
        cache_root[0] == '\0' ||
        snapshot_root == NULL ||
        snapshot_root[0] == '\0' ||
        repository_id == NULL ||
        repository_id[0] == '\0' ||
        repository_acronym == NULL ||
        repository_acronym[0] == '\0' ||
        repository_display_name == NULL ||
        repository_display_name[0] == '\0' ||
        persisted_version == NULL ||
        persisted_version[0] == '\0' ||
        !sha_is_valid (snapshot_sha)) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_RECONCILE_ERROR,
            ATM_REPOSITORY_RECONCILE_ERROR_ARGUMENT,
            "Repository reconciliation received invalid identity or state arguments."
        );
        return FALSE;
    }

    basename = g_path_get_basename (snapshot_root);
    if (g_strcmp0 (basename, snapshot_sha) != 0) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_RECONCILE_ERROR,
            ATM_REPOSITORY_RECONCILE_ERROR_ARGUMENT,
            "Repository snapshot path is not bound to the persisted snapshot SHA."
        );
        goto out;
    }

    ok = TRUE;

out:
    g_free (basename);
    return ok;
}

gboolean
atm_repository_reconcile_local (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *snapshot_sha,
    const char *persisted_version,
    AtmRepositoryReconcileResult **out_result,
    GError **error
)
{
    GStatBuf stat_buffer;
    char *validated_version = NULL;
    char *index_path = NULL;
    char *index_version = NULL;
    AtmRetrievalEnsureResult ensure_result;
    GError *local_error = NULL;
    AtmRepositoryReconcileResult *result = NULL;

    g_return_val_if_fail (out_result != NULL, FALSE);
    g_return_val_if_fail (*out_result == NULL, FALSE);

    if (!validate_arguments (
            cache_root,
            snapshot_root,
            repository_id,
            repository_acronym,
            repository_display_name,
            snapshot_sha,
            persisted_version,
            error
        )) {
        return FALSE;
    }

    if (g_lstat (snapshot_root, &stat_buffer) != 0) {
        if (errno == ENOENT) {
            *out_result = new_result (
                ATM_REPOSITORY_RECONCILE_SNAPSHOT_MISSING,
                "snapshot_missing",
                "The persisted repository snapshot directory does not exist."
            );
            return TRUE;
        }

        result = new_result (
            ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID,
            "snapshot_inspection_failed",
            g_strerror (errno)
        );
        *out_result = result;
        return TRUE;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        *out_result = new_result (
            ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID,
            "snapshot_root_invalid",
            "The persisted snapshot path is not a real directory."
        );
        return TRUE;
    }

    if (!atm_repository_validate_snapshot (
            snapshot_root,
            repository_id,
            repository_acronym,
            repository_display_name,
            &validated_version,
            &local_error
        )) {
        result = new_result (
            ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID,
            "manifest_validation_failed",
            local_error != NULL
                ? local_error->message
                : "Repository snapshot validation failed."
        );
        g_clear_error (&local_error);
        *out_result = result;
        return TRUE;
    }

    if (g_strcmp0 (
            validated_version,
            persisted_version
        ) != 0) {
        result = new_result (
            ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID,
            "version_mismatch",
            "Persisted repository version does not match the validated snapshot."
        );
        result->repository_version =
            g_steal_pointer (&validated_version);
        *out_result = result;
        return TRUE;
    }

    if (!atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &index_path,
            &index_version,
            &ensure_result,
            &local_error
        )) {
        result = new_result (
            ATM_REPOSITORY_RECONCILE_INDEX_ERROR,
            "index_prepare_failed",
            local_error != NULL
                ? local_error->message
                : "Retrieval index validation or rebuild failed."
        );
        result->repository_version =
            g_steal_pointer (&validated_version);
        g_clear_error (&local_error);
        *out_result = result;
        return TRUE;
    }

    if (g_strcmp0 (
            index_version,
            validated_version
        ) != 0) {
        result = new_result (
            ATM_REPOSITORY_RECONCILE_INDEX_ERROR,
            "index_version_mismatch",
            "Retrieval index preparation returned a different repository version."
        );
        result->repository_version =
            g_steal_pointer (&validated_version);
        g_free (index_version);
        g_free (index_path);
        *out_result = result;
        return TRUE;
    }

    result = new_result (
        ensure_result == ATM_RETRIEVAL_ENSURE_REUSED
            ? ATM_REPOSITORY_RECONCILE_READY
            : ATM_REPOSITORY_RECONCILE_READY_REPAIRED_INDEX,
        ensure_result == ATM_RETRIEVAL_ENSURE_REUSED
            ? "ready"
            : "index_rebuilt",
        ensure_result == ATM_RETRIEVAL_ENSURE_REUSED
            ? "Validated snapshot and retrieval index are ready."
            : "Validated snapshot is ready and its retrieval index was rebuilt locally."
    );
    result->repository_version =
        g_steal_pointer (&validated_version);
    result->index_path = g_steal_pointer (&index_path);

    g_free (index_version);
    *out_result = result;
    return TRUE;
}


gboolean
atm_repository_reconcile_local_values (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *snapshot_sha,
    const char *persisted_version,
    gint *out_status,
    char **out_reason_code,
    char **out_detail,
    char **out_repository_version,
    char **out_index_path,
    GError **error
)
{
    AtmRepositoryReconcileResult *result = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_status != NULL, FALSE);
    g_return_val_if_fail (out_reason_code != NULL, FALSE);
    g_return_val_if_fail (*out_reason_code == NULL, FALSE);
    g_return_val_if_fail (out_detail != NULL, FALSE);
    g_return_val_if_fail (*out_detail == NULL, FALSE);
    g_return_val_if_fail (out_repository_version != NULL, FALSE);
    g_return_val_if_fail (*out_repository_version == NULL, FALSE);
    g_return_val_if_fail (out_index_path != NULL, FALSE);
    g_return_val_if_fail (*out_index_path == NULL, FALSE);

    *out_status = ATM_REPOSITORY_RECONCILE_INDEX_ERROR;

    if (!atm_repository_reconcile_local (
            cache_root,
            snapshot_root,
            repository_id,
            repository_acronym,
            repository_display_name,
            snapshot_sha,
            persisted_version,
            &result,
            error
        )) {
        goto out;
    }

    *out_status = (gint) result->status;
    *out_reason_code = g_strdup (result->reason_code);
    *out_detail = g_strdup (result->detail);
    *out_repository_version =
        g_strdup (result->repository_version);
    *out_index_path = g_strdup (result->index_path);
    ok = TRUE;

out:
    if (!ok) {
        g_clear_pointer (out_reason_code, g_free);
        g_clear_pointer (out_detail, g_free);
        g_clear_pointer (out_repository_version, g_free);
        g_clear_pointer (out_index_path, g_free);
    }

    g_clear_pointer (
        &result,
        atm_repository_reconcile_result_free
    );
    return ok;
}
