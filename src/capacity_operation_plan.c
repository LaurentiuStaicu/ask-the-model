#include "capacity_operation_plan.h"

GQuark
atm_capacity_operation_plan_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-capacity-operation-plan-error-quark"
    );
}

static gboolean
operation_kind_valid (
    AtmCapacityOperationKind operation_kind
)
{
    return
        operation_kind ==
            ATM_CAPACITY_OPERATION_FRESH_INSTALL ||
        operation_kind ==
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE ||
        operation_kind ==
            ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR;
}

gboolean
atm_capacity_download_phase_build (
    const AtmCapacityDownloadPrediction *prediction,
    AtmCapacityPhaseRequirement *out_phase,
    GError **error
)
{
    g_return_val_if_fail (
        prediction != NULL,
        FALSE
    );
    g_return_val_if_fail (
        out_phase != NULL,
        FALSE
    );

    *out_phase =
        (AtmCapacityPhaseRequirement) { 0 };

    /*
     * This checkpoint is evaluated before deterministic archive staging
     * begins. The .part file is renamed to the completed archive on the
     * same cache filesystem, so policy supplies one additional archive
     * allocation/inode prediction rather than two simultaneous copies.
     */
    out_phase->bytes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->archive_additional_bytes;

    out_phase->inodes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->archive_additional_inodes;

    return TRUE;
}

gboolean
atm_capacity_mutation_plan_build (
    AtmCapacityOperationKind operation_kind,
    const AtmArchiveInspection *archive_inspection,
    const AtmCapacityMutationPrediction *prediction,
    AtmCapacityMutationPlan *out_plan,
    GError **error
)
{
    g_return_val_if_fail (
        archive_inspection != NULL,
        FALSE
    );
    g_return_val_if_fail (
        prediction != NULL,
        FALSE
    );
    g_return_val_if_fail (
        out_plan != NULL,
        FALSE
    );

    if (!operation_kind_valid (
            operation_kind
        )) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_OPERATION_PLAN_ERROR,
            ATM_CAPACITY_OPERATION_PLAN_ERROR_ARGUMENT,
            "Unknown repository capacity operation kind."
        );
        return FALSE;
    }

    if (archive_inspection->materialized_entries == 0) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_OPERATION_PLAN_ERROR,
            ATM_CAPACITY_OPERATION_PLAN_ERROR_ARGUMENT,
            "Archive inspection has no materialized-entry requirement."
        );
        return FALSE;
    }

    *out_plan =
        (AtmCapacityMutationPlan) {
            .operation_kind = operation_kind,
            .must_admit_before_quarantine =
                operation_kind ==
                    ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR,
            .phase_count =
                ATM_CAPACITY_MUTATION_PHASE_COUNT
        };

    /*
     * IMPORTANT CHECKPOINT SEMANTICS
     *
     * This plan is evaluated only after the completed archive has been
     * downloaded and the filesystem availability has been measured again.
     * The archive therefore belongs to the baseline already reflected in
     * f_bavail/f_favail and is intentionally absent from all requirements
     * below. Counting it again would double-charge the cache filesystem.
     */

    /*
     * Phase 0: extraction.
     *
     * Existing active/quarantined snapshots were already consuming blocks
     * and inodes before this checkpoint. Only the new materialized snapshot
     * is additional demand.
     */
    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
    ].bytes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        prediction->snapshot_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
    ].inodes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        archive_inspection->materialized_entries;

    /*
     * Phase 1: retrieval-index build.
     *
     * The new snapshot remains while index staging/final allocation is
     * created. The completed archive also remains physically present, but
     * it is still part of the post-download baseline rather than new demand.
     */
    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
    ].bytes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        prediction->snapshot_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
    ].inodes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        archive_inspection->materialized_entries;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
    ].bytes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->index_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
    ].inodes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->index_additional_inodes;

    /*
     * Phase 2: guarded Control DB authority commit.
     *
     * Snapshot + completed index remain while state mutation consumes
     * state-root capacity. Archive cleanup still occurs later, but archive
     * allocation remains baseline at this second checkpoint.
     */
    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
    ].bytes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        prediction->snapshot_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
    ].inodes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        archive_inspection->materialized_entries;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
    ].bytes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->index_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
    ].inodes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->index_additional_inodes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
    ].bytes[
        ATM_CAPACITY_ROOT_STATE
    ] =
        prediction->state_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
    ].inodes[
        ATM_CAPACITY_ROOT_STATE
    ] =
        prediction->state_additional_inodes;

    return TRUE;
}
