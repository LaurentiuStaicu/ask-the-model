#include "repository_ingest.h"

#include "archive_extract.h"
#include "fault_injection_test_hook.h"
#include "repository_manifest.h"
#include "repository_storage.h"
#include "snapshot_durability.h"
#include "snapshot_namespace_durability.h"
#include "snapshot_seal.h"

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


/*
 * The EWD GISTEMP history pin predates AtM's repository-manifest contract.
 * Its scientific source bundle is still independently admitted, but its
 * upstream tree has no .atm/repository.json. Add a versioned, AtM-owned
 * metadata overlay for this exact repository/SHA only. The overlay receipt
 * is sealed with the rest of the local snapshot and states plainly that the
 * manifest is not part of the upstream commit. No fallback exists for any
 * other SHA or repository.
 */
static const char ATM_EWD_D9_SHA[] =
    "d9e249339663015f6d1c05752338a955bf64ad0b";

static const char ATM_EWD_D9_MANIFEST[] =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"repository_id\": \"ewd\",\n"
    "  \"acronym\": \"EWD\",\n"
    "  \"display_name\": \"Empirical World3 Dynamics\",\n"
    "  \"version_source\": {\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
    "  \"status_source\": \"STATUS.md\",\n"
    "  \"required_paths\": [\n"
    "    \"CITATION.cff\",\n"
    "    \"STATUS.md\",\n"
    "    \"science/configs/real_model_structure.json\",\n"
    "    \"science/data/registry.csv\",\n"
    "    \"data/scenarios/bau_hybrid_2026_manifest.json\"\n"
    "  ],\n"
    "  \"retrieval\": {\n"
    "    \"canonical\": [\"STATUS.md\", \"README.md\", \"CITATION.cff\", \"data/scenarios/bau_hybrid_2026_manifest.json\"],\n"
    "    \"structural\": [\"science/configs\", \"science/src/world3_empirical/real_model_contract.py\", \"science/src/world3_empirical/model.py\", \"science/src/world3_empirical/world3_03.py\"],\n"
    "    \"evidence\": [\"science/data/registry.csv\", \"science/data/processed\", \"science/data/experiments\", \"science/data/forecasts\", \"data/scenarios/fit_diagnostics.csv\", \"data/scenarios/backtest_2019_latest.csv\", \"data/scenarios/backtest_multi_origin.csv\", \"data/scenarios/parameter_identifiability.csv\", \"data/scenarios/candidate_ranking.csv\", \"data/scenarios/validation_candidate_ranking.csv\"],\n"
    "    \"tabular\": [\"data/scenarios\", \"science/data/processed\", \"science/data/experiments\"],\n"
    "    \"implementation\": [\"science/src/world3_empirical\", \"science/scripts\"],\n"
    "    \"exclude\": [\".github\", \"science/vendor\", \"science/data/raw\", \"__pycache__\"]\n"
    "  }\n"
    "}\n";

