#pragma once

#include <glib.h>

#include "capacity_operation_plan.h"

G_BEGIN_DECLS

#define ATM_CAPACITY_STATE_COMMIT_HEADROOM_BYTES ((guint64) 131072)
#define ATM_CAPACITY_STATE_COMMIT_HEADROOM_INODES ((guint64) 4)
#define ATM_CAPACITY_INDEX_BUILD_INODES ((guint64) 4)
#define ATM_CAPACITY_ARCHIVE_FILE_INODES ((guint64) 1)

typedef enum {
    ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT
} AtmRepositoryCapacityPolicyError;

#define ATM_REPOSITORY_CAPACITY_POLICY_ERROR \
    (atm_repository_capacity_policy_error_quark ())

typedef struct {
    gboolean byte_prediction_qualified;
    AtmCapacityDownloadPrediction prediction;
} AtmRepositoryCapacityDownloadPolicy;

typedef struct {
    gboolean byte_prediction_qualified;
    AtmCapacityMutationPrediction prediction;
} AtmRepositoryCapacityMutationPolicy;

GQuark atm_repository_capacity_policy_error_quark (void);

gboolean atm_repository_capacity_policy_build_download (
    const char *repository_id,
    const char *repository_sha,
    AtmRepositoryCapacityDownloadPolicy *out_policy,
    GError **error
);

gboolean atm_repository_capacity_policy_build_mutation (
    const char *repository_id,
    const char *repository_sha,
    AtmCapacityOperationKind operation_kind,
    AtmRepositoryCapacityMutationPolicy *out_policy,
    GError **error
);

G_END_DECLS
