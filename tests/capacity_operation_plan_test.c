#include "capacity_operation_plan.h"

#include <glib.h>

static AtmArchiveInspection
sample_inspection (void)
{
    AtmArchiveInspection inspection = {
        .archive_entries = 90,
        .materialized_entries = 100,
        .regular_files = 70,
        .directories = 29,
        .logical_regular_bytes = 1234567,
        .largest_regular_file_bytes = 345678
    };

    return inspection;
}

static AtmCapacityDownloadPrediction
sample_download_prediction (void)
{
    AtmCapacityDownloadPrediction prediction = {
        .archive_additional_bytes = 100,
        .archive_additional_inodes = 1
    };

    return prediction;
}

static AtmCapacityMutationPrediction
sample_mutation_prediction (void)
{
    AtmCapacityMutationPrediction prediction = {
        .snapshot_additional_bytes = 500,
        .index_additional_bytes = 400,
        .index_additional_inodes = 3,
        .state_additional_bytes = 20,
        .state_additional_inodes = 2
    };

    return prediction;
}

static void
test_download_checkpoint_is_cache_only (void)
{
    AtmCapacityDownloadPrediction prediction =
        sample_download_prediction ();
    AtmCapacityPhaseRequirement phase;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_download_phase_build (
            &prediction,
            &phase,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        phase.bytes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        0
    );
    g_assert_cmpuint (
        phase.bytes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        100
    );
    g_assert_cmpuint (
        phase.inodes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        1
    );
    g_assert_cmpuint (
        phase.bytes[
            ATM_CAPACITY_ROOT_STATE
        ],
        ==,
        0
    );
}

static void
test_fresh_post_download_phase_construction (void)
{
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan plan;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            &prediction,
            &plan,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        plan.phase_count,
        ==,
        ATM_CAPACITY_MUTATION_PHASE_COUNT
    );
    g_assert_false (
        plan.must_admit_before_quarantine
    );

    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
        ].bytes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        500
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
        ].inodes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        105
    );

    /*
     * Fresh install can create five AtM-owned structural directories outside
     * the F2 materialized archive tree.
     */
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
        ].inodes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        105
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
        ].inodes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        105
    );

    /*
     * The archive is already on disk at this second checkpoint.
     * It must not be charged again as additional cache demand.
     */
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
        ].bytes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        0
    );

    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
        ].bytes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        500
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
        ].bytes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        400
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD
        ].inodes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        3
    );

    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
        ].bytes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        500
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
        ].bytes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        400
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
        ].bytes[
            ATM_CAPACITY_ROOT_STATE
        ],
        ==,
        20
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT
        ].inodes[
            ATM_CAPACITY_ROOT_STATE
        ],
        ==,
        2
    );
}

static void
test_operation_kinds_apply_fresh_inode_overhead_only (void)
{
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan fresh;
    AtmCapacityMutationPlan update;
    AtmCapacityMutationPlan repair;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            &prediction,
            &fresh,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
            &inspection,
            &prediction,
            &update,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR,
            &inspection,
            &prediction,
            &repair,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        fresh.must_admit_before_quarantine
    );
    g_assert_false (
        update.must_admit_before_quarantine
    );
    g_assert_true (
        repair.must_admit_before_quarantine
    );

    for (gsize phase = 0;
         phase < ATM_CAPACITY_MUTATION_PHASE_COUNT;
         phase++) {
        g_assert_cmpuint (
            fresh.phases[phase].bytes[
                ATM_CAPACITY_ROOT_DATA
            ],
            ==,
            update.phases[phase].bytes[
                ATM_CAPACITY_ROOT_DATA
            ]
        );
        g_assert_cmpuint (
            fresh.phases[phase].bytes[
                ATM_CAPACITY_ROOT_CACHE
            ],
            ==,
            update.phases[phase].bytes[
                ATM_CAPACITY_ROOT_CACHE
            ]
        );
        g_assert_cmpuint (
            fresh.phases[phase].bytes[
                ATM_CAPACITY_ROOT_STATE
            ],
            ==,
            update.phases[phase].bytes[
                ATM_CAPACITY_ROOT_STATE
            ]
        );

        g_assert_cmpuint (
            fresh.phases[phase].inodes[
                ATM_CAPACITY_ROOT_DATA
            ],
            ==,
            inspection.materialized_entries + 5
        );
        g_assert_cmpuint (
            update.phases[phase].inodes[
                ATM_CAPACITY_ROOT_DATA
            ],
            ==,
            inspection.materialized_entries
        );
        g_assert_cmpuint (
            repair.phases[phase].inodes[
                ATM_CAPACITY_ROOT_DATA
            ],
            ==,
            inspection.materialized_entries
        );

        g_assert_cmpuint (
            fresh.phases[phase].inodes[
                ATM_CAPACITY_ROOT_CACHE
            ],
            ==,
            update.phases[phase].inodes[
                ATM_CAPACITY_ROOT_CACHE
            ]
        );
        g_assert_cmpuint (
            fresh.phases[phase].inodes[
                ATM_CAPACITY_ROOT_STATE
            ],
            ==,
            update.phases[phase].inodes[
                ATM_CAPACITY_ROOT_STATE
            ]
        );
    }

    g_assert_cmpmem (
        update.phases,
        sizeof update.phases,
        repair.phases,
        sizeof repair.phases
    );
}

