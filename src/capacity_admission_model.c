#include "capacity_admission_model.h"

GQuark
atm_capacity_admission_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-capacity-admission-error-quark"
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
            ATM_CAPACITY_ADMISSION_ERROR,
            ATM_CAPACITY_ADMISSION_ERROR_OVERFLOW,
            "Capacity admission arithmetic overflowed."
        );
        return FALSE;
    }

    *out = left + right;
    return TRUE;
}

static gint
find_device (
    const AtmCapacityAdmissionDecision *decision,
    guint64 device_id
)
{
    for (guint i = 0;
         i < decision->device_count;
         i++) {
        if (decision->devices[i].device_id ==
            device_id) {
            return (gint) i;
        }
    }

    return -1;
}

static gboolean
build_device_groups (
    const AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT],
    const AtmCapacityReservePolicy *reserve_policy,
    AtmCapacityAdmissionDecision *decision,
    GError **error
)
{
    for (guint root = 0;
         root < ATM_CAPACITY_ROOT_COUNT;
         root++) {
        gint existing =
            find_device (
                decision,
                roots[root].device_id
            );

        AtmCapacityDeviceDecision *device = NULL;

        if (existing < 0) {
            if (decision->device_count >=
                ATM_CAPACITY_ROOT_COUNT) {
                g_set_error_literal (
                    error,
                    ATM_CAPACITY_ADMISSION_ERROR,
                    ATM_CAPACITY_ADMISSION_ERROR_ARGUMENT,
                    "Capacity admission produced too many filesystem groups."
                );
                return FALSE;
            }

            device =
                &decision->devices[
                    decision->device_count++
                ];

            *device =
                (AtmCapacityDeviceDecision) {
                    .device_id =
                        roots[root].device_id,
                    .root_mask = 0,
                    .available_bytes =
                        roots[root].available_bytes,
                    .available_inodes =
                        roots[root].available_inodes,
                    .inode_budget_known =
                        roots[root].inode_budget_known,
                    .operation_peak_bytes = 0,
                    .operation_peak_inodes = 0,
                    .reserve_bytes = 0,
                    .reserve_inodes = 0,
                    .required_bytes = 0,
                    .required_inodes = 0,
                    .bytes_sufficient = FALSE,
                    .inodes_sufficient = FALSE
                };
        } else {
            device =
                &decision->devices[existing];

            if (device->inode_budget_known !=
                roots[root].inode_budget_known) {
                g_set_error_literal (
                    error,
                    ATM_CAPACITY_ADMISSION_ERROR,
                    ATM_CAPACITY_ADMISSION_ERROR_INCONSISTENT_FILESYSTEM,
                    "Roots on one filesystem disagree about inode-budget availability."
                );
                return FALSE;
            }

            device->available_bytes =
                MIN (
                    device->available_bytes,
                    roots[root].available_bytes
                );

            if (device->inode_budget_known) {
                device->available_inodes =
                    MIN (
                        device->available_inodes,
                        roots[root].available_inodes
                    );
            }
        }

        device->root_mask |=
            1u << root;

        if (reserve_policy != NULL) {
            /*
             * A reserve is headroom that must remain after the
             * operation. When several AtM roots share one
             * filesystem, the same remaining pool satisfies
             * those headroom constraints, so use the strictest
             * root reserve rather than summing duplicate reserve
             * floors.
             */
            device->reserve_bytes =
                MAX (
                    device->reserve_bytes,
                    reserve_policy->bytes[root]
                );

            if (device->inode_budget_known) {
                device->reserve_inodes =
                    MAX (
                        device->reserve_inodes,
                        reserve_policy->inodes[root]
                    );
            }
        }
    }

    return TRUE;
}

static gboolean
accumulate_phase_for_device (
    const AtmCapacityDeviceDecision *device,
    const AtmCapacityPhaseRequirement *phase,
    guint64 *out_bytes,
    guint64 *out_inodes,
    GError **error
)
{
    guint64 bytes = 0;
    guint64 inodes = 0;

    for (guint root = 0;
         root < ATM_CAPACITY_ROOT_COUNT;
         root++) {
        if ((device->root_mask &
             (1u << root)) == 0) {
            continue;
        }

        if (!add_checked (
                bytes,
                phase->bytes[root],
                &bytes,
                error
            ) ||
            !add_checked (
                inodes,
                phase->inodes[root],
                &inodes,
                error
            )) {
            return FALSE;
        }
    }

    *out_bytes = bytes;
    *out_inodes = inodes;
    return TRUE;
}

gboolean
atm_capacity_admission_evaluate (
    const AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT],
    const AtmCapacityPhaseRequirement *phases,
    gsize phase_count,
    const AtmCapacityReservePolicy *reserve_policy,
    AtmCapacityAdmissionDecision *out_decision,
    GError **error
)
{
    g_return_val_if_fail (roots != NULL, FALSE);
    g_return_val_if_fail (out_decision != NULL, FALSE);

    if (phase_count > 0 && phases == NULL) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_ADMISSION_ERROR,
            ATM_CAPACITY_ADMISSION_ERROR_ARGUMENT,
            "Capacity admission phases are missing."
        );
        return FALSE;
    }

    *out_decision =
        (AtmCapacityAdmissionDecision) { 0 };

    if (!build_device_groups (
            roots,
            reserve_policy,
            out_decision,
            error
        )) {
        return FALSE;
    }

    for (guint device_index = 0;
         device_index < out_decision->device_count;
         device_index++) {
        AtmCapacityDeviceDecision *device =
            &out_decision->devices[device_index];

        for (gsize phase_index = 0;
             phase_index < phase_count;
             phase_index++) {
            guint64 phase_bytes = 0;
            guint64 phase_inodes = 0;

            if (!accumulate_phase_for_device (
                    device,
                    &phases[phase_index],
                    &phase_bytes,
                    &phase_inodes,
                    error
                )) {
                return FALSE;
            }

            device->operation_peak_bytes =
                MAX (
                    device->operation_peak_bytes,
                    phase_bytes
                );

            device->operation_peak_inodes =
                MAX (
                    device->operation_peak_inodes,
                    phase_inodes
                );
        }

        if (!add_checked (
                device->operation_peak_bytes,
                device->reserve_bytes,
                &device->required_bytes,
                error
            )) {
            return FALSE;
        }

        device->bytes_sufficient =
            device->available_bytes >=
            device->required_bytes;

        if (device->inode_budget_known) {
            if (!add_checked (
                    device->operation_peak_inodes,
                    device->reserve_inodes,
                    &device->required_inodes,
                    error
                )) {
                return FALSE;
            }

            device->inodes_sufficient =
                device->available_inodes >=
                device->required_inodes;
        } else {
            device->required_inodes = 0;
            device->inodes_sufficient = TRUE;
        }
    }

    out_decision->admitted = TRUE;

    for (guint i = 0;
         i < out_decision->device_count;
         i++) {
        if (!out_decision->devices[i].bytes_sufficient ||
            !out_decision->devices[i].inodes_sufficient) {
            out_decision->admitted = FALSE;
            break;
        }
    }

    return TRUE;
}
