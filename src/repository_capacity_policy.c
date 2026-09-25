#include "repository_capacity_policy.h"

#include <string.h>

typedef struct {
    const char *repository_id;
    const char *repository_sha;
    guint64 archive_allocated_bytes;
    guint64 snapshot_allocated_bytes;
    guint64 fresh_data_additional_peak_bytes;
    guint64 index_cache_additional_peak_bytes;
    guint64 same_sha_repair_data_additional_peak_bytes;
} AtmRepositoryCapacityExactProfile;

#define ATM_CAPACITY_EXACT_PROFILE( \
    repository_id_value, \
    repository_sha_value, \
    archive_bytes_value, \
    snapshot_bytes_value, \
    fresh_bytes_value, \
    index_bytes_value, \
    repair_bytes_value \
) \
    { \
        repository_id_value, \
        repository_sha_value, \
        (guint64) archive_bytes_value, \
        (guint64) snapshot_bytes_value, \
        (guint64) fresh_bytes_value, \
        (guint64) index_bytes_value, \
        (guint64) repair_bytes_value \
    },

static const AtmRepositoryCapacityExactProfile EXACT_PROFILES[] = {
#include "repository_capacity_profiles.inc"
};

#undef ATM_CAPACITY_EXACT_PROFILE

GQuark
atm_repository_capacity_policy_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-repository-capacity-policy-error-quark"
    );
}

static gboolean
sha40_is_valid (
    const char *sha
)
{
    if (sha == NULL || strlen (sha) != 40) {
        return FALSE;
    }

    for (gsize i = 0; i < 40; i++) {
        if (!g_ascii_isdigit (sha[i]) &&
            !(sha[i] >= 'a' && sha[i] <= 'f')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
repository_id_supported (
    const char *repository_id
)
{
    return
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
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

static const AtmRepositoryCapacityExactProfile *
lookup_exact_profile (
    const char *repository_id,
    const char *repository_sha
)
{
    for (gsize i = 0; i < G_N_ELEMENTS (EXACT_PROFILES); i++) {
        if (g_strcmp0 (
                EXACT_PROFILES[i].repository_id,
                repository_id
            ) == 0 &&
            g_strcmp0 (
                EXACT_PROFILES[i].repository_sha,
                repository_sha
            ) == 0) {
            return &EXACT_PROFILES[i];
        }
    }

    return NULL;
}

static gboolean
validate_identity (
    const char *repository_id,
    const char *repository_sha,
    GError **error
)
{
    if (!repository_id_supported (repository_id)) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_CAPACITY_POLICY_ERROR,
            ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT,
            "Capacity policy received an unsupported repository ID."
        );
        return FALSE;
    }

    if (!sha40_is_valid (repository_sha)) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_CAPACITY_POLICY_ERROR,
            ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT,
            "Capacity policy received an invalid repository SHA."
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_repository_capacity_policy_build_download (
    const char *repository_id,
    const char *repository_sha,
    AtmRepositoryCapacityDownloadPolicy *out_policy,
    GError **error
)
{
    g_return_val_if_fail (out_policy != NULL, FALSE);

    *out_policy =
        (AtmRepositoryCapacityDownloadPolicy) { 0 };

    if (!validate_identity (
            repository_id,
            repository_sha,
            error
        )) {
        return FALSE;
    }

    const AtmRepositoryCapacityExactProfile *profile =
        lookup_exact_profile (
            repository_id,
            repository_sha
        );

    out_policy->prediction.archive_additional_inodes =
        ATM_CAPACITY_ARCHIVE_FILE_INODES;

    if (profile != NULL) {
        out_policy->byte_prediction_qualified = TRUE;
        out_policy->prediction.archive_additional_bytes =
            profile->archive_allocated_bytes;
    }

    return TRUE;
}

gboolean
atm_repository_capacity_policy_build_mutation (
    const char *repository_id,
    const char *repository_sha,
    AtmCapacityOperationKind operation_kind,
    AtmRepositoryCapacityMutationPolicy *out_policy,
    GError **error
)
{
    g_return_val_if_fail (out_policy != NULL, FALSE);

    *out_policy =
        (AtmRepositoryCapacityMutationPolicy) { 0 };

    if (!validate_identity (
            repository_id,
            repository_sha,
            error
        )) {
        return FALSE;
    }

    if (!operation_kind_valid (operation_kind)) {
        g_set_error_literal (
            error,
            ATM_REPOSITORY_CAPACITY_POLICY_ERROR,
            ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT,
            "Capacity policy received an invalid operation kind."
        );
        return FALSE;
    }

    const AtmRepositoryCapacityExactProfile *profile =
        lookup_exact_profile (
            repository_id,
            repository_sha
        );

    out_policy->prediction.index_additional_inodes =
        ATM_CAPACITY_INDEX_BUILD_INODES;
    out_policy->prediction.state_additional_bytes =
        ATM_CAPACITY_STATE_COMMIT_HEADROOM_BYTES;
    out_policy->prediction.state_additional_inodes =
        ATM_CAPACITY_STATE_COMMIT_HEADROOM_INODES;

    if (profile == NULL) {
        return TRUE;
    }

    out_policy->byte_prediction_qualified = TRUE;
    out_policy->prediction.index_additional_bytes =
        profile->index_cache_additional_peak_bytes;

    switch (operation_kind) {
        case ATM_CAPACITY_OPERATION_FRESH_INSTALL:
            out_policy->prediction.snapshot_additional_bytes =
                profile->fresh_data_additional_peak_bytes;
            break;

        case ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE:
            out_policy->prediction.snapshot_additional_bytes =
                profile->snapshot_allocated_bytes;
            break;

        case ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR:
            out_policy->prediction.snapshot_additional_bytes =
                profile->same_sha_repair_data_additional_peak_bytes;
            break;

        default:
            g_assert_not_reached ();
    }

    return TRUE;
}
