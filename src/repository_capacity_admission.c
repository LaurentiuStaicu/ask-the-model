#include "repository_capacity_admission.h"

#include "capacity_measurement.h"
#include "capacity_operation_plan.h"
#include "repository_capacity_policy.h"
#include "repository_ingest.h"

static AtmCapacityRootAvailability
root_from_measurement (
    const AtmFilesystemCapacityMeasurement *measurement
)
{
    return (AtmCapacityRootAvailability) {
        .device_id = measurement->device_id,
        .available_bytes = measurement->available_bytes,
        .available_inodes = measurement->available_inodes,
        .inode_budget_known = measurement->inode_budget_known
    };
}

static void
copy_single_root (
    const AtmCapacityRootAvailability *source,
    AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT]
)
{
    for (guint i = 0; i < ATM_CAPACITY_ROOT_COUNT; i++) {
        roots[i] = *source;
    }
}

static gboolean
evaluate_phases (
    gboolean byte_prediction_qualified,
    gboolean must_admit_before_quarantine,
    const AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT],
    const AtmCapacityPhaseRequirement *phases,
    gsize phase_count,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmCapacityReservePolicy no_reserve = { 0 };

    *out_result =
        (AtmRepositoryCapacityAdmissionResult) {
            .byte_prediction_qualified =
                byte_prediction_qualified,
            .admitted = FALSE,
            .must_admit_before_quarantine =
                must_admit_before_quarantine,
            .decision = { 0 }
        };

    if (!atm_capacity_admission_evaluate (
            roots,
            phases,
            phase_count,
            &no_reserve,
            &out_result->decision,
            error
        )) {
        return FALSE;
    }

    out_result->admitted =
        out_result->decision.admitted;
    return TRUE;
}

gboolean
atm_repository_capacity_download_evaluate (
    const char *repository_id,
    const char *repository_sha,
    const AtmCapacityRootAvailability *cache_availability,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmRepositoryCapacityDownloadPolicy policy;
    AtmCapacityPhaseRequirement phase;
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ];

    g_return_val_if_fail (
        cache_availability != NULL,
        FALSE
    );
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!atm_repository_capacity_policy_build_download (
            repository_id,
            repository_sha,
            &policy,
            error
        ) ||
        !atm_capacity_download_phase_build (
            &policy.prediction,
            &phase,
            error
        )) {
        return FALSE;
    }

    copy_single_root (
        cache_availability,
        roots
    );

    return evaluate_phases (
        policy.byte_prediction_qualified,
        FALSE,
        roots,
        &phase,
        1,
        out_result,
        error
    );
}

gboolean
atm_repository_capacity_mutation_evaluate (
    const char *repository_id,
    const char *repository_sha,
    int operation_kind,
    const AtmArchiveInspection *archive_inspection,
    const AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT],
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmRepositoryCapacityMutationPolicy policy;
    AtmCapacityMutationPlan plan;

    g_return_val_if_fail (
        archive_inspection != NULL,
        FALSE
    );
    g_return_val_if_fail (roots != NULL, FALSE);
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!atm_repository_capacity_policy_build_mutation (
            repository_id,
            repository_sha,
            (AtmCapacityOperationKind) operation_kind,
            &policy,
            error
        ) ||
        !atm_capacity_mutation_plan_build (
            (AtmCapacityOperationKind) operation_kind,
            archive_inspection,
            &policy.prediction,
            &plan,
            error
        )) {
        return FALSE;
    }

    return evaluate_phases (
        policy.byte_prediction_qualified,
        plan.must_admit_before_quarantine,
        roots,
        plan.phases,
        plan.phase_count,
        out_result,
        error
    );
}

gboolean
atm_repository_capacity_state_commit_evaluate (
    const AtmCapacityRootAvailability *state_availability,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ];
    AtmCapacityPhaseRequirement phase = { 0 };

    g_return_val_if_fail (
        state_availability != NULL,
        FALSE
    );
    g_return_val_if_fail (out_result != NULL, FALSE);

    copy_single_root (
        state_availability,
        roots
    );

    phase.bytes[ATM_CAPACITY_ROOT_STATE] =
        ATM_CAPACITY_STATE_COMMIT_HEADROOM_BYTES;
    phase.inodes[ATM_CAPACITY_ROOT_STATE] =
        ATM_CAPACITY_STATE_COMMIT_HEADROOM_INODES;

    return evaluate_phases (
        TRUE,
        FALSE,
        roots,
        &phase,
        1,
        out_result,
        error
    );
}

gboolean
atm_repository_capacity_download_preflight (
    const char *cache_path,
    const char *repository_id,
    const char *repository_sha,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmFilesystemCapacityMeasurement measurement;

    g_return_val_if_fail (cache_path != NULL, FALSE);
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!atm_capacity_measure_filesystem (
            cache_path,
            &measurement,
            error
        )) {
        return FALSE;
    }

    AtmCapacityRootAvailability availability =
        root_from_measurement (&measurement);

    return atm_repository_capacity_download_evaluate (
        repository_id,
        repository_sha,
        &availability,
        out_result,
        error
    );
}

gboolean
atm_repository_capacity_mutation_preflight (
    const char *data_path,
    const char *cache_path,
    const char *state_path,
    const char *archive_path,
    const char *repository_id,
    const char *repository_sha,
    int operation_kind,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmArchiveLimits limits = {
        .max_entries = ATM_INGEST_MAX_ENTRIES,
        .max_file_bytes = ATM_INGEST_MAX_FILE_BYTES,
        .max_total_bytes = ATM_INGEST_MAX_TOTAL_BYTES
    };
    AtmArchiveInspection inspection;
    AtmFilesystemCapacityMeasurement measurements[
        ATM_CAPACITY_ROOT_COUNT
    ];
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ];

    g_return_val_if_fail (data_path != NULL, FALSE);
    g_return_val_if_fail (cache_path != NULL, FALSE);
    g_return_val_if_fail (state_path != NULL, FALSE);
    g_return_val_if_fail (archive_path != NULL, FALSE);
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!atm_archive_inspect_snapshot (
            archive_path,
            &limits,
            &inspection,
            error
        ) ||
        !atm_capacity_measure_filesystem (
            data_path,
            &measurements[ATM_CAPACITY_ROOT_DATA],
            error
        ) ||
        !atm_capacity_measure_filesystem (
            cache_path,
            &measurements[ATM_CAPACITY_ROOT_CACHE],
            error
        ) ||
        !atm_capacity_measure_filesystem (
            state_path,
            &measurements[ATM_CAPACITY_ROOT_STATE],
            error
        )) {
        return FALSE;
    }

    for (guint i = 0; i < ATM_CAPACITY_ROOT_COUNT; i++) {
        roots[i] =
            root_from_measurement (&measurements[i]);
    }

    return atm_repository_capacity_mutation_evaluate (
        repository_id,
        repository_sha,
        operation_kind,
        &inspection,
        roots,
        out_result,
        error
    );
}

gboolean
atm_repository_capacity_state_commit_preflight (
    const char *state_path,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
)
{
    AtmFilesystemCapacityMeasurement measurement;

    g_return_val_if_fail (state_path != NULL, FALSE);
    g_return_val_if_fail (out_result != NULL, FALSE);

    if (!atm_capacity_measure_filesystem (
            state_path,
            &measurement,
            error
        )) {
        return FALSE;
    }

    AtmCapacityRootAvailability availability =
        root_from_measurement (&measurement);

    return atm_repository_capacity_state_commit_evaluate (
        &availability,
        out_result,
        error
    );
}
