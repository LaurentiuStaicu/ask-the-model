#pragma once

#include <glib.h>

#include "archive_extract.h"
#include "capacity_admission_model.h"

G_BEGIN_DECLS

typedef struct {
    gboolean byte_prediction_qualified;
    gboolean admitted;
    gboolean must_admit_before_quarantine;
    AtmCapacityAdmissionDecision decision;
} AtmRepositoryCapacityAdmissionResult;

gboolean atm_repository_capacity_download_evaluate (
    const char *repository_id,
    const char *repository_sha,
    const AtmCapacityRootAvailability *cache_availability,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
);

gboolean atm_repository_capacity_mutation_evaluate (
    const char *repository_id,
    const char *repository_sha,
    int operation_kind,
    const AtmArchiveInspection *archive_inspection,
    const AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT],
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
);

gboolean atm_repository_capacity_state_commit_evaluate (
    const AtmCapacityRootAvailability *state_availability,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
);

gboolean atm_repository_capacity_download_preflight (
    const char *cache_path,
    const char *repository_id,
    const char *repository_sha,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
);

gboolean atm_repository_capacity_mutation_preflight (
    const char *data_path,
    const char *cache_path,
    const char *state_path,
    const char *archive_path,
    const char *repository_id,
    const char *repository_sha,
    int operation_kind,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
);

gboolean atm_repository_capacity_state_commit_preflight (
    const char *state_path,
    AtmRepositoryCapacityAdmissionResult *out_result,
    GError **error
);

G_END_DECLS
