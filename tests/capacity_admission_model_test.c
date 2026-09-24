#include "capacity_admission_model.h"

#include <glib.h>

static AtmCapacityDeviceDecision *
device_for (
    AtmCapacityAdmissionDecision *decision,
    guint64 device_id
)
{
    for (guint i = 0;
         i < decision->device_count;
         i++) {
        if (decision->devices[i].device_id ==
            device_id) {
            return &decision->devices[i];
        }
    }

    return NULL;
}

static void
test_same_device_uses_phase_overlap (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 11,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 11,
            .available_bytes = 990,
            .available_inodes = 95,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 11,
            .available_bytes = 995,
            .available_inodes = 98,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phases[] = {
        {
            .bytes = {
                [ATM_CAPACITY_ROOT_CACHE] = 100
            },
            .inodes = {
                [ATM_CAPACITY_ROOT_CACHE] = 1
            }
        },
        {
            .bytes = {
                [ATM_CAPACITY_ROOT_DATA] = 500,
                [ATM_CAPACITY_ROOT_CACHE] = 100,
                [ATM_CAPACITY_ROOT_STATE] = 10
            },
            .inodes = {
                [ATM_CAPACITY_ROOT_DATA] = 40,
                [ATM_CAPACITY_ROOT_CACHE] = 1,
                [ATM_CAPACITY_ROOT_STATE] = 1
            }
        },
        {
            .bytes = {
                [ATM_CAPACITY_ROOT_DATA] = 100,
                [ATM_CAPACITY_ROOT_CACHE] = 450,
                [ATM_CAPACITY_ROOT_STATE] = 20
            },
            .inodes = {
                [ATM_CAPACITY_ROOT_DATA] = 5,
                [ATM_CAPACITY_ROOT_CACHE] = 10,
                [ATM_CAPACITY_ROOT_STATE] = 3
            }
        }
    };

    AtmCapacityReservePolicy reserve = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = 50,
            [ATM_CAPACITY_ROOT_CACHE] = 80,
            [ATM_CAPACITY_ROOT_STATE] = 25
        },
        .inodes = {
            [ATM_CAPACITY_ROOT_DATA] = 4,
            [ATM_CAPACITY_ROOT_CACHE] = 7,
            [ATM_CAPACITY_ROOT_STATE] = 2
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            phases,
            G_N_ELEMENTS (phases),
            &reserve,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        decision.device_count,
        ==,
        1
    );

    AtmCapacityDeviceDecision *device =
        &decision.devices[0];

    g_assert_cmpuint (
        device->root_mask,
        ==,
        (1u << ATM_CAPACITY_ROOT_DATA) |
        (1u << ATM_CAPACITY_ROOT_CACHE) |
        (1u << ATM_CAPACITY_ROOT_STATE)
    );
    g_assert_cmpuint (
        device->available_bytes,
        ==,
        990
    );
    g_assert_cmpuint (
        device->available_inodes,
        ==,
        95
    );

    /*
     * The phase peak is 500+100+10=610, not the
     * invalid sum of independent per-root maxima:
     * 500+450+20=970.
     */
    g_assert_cmpuint (
        device->operation_peak_bytes,
        ==,
        610
    );
    g_assert_cmpuint (
        device->reserve_bytes,
        ==,
        80
    );
    g_assert_cmpuint (
        device->required_bytes,
        ==,
        690
    );

    g_assert_cmpuint (
        device->operation_peak_inodes,
        ==,
        42
    );
    g_assert_cmpuint (
        device->reserve_inodes,
        ==,
        7
    );
    g_assert_cmpuint (
        device->required_inodes,
        ==,
        49
    );

    g_assert_true (
        decision.admitted
    );
}

