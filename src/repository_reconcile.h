#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_REPOSITORY_RECONCILE_SNAPSHOT_MISSING,
    ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID,
    ATM_REPOSITORY_RECONCILE_INDEX_ERROR,
    ATM_REPOSITORY_RECONCILE_READY,
    ATM_REPOSITORY_RECONCILE_READY_REPAIRED_INDEX
} AtmRepositoryReconcileStatus;

typedef enum {
    ATM_REPOSITORY_RECONCILE_ERROR_ARGUMENT
} AtmRepositoryReconcileError;

#define ATM_REPOSITORY_RECONCILE_ERROR \
    (atm_repository_reconcile_error_quark ())

GQuark atm_repository_reconcile_error_quark (void);

typedef struct {
    AtmRepositoryReconcileStatus status;
    char *reason_code;
    char *detail;
    char *repository_version;
    char *index_path;
} AtmRepositoryReconcileResult;

gboolean atm_repository_reconcile_local (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *snapshot_sha,
    const char *persisted_version,
    AtmRepositoryReconcileResult **out_result,
    GError **error
);

void atm_repository_reconcile_result_free (
    AtmRepositoryReconcileResult *result
);

G_END_DECLS
