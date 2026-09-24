#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_RETRIEVAL_ENSURE_REUSED,
    ATM_RETRIEVAL_ENSURE_REBUILT
} AtmRetrievalEnsureResult;

typedef enum {
    ATM_RETRIEVAL_LIFECYCLE_ERROR_SNAPSHOT,
    ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE,
    ATM_RETRIEVAL_LIFECYCLE_ERROR_VERSION
} AtmRetrievalLifecycleError;

#define ATM_RETRIEVAL_LIFECYCLE_ERROR \
    (atm_retrieval_lifecycle_error_quark ())

GQuark atm_retrieval_lifecycle_error_quark (void);

gboolean atm_retrieval_index_ensure_for_snapshot (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_index_path,
    char **out_repository_version,
    AtmRetrievalEnsureResult *out_result,
    GError **error
);

gboolean atm_retrieval_index_ensure_for_snapshot_coordinated (
    const char *state_root,
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_index_path,
    char **out_repository_version,
    AtmRetrievalEnsureResult *out_result,
    GError **error
);

#ifdef ATM_TEST_INDEX_SINGLE_FLIGHT
void atm_retrieval_index_single_flight_test_reset (void);
gint atm_retrieval_index_single_flight_test_build_entries (void);
#endif

G_END_DECLS