static void
test_shared_filesystem_post_download_peak (void)
{
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan plan;
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 7,
            .available_bytes = 920,
            .available_inodes = 110,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 7,
            .available_bytes = 920,
            .available_inodes = 105,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 7,
            .available_bytes = 920,
            .available_inodes = 105,
            .inode_budget_known = TRUE
        }
    };
    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            &prediction,
            &plan,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            plan.phases,
            plan.phase_count,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (decision.admitted);

    g_assert_cmpuint (
        decision.devices[0].operation_peak_bytes,
        ==,
        920
    );
    g_assert_cmpuint (
        decision.devices[0].operation_peak_inodes,
        ==,
        110
    );

    roots[0].available_bytes = 919;
    roots[1].available_bytes = 919;
    roots[2].available_bytes = 919;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            plan.phases,
            plan.phase_count,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (decision.admitted);
}

static void
test_split_filesystems_post_download (void)
{
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan plan;
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = 500,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = 400,
            .available_inodes = 3,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 3,
            .available_bytes = 20,
            .available_inodes = 2,
            .inode_budget_known = TRUE
        }
    };
    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
            &inspection,
            &prediction,
            &plan,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            plan.phases,
            plan.phase_count,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (decision.admitted);
    g_assert_cmpuint (
        decision.device_count,
        ==,
        3
    );
}

static void
test_archive_not_double_counted_between_checkpoints (void)
{
    AtmCapacityDownloadPrediction download =
        sample_download_prediction ();
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction mutation =
        sample_mutation_prediction ();
    AtmCapacityPhaseRequirement download_phase;
    AtmCapacityMutationPlan mutation_plan;
    GError *error = NULL;

    download.archive_additional_bytes = 777;

    g_assert_true (
        atm_capacity_download_phase_build (
            &download,
            &download_phase,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            &mutation,
            &mutation_plan,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        download_phase.bytes[
            ATM_CAPACITY_ROOT_CACHE
        ],
        ==,
        777
    );

    for (gsize i = 0;
         i < mutation_plan.phase_count;
         i++) {
        g_assert_cmpuint (
            mutation_plan.phases[i].bytes[
                ATM_CAPACITY_ROOT_CACHE
            ],
            <=,
            mutation.index_additional_bytes
        );
    }
}

static void
test_logical_archive_bytes_are_not_allocated_prediction (void)
{
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan plan;
    GError *error = NULL;

    inspection.logical_regular_bytes =
        G_GUINT64_CONSTANT (9000000000);
    prediction.snapshot_additional_bytes =
        1234;

    g_assert_true (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            &prediction,
            &plan,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
        ].bytes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        1234
    );
    g_assert_cmpuint (
        plan.phases[
            ATM_CAPACITY_MUTATION_PHASE_EXTRACTION
        ].inodes[
            ATM_CAPACITY_ROOT_DATA
        ],
        ==,
        inspection.materialized_entries
    );
}

static void
test_invalid_operation_kind_rejected (void)
{
    AtmArchiveInspection inspection =
        sample_inspection ();
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan plan;
    GError *error = NULL;

    g_assert_false (
        atm_capacity_mutation_plan_build (
            (AtmCapacityOperationKind) 99,
            &inspection,
            &prediction,
            &plan,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_OPERATION_PLAN_ERROR,
        ATM_CAPACITY_OPERATION_PLAN_ERROR_ARGUMENT
    );
    g_clear_error (&error);
}

static void
test_empty_inspection_rejected (void)
{
    AtmArchiveInspection inspection = { 0 };
    AtmCapacityMutationPrediction prediction =
        sample_mutation_prediction ();
    AtmCapacityMutationPlan plan;
    GError *error = NULL;

    g_assert_false (
        atm_capacity_mutation_plan_build (
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            &prediction,
            &plan,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_OPERATION_PLAN_ERROR,
        ATM_CAPACITY_OPERATION_PLAN_ERROR_ARGUMENT
    );
    g_clear_error (&error);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (
        &argc,
        &argv,
        NULL
    );

    g_test_add_func (
        "/capacity-operation/download-checkpoint",
        test_download_checkpoint_is_cache_only
    );
    g_test_add_func (
        "/capacity-operation/post-download-fresh-phases",
        test_fresh_post_download_phase_construction
    );
    g_test_add_func (
        "/capacity-operation/fresh-inode-overhead",
        test_operation_kinds_apply_fresh_inode_overhead_only
    );
    g_test_add_func (
        "/capacity-operation/shared-filesystem-post-download",
        test_shared_filesystem_post_download_peak
    );
    g_test_add_func (
        "/capacity-operation/split-filesystems-post-download",
        test_split_filesystems_post_download
    );
    g_test_add_func (
        "/capacity-operation/archive-not-double-counted",
        test_archive_not_double_counted_between_checkpoints
    );
    g_test_add_func (
        "/capacity-operation/logical-not-allocated",
        test_logical_archive_bytes_are_not_allocated_prediction
    );
    g_test_add_func (
        "/capacity-operation/invalid-kind",
        test_invalid_operation_kind_rejected
    );
    g_test_add_func (
        "/capacity-operation/empty-inspection",
        test_empty_inspection_rejected
    );

    return g_test_run ();
}
