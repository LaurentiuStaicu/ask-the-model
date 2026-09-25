#include "repository_capacity_admission.h"

#include "capacity_operation_plan.h"

#include <glib.h>

static const char *CBD_SHA =
    "e6e3b3077b7d78e3f37d963e0734e9084e8a62ab";
static const char *UNKNOWN_SHA =
    "1111111111111111111111111111111111111111";

static AtmCapacityRootAvailability
availability (
    guint64 device_id,
    guint64 bytes,
    guint64 inodes,
    gboolean inode_budget_known
)
{
    return (AtmCapacityRootAvailability) {
        .device_id = device_id,
        .available_bytes = bytes,
        .available_inodes = inodes,
        .inode_budget_known = inode_budget_known
    };
}

static const AtmCapacityDeviceDecision *
device_for (
    const AtmRepositoryCapacityAdmissionResult *result,
    guint64 device_id
)
{
    for (guint i = 0;
         i < result->decision.device_count;
         i++) {
        if (result->decision.devices[i].device_id ==
            device_id) {
            return &result->decision.devices[i];
        }
    }

    return NULL;
}

static AtmArchiveInspection
inspection_with_entries (
    guint64 entries
)
{
    return (AtmArchiveInspection) {
        .archive_entries = entries,
        .materialized_entries = entries,
        .regular_files = entries,
        .directories = 0,
        .logical_regular_bytes = 1,
        .largest_regular_file_bytes = 1
    };
}

static void
test_exact_download_threshold (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmCapacityRootAvailability cache =
        availability (
            10,
            200704,
            1,
            TRUE
        );

    g_assert_true (
        atm_repository_capacity_download_evaluate (
            "cbd",
            CBD_SHA,
            &cache,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.byte_prediction_qualified);
    g_assert_true (result.admitted);
    g_assert_false (
        result.must_admit_before_quarantine
    );
    g_assert_cmpuint (
        result.decision.device_count,
        ==,
        1
    );

    const AtmCapacityDeviceDecision *device =
        device_for (&result, 10);
    g_assert_nonnull (device);
    g_assert_cmpuint (
        device->required_bytes,
        ==,
        200704
    );
    g_assert_cmpuint (
        device->required_inodes,
        ==,
        1
    );

    cache.available_bytes = 200703;

    g_assert_true (
        atm_repository_capacity_download_evaluate (
            "cbd",
            CBD_SHA,
            &cache,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);
}

static void
test_unknown_download_skips_byte_rejection (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmCapacityRootAvailability cache =
        availability (
            11,
            0,
            1,
            TRUE
        );

    g_assert_true (
        atm_repository_capacity_download_evaluate (
            "cbd",
            UNKNOWN_SHA,
            &cache,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        result.byte_prediction_qualified
    );
    g_assert_true (result.admitted);

    const AtmCapacityDeviceDecision *device =
        device_for (&result, 11);
    g_assert_nonnull (device);
    g_assert_cmpuint (
        device->required_bytes,
        ==,
        0
    );
    g_assert_cmpuint (
        device->required_inodes,
        ==,
        1
    );

    cache.available_inodes = 0;

    g_assert_true (
        atm_repository_capacity_download_evaluate (
            "cbd",
            UNKNOWN_SHA,
            &cache,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);

    cache.inode_budget_known = FALSE;

    g_assert_true (
        atm_repository_capacity_download_evaluate (
            "cbd",
            UNKNOWN_SHA,
            &cache,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.admitted);
}

static void
test_exact_mutation_same_filesystem_aggregates (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmArchiveInspection inspection =
        inspection_with_entries (10);
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ];

    for (guint i = 0; i < ATM_CAPACITY_ROOT_COUNT; i++) {
        roots[i] = availability (
            20,
            2129920,
            23,
            TRUE
        );
    }

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            CBD_SHA,
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.byte_prediction_qualified);
    g_assert_true (result.admitted);
    g_assert_false (
        result.must_admit_before_quarantine
    );

    const AtmCapacityDeviceDecision *device =
        device_for (&result, 20);
    g_assert_nonnull (device);
    g_assert_cmpuint (
        device->required_bytes,
        ==,
        2129920
    );
    g_assert_cmpuint (
        device->required_inodes,
        ==,
        23
    );

    for (guint i = 0; i < ATM_CAPACITY_ROOT_COUNT; i++) {
        roots[i].available_bytes = 2129919;
    }

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            CBD_SHA,
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);

    for (guint i = 0; i < ATM_CAPACITY_ROOT_COUNT; i++) {
        roots[i].available_bytes = 2129920;
        roots[i].available_inodes = 22;
    }

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            CBD_SHA,
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);
}

static void
test_exact_mutation_split_filesystems (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmArchiveInspection inspection =
        inspection_with_entries (10);
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        availability (
            31,
            987136,
            15,
            TRUE
        ),
        availability (
            32,
            1011712,
            4,
            TRUE
        ),
        availability (
            33,
            131072,
            4,
            TRUE
        )
    };

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            CBD_SHA,
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.admitted);
    g_assert_cmpuint (
        result.decision.device_count,
        ==,
        3
    );

    const AtmCapacityDeviceDecision *data =
        device_for (&result, 31);
    const AtmCapacityDeviceDecision *cache =
        device_for (&result, 32);
    const AtmCapacityDeviceDecision *state =
        device_for (&result, 33);

    g_assert_nonnull (data);
    g_assert_nonnull (cache);
    g_assert_nonnull (state);
    g_assert_cmpuint (
        data->required_bytes,
        ==,
        987136
    );
    g_assert_cmpuint (
        cache->required_bytes,
        ==,
        1011712
    );
    g_assert_cmpuint (
        state->required_bytes,
        ==,
        131072
    );

    roots[ATM_CAPACITY_ROOT_CACHE].
        available_bytes = 1011711;

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            CBD_SHA,
            ATM_CAPACITY_OPERATION_FRESH_INSTALL,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);
}

