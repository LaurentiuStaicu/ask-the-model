#include "repository_capacity_ui_bridge.h"

#include "repository_capacity_admission.h"

static char *
format_decision_detail (
    const char *checkpoint,
    const AtmRepositoryCapacityAdmissionResult *result
)
{
    if (result->admitted) {
        return g_strdup ("");
    }

    for (guint i = 0;
         i < result->decision.device_count;
         i++) {
        const AtmCapacityDeviceDecision *device =
            &result->decision.devices[i];

        if (device->bytes_sufficient &&
            device->inodes_sufficient) {
            continue;
        }

        if (!device->bytes_sufficient &&
            !device->inodes_sufficient &&
            device->inode_budget_known) {
            return g_strdup_printf (
                "%s local-capacity check failed on filesystem device %" G_GUINT64_FORMAT
                ": %" G_GUINT64_FORMAT " bytes and %" G_GUINT64_FORMAT
                " inode/file slots are required, but only %" G_GUINT64_FORMAT
                " bytes and %" G_GUINT64_FORMAT " slots are available.",
                checkpoint,
                device->device_id,
                device->required_bytes,
                device->required_inodes,
                device->available_bytes,
                device->available_inodes
            );
        }

        if (!device->bytes_sufficient) {
            return g_strdup_printf (
                "%s local-capacity check failed on filesystem device %" G_GUINT64_FORMAT
                ": %" G_GUINT64_FORMAT " bytes are required, but only %"
                G_GUINT64_FORMAT " bytes are available.",
                checkpoint,
                device->device_id,
                device->required_bytes,
                device->available_bytes
            );
        }

        if (!device->inodes_sufficient &&
            device->inode_budget_known) {
            return g_strdup_printf (
                "%s local-capacity check failed on filesystem device %" G_GUINT64_FORMAT
                ": %" G_GUINT64_FORMAT " inode/file slots are required, but only %"
                G_GUINT64_FORMAT " are available.",
                checkpoint,
                device->device_id,
                device->required_inodes,
                device->available_inodes
            );
        }
    }

    return g_strdup_printf (
        "%s local-capacity check rejected the operation.",
        checkpoint
    );
}

static gboolean
publish_result (
    const char *checkpoint,
    const AtmRepositoryCapacityAdmissionResult *result,
    gboolean *out_admitted,
    gboolean *out_byte_prediction_qualified,
    gboolean *out_must_admit_before_quarantine,
    char **out_detail
)
{
    g_return_val_if_fail (result != NULL, FALSE);
    g_return_val_if_fail (out_admitted != NULL, FALSE);
    g_return_val_if_fail (out_detail != NULL, FALSE);
    g_return_val_if_fail (*out_detail == NULL, FALSE);

    *out_admitted = result->admitted;

    if (out_byte_prediction_qualified != NULL) {
        *out_byte_prediction_qualified =
            result->byte_prediction_qualified;
    }

    if (out_must_admit_before_quarantine != NULL) {
        *out_must_admit_before_quarantine =
            result->must_admit_before_quarantine;
    }

    *out_detail =
        format_decision_detail (
            checkpoint,
            result
        );
    return TRUE;
}

gboolean
atm_repository_capacity_ui_download_preflight (
    const char *cache_path,
    const char *repository_id,
    const char *repository_sha,
    gboolean *out_admitted,
    gboolean *out_byte_prediction_qualified,
    char **out_detail,
    GError **error
)
{
    AtmRepositoryCapacityAdmissionResult result;

    g_return_val_if_fail (out_admitted != NULL, FALSE);
    g_return_val_if_fail (
        out_byte_prediction_qualified != NULL,
        FALSE
    );
    g_return_val_if_fail (out_detail != NULL, FALSE);
    g_return_val_if_fail (*out_detail == NULL, FALSE);

    if (!atm_repository_capacity_download_preflight (
            cache_path,
            repository_id,
            repository_sha,
            &result,
            error
        )) {
        return FALSE;
    }

    return publish_result (
        "Pre-download",
        &result,
        out_admitted,
        out_byte_prediction_qualified,
        NULL,
        out_detail
    );
}

gboolean
atm_repository_capacity_ui_mutation_preflight (
    const char *data_path,
    const char *cache_path,
    const char *state_path,
    const char *archive_path,
    const char *repository_id,
    const char *repository_sha,
    int operation_kind,
    gboolean *out_admitted,
    gboolean *out_byte_prediction_qualified,
    gboolean *out_must_admit_before_quarantine,
    char **out_detail,
    GError **error
)
{
    AtmRepositoryCapacityAdmissionResult result;

    g_return_val_if_fail (out_admitted != NULL, FALSE);
    g_return_val_if_fail (
        out_byte_prediction_qualified != NULL,
        FALSE
    );
    g_return_val_if_fail (
        out_must_admit_before_quarantine != NULL,
        FALSE
    );
    g_return_val_if_fail (out_detail != NULL, FALSE);
    g_return_val_if_fail (*out_detail == NULL, FALSE);

    if (!atm_repository_capacity_mutation_preflight (
            data_path,
            cache_path,
            state_path,
            archive_path,
            repository_id,
            repository_sha,
            operation_kind,
            &result,
            error
        )) {
        return FALSE;
    }

    return publish_result (
        "Post-download",
        &result,
        out_admitted,
        out_byte_prediction_qualified,
        out_must_admit_before_quarantine,
        out_detail
    );
}

gboolean
atm_repository_capacity_ui_state_commit_preflight (
    const char *state_path,
    gboolean *out_admitted,
    char **out_detail,
    GError **error
)
{
    AtmRepositoryCapacityAdmissionResult result;

    g_return_val_if_fail (out_admitted != NULL, FALSE);
    g_return_val_if_fail (out_detail != NULL, FALSE);
    g_return_val_if_fail (*out_detail == NULL, FALSE);

    if (!atm_repository_capacity_state_commit_preflight (
            state_path,
            &result,
            error
        )) {
        return FALSE;
    }

    return publish_result (
        "State-publication",
        &result,
        out_admitted,
        NULL,
        NULL,
        out_detail
    );
}