gboolean
atm_repository_apply_builtin_compatibility_overlay (
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    gboolean *out_applied,
    GError **error
)
{
    GStatBuf root_stat;
    GStatBuf atm_stat;
    char *basename = NULL;
    char *atm_path = NULL;
    char *manifest_path = NULL;
    char *receipt_path = NULL;
    char *manifest_sha256 = NULL;
    char *receipt = NULL;
    gboolean ok = FALSE;

    if (snapshot_root == NULL || snapshot_root[0] == '\0' ||
        repository_id == NULL || snapshot_sha == NULL ||
        out_applied == NULL) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Compatibility metadata overlay received invalid arguments."
        );
        return FALSE;
    }

    *out_applied = FALSE;

    if (g_strcmp0 (repository_id, "ewd") != 0 ||
        g_strcmp0 (snapshot_sha, ATM_EWD_D9_SHA) != 0) {
        return TRUE;
    }

    basename = g_path_get_basename (snapshot_root);
    char *staging_basename = g_strdup_printf ("%s.part", ATM_EWD_D9_SHA);
    gboolean path_is_bound =
        g_strcmp0 (basename, ATM_EWD_D9_SHA) == 0 ||
        g_strcmp0 (basename, staging_basename) == 0;
    g_free (staging_basename);
    if (!path_is_bound) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Compatibility overlay path is not bound to the admitted EWD SHA."
        );
        goto out;
    }

    if (g_lstat (snapshot_root, &root_stat) != 0 ||
        !S_ISDIR (root_stat.st_mode) ||
        S_ISLNK (root_stat.st_mode)) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Compatibility overlay requires a real snapshot/staging directory."
        );
        goto out;
    }

    atm_path = g_build_filename (snapshot_root, ".atm", NULL);
    manifest_path = g_build_filename (
        atm_path, "repository.json", NULL);
    receipt_path = g_build_filename (
        atm_path, "compatibility-overlay.json", NULL);

    if (g_lstat (atm_path, &atm_stat) == 0) {
        if (!S_ISDIR (atm_stat.st_mode) || S_ISLNK (atm_stat.st_mode)) {
            g_set_error_literal (
                error,
                ATM_INGEST_ERROR,
                ATM_INGEST_ERROR_IO,
                "Existing .atm path is not a real directory; overlay refused."
            );
            goto out;
        }

        /* Never overwrite upstream metadata or repair a partial overlay in place.
         * The normal manifest validator must accept or reject existing content. */
        ok = TRUE;
        goto out;
    }
    if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Could not inspect .atm before applying compatibility overlay: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (g_mkdir (atm_path, 0700) != 0) {
        g_set_error (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Could not create AtM overlay directory: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!g_file_set_contents (
            manifest_path,
            ATM_EWD_D9_MANIFEST,
            (gssize) (sizeof (ATM_EWD_D9_MANIFEST) - 1),
            error
        )) {
        g_rmdir (atm_path);
        goto out;
    }

    manifest_sha256 = g_compute_checksum_for_data (
        G_CHECKSUM_SHA256,
        (const guchar *) ATM_EWD_D9_MANIFEST,
        sizeof (ATM_EWD_D9_MANIFEST) - 1
    );
    receipt = g_strdup_printf (
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"overlay_id\": \"atm-ewd-d9e249-manifest-overlay/1\",\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"upstream_sha\": \"%s\",\n"
        "  \"manifest_sha256\": \"%s\",\n"
        "  \"ownership\": \"AtM application metadata; absent from the upstream commit\"\n"
        "}\n",
        ATM_EWD_D9_SHA,
        manifest_sha256
    );

    if (!g_file_set_contents (
            receipt_path,
            receipt,
            -1,
            error
        )) {
        g_remove (manifest_path);
        g_rmdir (atm_path);
        goto out;
    }

    *out_applied = TRUE;
    ok = TRUE;

out:
    g_free (receipt);
    g_free (manifest_sha256);
    g_free (receipt_path);
    g_free (manifest_path);
    g_free (atm_path);
    g_free (basename);
    return ok;
}


