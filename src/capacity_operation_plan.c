#include "capacity_operation_plan.h"

GQuark
atm_capacity_operation_plan_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-capacity-operation-plan-error-quark"
    );
}

static gboolean
add_checked (
    guint64 left,
    guint64 right,
    guint64 *out,
    GError **error
)
{
    if (G_MAXUINT64 - left < right) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_OPERATION_PLAN_ERROR,
            ATM_CAPACITY_OPERATION_PLAN_ERROR_OVERFLOW,
            "Capacity operation phase arithmetic overflowed."
        );
        return FALSE;
    }

    *out = left + right;
    return TRUE;
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
atm_capacity_operation_plan_build (
    AtmCapacityOperationKind operation_kind,
    const AtmArchiveInspection *archive_inspection,
    const AtmCapacityOperationPrediction *prediction,
    AtmCapacityOperationPlan *out_plan,
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
        (AtmCapacityOperationPlan) {
            .operation_kind = operation_kind,
            .must_admit_before_quarantine =
                operation_kind ==
                    ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR,
            .phase_count =
                ATM_CAPACITY_PHASE_COUNT
        };

    /*
     * Phase 0: archive download.
     *
     * The archive .part file is renamed to the completed archive on
     * the same cache filesystem, so this is one operation-owned
     * allocation rather than two simultaneous archive copies.
     */
    out_plan->phases[
        ATM_CAPACITY_PHASE_DOWNLOAD
    ].bytes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->archive_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_DOWNLOAD
    ].inodes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->archive_additional_inodes;

    /*
     * Phase 1: extraction.
     *
     * The completed archive remains present while extraction creates
     * the new snapshot tree. Existing active/quarantined snapshots
     * were already consuming blocks before admission and therefore
     * are intentionally not counted again as additional demand.
     */
    out_plan->phases[
        ATM_CAPACITY_PHASE_EXTRACTION
    ].bytes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->archive_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_EXTRACTION
    ].inodes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        prediction->archive_additional_inodes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_EXTRACTION
    ].bytes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        prediction->snapshot_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_EXTRACTION
    ].inodes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        archive_inspection->materialized_entries;

    /*
     * Phase 2: retrieval-index build.
     *
     * The newly materialized snapshot and completed archive remain
     * present while index staging/final storage is created.
     */
    out_plan->phases[
        ATM_CAPACITY_PHASE_INDEX_BUILD
    ].bytes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        prediction->snapshot_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_INDEX_BUILD
    ].inodes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        archive_inspection->materialized_entries;

    if (!add_checked (
            prediction->archive_additional_bytes,
            prediction->index_additional_bytes,
            &out_plan->phases[
                ATM_CAPACITY_PHASE_INDEX_BUILD
            ].bytes[
                ATM_CAPACITY_ROOT_CACHE
            ],
            error
        ) ||
        !add_checked (
            prediction->archive_additional_inodes,
            prediction->index_additional_inodes,
            &out_plan->phases[
                ATM_CAPACITY_PHASE_INDEX_BUILD
            ].inodes[
                ATM_CAPACITY_ROOT_CACHE
            ],
            error
        )) {
        return FALSE;
    }

    /*
     * Phase 3: guarded Control DB authority commit.
     *
     * Archive cleanup happens after the operation body completes, so
     * archive + snapshot + completed index still coexist while state
     * mutation occurs.
     */
    out_plan->phases[
        ATM_CAPACITY_PHASE_STATE_COMMIT
    ].bytes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        prediction->snapshot_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_STATE_COMMIT
    ].inodes[
        ATM_CAPACITY_ROOT_DATA
    ] =
        archive_inspection->materialized_entries;

    out_plan->phases[
        ATM_CAPACITY_PHASE_STATE_COMMIT
    ].bytes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        out_plan->phases[
            ATM_CAPACITY_PHASE_INDEX_BUILD
        ].bytes[
            ATM_CAPACITY_ROOT_CACHE
        ];

    out_plan->phases[
        ATM_CAPACITY_PHASE_STATE_COMMIT
    ].inodes[
        ATM_CAPACITY_ROOT_CACHE
    ] =
        out_plan->phases[
            ATM_CAPACITY_PHASE_INDEX_BUILD
        ].inodes[
            ATM_CAPACITY_ROOT_CACHE
        ];

    out_plan->phases[
        ATM_CAPACITY_PHASE_STATE_COMMIT
    ].bytes[
        ATM_CAPACITY_ROOT_STATE
    ] =
        prediction->state_additional_bytes;

    out_plan->phases[
        ATM_CAPACITY_PHASE_STATE_COMMIT
    ].inodes[
        ATM_CAPACITY_ROOT_STATE
    ] =
        prediction->state_additional_inodes;

    return TRUE;
}