static void
test_split_filesystems_are_independent (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = 600,
            .available_inodes = 50,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = 450,
            .available_inodes = 20,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 3,
            .available_bytes = 100,
            .available_inodes = 10,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phases[] = {
        {
            .bytes = {
                [ATM_CAPACITY_ROOT_DATA] = 500,
                [ATM_CAPACITY_ROOT_CACHE] = 400,
                [ATM_CAPACITY_ROOT_STATE] = 30
            },
            .inodes = {
                [ATM_CAPACITY_ROOT_DATA] = 40,
                [ATM_CAPACITY_ROOT_CACHE] = 5,
                [ATM_CAPACITY_ROOT_STATE] = 2
            }
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            phases,
            G_N_ELEMENTS (phases),
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        decision.device_count,
        ==,
        3
    );
    g_assert_true (
        decision.admitted
    );

    AtmCapacityDeviceDecision *data =
        device_for (
            &decision,
            1
        );
    AtmCapacityDeviceDecision *cache =
        device_for (
            &decision,
            2
        );
    AtmCapacityDeviceDecision *state =
        device_for (
            &decision,
            3
        );

    g_assert_nonnull (data);
    g_assert_nonnull (cache);
    g_assert_nonnull (state);

    g_assert_cmpuint (
        data->required_bytes,
        ==,
        500
    );
    g_assert_cmpuint (
        cache->required_bytes,
        ==,
        400
    );
    g_assert_cmpuint (
        state->required_bytes,
        ==,
        30
    );
}

static void
test_insufficient_cache_bytes_blocks (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = 399,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 3,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_CACHE] = 400
        },
        .inodes = {
            [ATM_CAPACITY_ROOT_CACHE] = 1
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        decision.admitted
    );

    AtmCapacityDeviceDecision *cache =
        device_for (
            &decision,
            2
        );

    g_assert_nonnull (cache);
    g_assert_false (
        cache->bytes_sufficient
    );
    g_assert_true (
        cache->inodes_sufficient
    );
}

static void
test_insufficient_data_bytes_blocks (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = 499,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 3,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = 500
        },
        .inodes = {
            [ATM_CAPACITY_ROOT_DATA] = 1
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        decision.admitted
    );

    AtmCapacityDeviceDecision *data =
        device_for (
            &decision,
            1
        );

    g_assert_nonnull (data);
    g_assert_false (
        data->bytes_sufficient
    );
}

static void
test_insufficient_inode_budget_blocks (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = 1000,
            .available_inodes = 39,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 3,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = 100
        },
        .inodes = {
            [ATM_CAPACITY_ROOT_DATA] = 40
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        decision.admitted
    );

    AtmCapacityDeviceDecision *data =
        device_for (
            &decision,
            1
        );

    g_assert_nonnull (data);
    g_assert_true (
        data->bytes_sufficient
    );
    g_assert_false (
        data->inodes_sufficient
    );
}

static void
test_shared_data_cache_state_separate (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 10,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 10,
            .available_bytes = 900,
            .available_inodes = 90,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 20,
            .available_bytes = 100,
            .available_inodes = 20,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = 300,
            [ATM_CAPACITY_ROOT_CACHE] = 250,
            [ATM_CAPACITY_ROOT_STATE] = 50
        },
        .inodes = {
            [ATM_CAPACITY_ROOT_DATA] = 20,
            [ATM_CAPACITY_ROOT_CACHE] = 5,
            [ATM_CAPACITY_ROOT_STATE] = 2
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        decision.device_count,
        ==,
        2
    );

    AtmCapacityDeviceDecision *shared =
        device_for (
            &decision,
            10
        );
    AtmCapacityDeviceDecision *state =
        device_for (
            &decision,
            20
        );

    g_assert_nonnull (shared);
    g_assert_nonnull (state);
    g_assert_cmpuint (
        shared->required_bytes,
        ==,
        550
    );
    g_assert_cmpuint (
        shared->required_inodes,
        ==,
        25
    );
    g_assert_cmpuint (
        state->required_bytes,
        ==,
        50
    );
    g_assert_cmpuint (
        state->required_inodes,
        ==,
        2
    );
}

static void
test_unknown_inode_budget_does_not_false_reject (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 7,
            .available_bytes = 1000,
            .available_inodes = 0,
            .inode_budget_known = FALSE
        },
        {
            .device_id = 7,
            .available_bytes = 1000,
            .available_inodes = 0,
            .inode_budget_known = FALSE
        },
        {
            .device_id = 7,
            .available_bytes = 1000,
            .available_inodes = 0,
            .inode_budget_known = FALSE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = 100
        },
        .inodes = {
            [ATM_CAPACITY_ROOT_DATA] = 5000
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        decision.admitted
    );
    g_assert_false (
        decision.devices[0].inode_budget_known
    );
    g_assert_true (
        decision.devices[0].inodes_sufficient
    );
    g_assert_cmpuint (
        decision.devices[0].required_inodes,
        ==,
        0
    );
}