static void
test_unknown_mutation_keeps_inode_and_state_guards (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmArchiveInspection inspection =
        inspection_with_entries (10);
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        availability (
            41,
            0,
            10,
            TRUE
        ),
        availability (
            42,
            0,
            4,
            TRUE
        ),
        availability (
            43,
            131072,
            4,
            TRUE
        )
    };

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            UNKNOWN_SHA,
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        result.byte_prediction_qualified
    );
    g_assert_true (result.admitted);

    const AtmCapacityDeviceDecision *data =
        device_for (&result, 41);
    const AtmCapacityDeviceDecision *cache =
        device_for (&result, 42);
    const AtmCapacityDeviceDecision *state =
        device_for (&result, 43);

    g_assert_nonnull (data);
    g_assert_nonnull (cache);
    g_assert_nonnull (state);
    g_assert_cmpuint (
        data->required_bytes,
        ==,
        0
    );
    g_assert_cmpuint (
        cache->required_bytes,
        ==,
        0
    );
    g_assert_cmpuint (
        state->required_bytes,
        ==,
        131072
    );
    g_assert_cmpuint (
        data->required_inodes,
        ==,
        10
    );
    g_assert_cmpuint (
        cache->required_inodes,
        ==,
        4
    );
    g_assert_cmpuint (
        state->required_inodes,
        ==,
        4
    );

    roots[ATM_CAPACITY_ROOT_DATA].
        available_inodes = 9;

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            UNKNOWN_SHA,
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);

    roots[ATM_CAPACITY_ROOT_DATA].
        available_inodes = 0;
    roots[ATM_CAPACITY_ROOT_DATA].
        inode_budget_known = FALSE;

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            UNKNOWN_SHA,
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.admitted);
}

static void
test_same_sha_repair_marks_pre_quarantine_gate (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmArchiveInspection inspection =
        inspection_with_entries (10);
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        availability (
            51,
            G_MAXUINT64,
            G_MAXUINT64,
            TRUE
        ),
        availability (
            52,
            G_MAXUINT64,
            G_MAXUINT64,
            TRUE
        ),
        availability (
            53,
            G_MAXUINT64,
            G_MAXUINT64,
            TRUE
        )
    };

    g_assert_true (
        atm_repository_capacity_mutation_evaluate (
            "cbd",
            CBD_SHA,
            ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR,
            &inspection,
            roots,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.admitted);
    g_assert_true (
        result.must_admit_before_quarantine
    );
}

static void
test_state_commit_guard_boundary (void)
{
    AtmRepositoryCapacityAdmissionResult result;
    GError *error = NULL;
    AtmCapacityRootAvailability state =
        availability (
            61,
            131072,
            4,
            TRUE
        );

    g_assert_true (
        atm_repository_capacity_state_commit_evaluate (
            &state,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.admitted);

    const AtmCapacityDeviceDecision *device =
        device_for (&result, 61);
    g_assert_nonnull (device);
    g_assert_cmpuint (
        device->required_bytes,
        ==,
        131072
    );
    g_assert_cmpuint (
        device->required_inodes,
        ==,
        4
    );

    state.available_bytes = 131071;

    g_assert_true (
        atm_repository_capacity_state_commit_evaluate (
            &state,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);

    state.available_bytes = 131072;
    state.available_inodes = 3;

    g_assert_true (
        atm_repository_capacity_state_commit_evaluate (
            &state,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (result.admitted);

    state.available_inodes = 0;
    state.inode_budget_known = FALSE;

    g_assert_true (
        atm_repository_capacity_state_commit_evaluate (
            &state,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (result.admitted);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/repository-capacity-admission/exact-download",
        test_exact_download_threshold
    );
    g_test_add_func (
        "/repository-capacity-admission/unknown-download",
        test_unknown_download_skips_byte_rejection
    );
    g_test_add_func (
        "/repository-capacity-admission/same-filesystem",
        test_exact_mutation_same_filesystem_aggregates
    );
    g_test_add_func (
        "/repository-capacity-admission/split-filesystems",
        test_exact_mutation_split_filesystems
    );
    g_test_add_func (
        "/repository-capacity-admission/unknown-mutation",
        test_unknown_mutation_keeps_inode_and_state_guards
    );
    g_test_add_func (
        "/repository-capacity-admission/repair-pre-quarantine",
        test_same_sha_repair_marks_pre_quarantine_gate
    );
    g_test_add_func (
        "/repository-capacity-admission/state-commit-boundary",
        test_state_commit_guard_boundary
    );

    return g_test_run ();
}