static gboolean
repository_ingest_archive_internal (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    GCancellable *cancellable,
    gboolean durable,
    char **out_pre_barrier_seal,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    GStatBuf archive_stat;
    GStatBuf data_root_stat;
    GStatBuf staging_stat;
    char *staging_path = NULL;
    char *staging_parent = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    char *pre_barrier_seal = NULL;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    guint64 sealed_files = 0;
    guint64 sealed_bytes = 0;
    AtmSnapshotDurabilityStats durability_stats = { 0 };
    AtmSnapshotNamespaceStats namespace_stats = { 0 };
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
    if (durable) {
        g_return_val_if_fail (out_pre_barrier_seal != NULL, FALSE);
        g_return_val_if_fail (*out_pre_barrier_seal == NULL, FALSE);
    }

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

    if (durable &&
        (g_lstat (
             data_root,
             &data_root_stat
         ) != 0 ||
         !S_ISDIR (data_root_stat.st_mode) ||
         S_ISLNK (data_root_stat.st_mode))) {
        g_set_error_literal (
            error,
            ATM_INGEST_ERROR,
            ATM_INGEST_ERROR_IO,
            "Durable repository ingest requires an existing real data root."
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

    gboolean compatibility_overlay_applied = FALSE;
    if (!atm_repository_apply_builtin_compatibility_overlay (
            staging_path,
            repository_id,
            sha,
            &compatibility_overlay_applied,
            error
        )) {
        goto out;
    }
    (void) compatibility_overlay_applied;

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

    if (durable) {
        if (!atm_snapshot_seal_compute (
                staging_path,
                &pre_barrier_seal,
                &sealed_files,
                &sealed_bytes,
                error
            )) {
            goto out;
        }

        atm_test_fault_checkpoint (
            "runtime_ingest_before_tree_fsync"
        );

        if (!atm_snapshot_durability_sync_tree (
                staging_path,
                &durability_stats,
                error
            )) {
            goto out;
        }

        if (!atm_snapshot_namespace_prepare_final_parent (
                data_root,
                repository_id,
                &namespace_stats,
                error
            )) {
            goto out;
        }

        if (cancellable != NULL &&
            g_cancellable_set_error_if_cancelled (
                cancellable,
                error
            )) {
            goto out;
        }
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

    if (durable) {
        atm_test_fault_checkpoint (
            "runtime_ingest_before_destination_parent_fsync"
        );
    }

    if (durable &&
        !atm_snapshot_durability_sync_parent (
            snapshot_path,
            &durability_stats,
            error
        )) {
        goto out;
    }

    if (durable) {
        atm_test_fault_checkpoint (
            "runtime_ingest_before_source_parent_fsync"
        );
    }

    if (durable &&
        !atm_snapshot_durability_sync_parent (
            staging_path,
            &durability_stats,
            error
        )) {
        goto out;
    }

    if (durable) {
        *out_pre_barrier_seal =
            g_steal_pointer (&pre_barrier_seal);
    }

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

    g_clear_pointer (&pre_barrier_seal, g_free);
    g_clear_pointer (&snapshot_path, g_free);
    g_clear_pointer (&version, g_free);
    g_clear_pointer (&staging_parent, g_free);
    g_clear_pointer (&staging_path, g_free);
    return ok;
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
    return repository_ingest_archive_internal (
        data_root,
        archive_path,
        repository_id,
        repository_acronym,
        repository_display_name,
        sha,
        cancellable,
        FALSE,
        NULL,
        out_version,
        out_snapshot_path,
        out_entries,
        out_total_bytes,
        error
    );
}

gboolean
atm_repository_ingest_archive_durable (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    char **out_pre_barrier_seal,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    return repository_ingest_archive_internal (
        data_root,
        archive_path,
        repository_id,
        repository_acronym,
        repository_display_name,
        sha,
        NULL,
        TRUE,
        out_pre_barrier_seal,
        out_version,
        out_snapshot_path,
        out_entries,
        out_total_bytes,
        error
    );
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


typedef struct {
    char *data_root;
    char *archive_path;
    char *repository_id;
    char *repository_acronym;
    char *repository_display_name;
    char *sha;
} AtmIngestTaskData;

typedef struct {
    char *version;
    char *snapshot_path;
    guint64 entries;
    guint64 total_bytes;
} AtmIngestTaskResult;

static void
ingest_task_data_free (AtmIngestTaskData *data)
{
    if (data == NULL) {
        return;
    }

    g_free (data->data_root);
    g_free (data->archive_path);
    g_free (data->repository_id);
    g_free (data->repository_acronym);
    g_free (data->repository_display_name);
    g_free (data->sha);
    g_free (data);
}

static void
ingest_task_result_free (AtmIngestTaskResult *result)
{
    if (result == NULL) {
        return;
    }

    g_free (result->version);
    g_free (result->snapshot_path);
    g_free (result);
}

static void
ingest_task_thread (
    GTask *task,
    gpointer source_object,
    gpointer task_data,
    GCancellable *cancellable
)
{
    AtmIngestTaskData *data = task_data;
    AtmIngestTaskResult *result = g_new0 (AtmIngestTaskResult, 1);
    GError *error = NULL;

    (void) source_object;

    if (!atm_repository_ingest_archive_cancellable (
            data->data_root,
            data->archive_path,
            data->repository_id,
            data->repository_acronym,
            data->repository_display_name,
            data->sha,
            cancellable,
            &result->version,
            &result->snapshot_path,
            &result->entries,
            &result->total_bytes,
            &error
        )) {
        ingest_task_result_free (result);
        g_task_return_error (task, error);
        return;
    }

    g_task_return_pointer (
        task,
        result,
        (GDestroyNotify) ingest_task_result_free
    );
}

void
atm_repository_ingest_archive_async (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data
)
{
    AtmIngestTaskData *data = g_new0 (AtmIngestTaskData, 1);
    GTask *task;

    data->data_root = g_strdup (data_root);
    data->archive_path = g_strdup (archive_path);
    data->repository_id = g_strdup (repository_id);
    data->repository_acronym = g_strdup (repository_acronym);
    data->repository_display_name = g_strdup (repository_display_name);
    data->sha = g_strdup (sha);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_task_data (
        task,
        data,
        (GDestroyNotify) ingest_task_data_free
    );
    g_task_set_check_cancellable (task, FALSE);
    g_task_set_name (task, "atm-repository-ingest");
    g_task_run_in_thread (task, ingest_task_thread);
    g_object_unref (task);
}

gboolean
atm_repository_ingest_archive_finish (
    GAsyncResult *result,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    AtmIngestTaskResult *task_result;

    g_return_val_if_fail (G_IS_TASK (result), FALSE);
    g_return_val_if_fail (out_version != NULL, FALSE);
    g_return_val_if_fail (*out_version == NULL, FALSE);
    g_return_val_if_fail (out_snapshot_path != NULL, FALSE);
    g_return_val_if_fail (*out_snapshot_path == NULL, FALSE);

    task_result = g_task_propagate_pointer (
        G_TASK (result),
        error
    );

    if (task_result == NULL) {
        return FALSE;
    }

    *out_version = g_steal_pointer (&task_result->version);
    *out_snapshot_path =
        g_steal_pointer (&task_result->snapshot_path);

    if (out_entries != NULL) {
        *out_entries = task_result->entries;
    }

    if (out_total_bytes != NULL) {
        *out_total_bytes = task_result->total_bytes;
    }

    ingest_task_result_free (task_result);
    return TRUE;
}