static void
test_inconsistent_inode_semantics_rejected (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 7,
            .available_bytes = 1000,
            .available_inodes = 100,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 7,
            .available_bytes = 1000,
            .available_inodes = 0,
            .inode_budget_known = FALSE
        },
        {
            .device_id = 8,
            .available_bytes = 1000,
            .available_inodes = 0,
            .inode_budget_known = FALSE
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_false (
        atm_capacity_admission_evaluate (
            roots,
            NULL,
            0,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_ADMISSION_ERROR,
        ATM_CAPACITY_ADMISSION_ERROR_INCONSISTENT_FILESYSTEM
    );
    g_clear_error (&error);
}

static void
test_phase_sum_overflow_rejected (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = G_MAXUINT64,
            .available_inodes = G_MAXUINT64,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 1,
            .available_bytes = G_MAXUINT64,
            .available_inodes = G_MAXUINT64,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = G_MAXUINT64,
            .available_inodes = G_MAXUINT64,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = G_MAXUINT64,
            [ATM_CAPACITY_ROOT_CACHE] = 1
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_false (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            NULL,
            &decision,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_ADMISSION_ERROR,
        ATM_CAPACITY_ADMISSION_ERROR_OVERFLOW
    );
    g_clear_error (&error);
}

static void
test_reserve_overflow_rejected (void)
{
    AtmCapacityRootAvailability roots[
        ATM_CAPACITY_ROOT_COUNT
    ] = {
        {
            .device_id = 1,
            .available_bytes = G_MAXUINT64,
            .available_inodes = G_MAXUINT64,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 2,
            .available_bytes = G_MAXUINT64,
            .available_inodes = G_MAXUINT64,
            .inode_budget_known = TRUE
        },
        {
            .device_id = 3,
            .available_bytes = G_MAXUINT64,
            .available_inodes = G_MAXUINT64,
            .inode_budget_known = TRUE
        }
    };

    AtmCapacityPhaseRequirement phase = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] =
                G_MAXUINT64
        }
    };

    AtmCapacityReservePolicy reserve = {
        .bytes = {
            [ATM_CAPACITY_ROOT_DATA] = 1
        }
    };

    AtmCapacityAdmissionDecision decision;
    GError *error = NULL;

    g_assert_false (
        atm_capacity_admission_evaluate (
            roots,
            &phase,
            1,
            &reserve,
            &decision,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_ADMISSION_ERROR,
        ATM_CAPACITY_ADMISSION_ERROR_OVERFLOW
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
        "/capacity-admission/same-device-phase-overlap",
        test_same_device_uses_phase_overlap
    );
    g_test_add_func (
        "/capacity-admission/split-filesystems",
        test_split_filesystems_are_independent
    );
    g_test_add_func (
        "/capacity-admission/insufficient-cache-bytes",
        test_insufficient_cache_bytes_blocks
    );
    g_test_add_func (
        "/capacity-admission/insufficient-data-bytes",
        test_insufficient_data_bytes_blocks
    );
    g_test_add_func (
        "/capacity-admission/insufficient-inodes",
        test_insufficient_inode_budget_blocks
    );
    g_test_add_func (
        "/capacity-admission/shared-data-cache-state-separate",
        test_shared_data_cache_state_separate
    );
    g_test_add_func (
        "/capacity-admission/unknown-inode-budget",
        test_unknown_inode_budget_does_not_false_reject
    );
    g_test_add_func (
        "/capacity-admission/inconsistent-inode-semantics",
        test_inconsistent_inode_semantics_rejected
    );
    g_test_add_func (
        "/capacity-admission/phase-overflow",
        test_phase_sum_overflow_rejected
    );
    g_test_add_func (
        "/capacity-admission/reserve-overflow",
        test_reserve_overflow_rejected
    );

    return g_test_run ();
}
