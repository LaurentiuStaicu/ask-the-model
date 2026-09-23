#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CONTROL_STATE_APPLICATION_ID 0x41544D31
#define ATM_CONTROL_STATE_SCHEMA_VERSION 1
#define ATM_CONTROL_STATE_SCHEMA_ID "atm-control-state/1"

typedef enum {
    ATM_CONTROL_STATE_ERROR_ARGUMENT,
    ATM_CONTROL_STATE_ERROR_IO,
    ATM_CONTROL_STATE_ERROR_SQLITE,
    ATM_CONTROL_STATE_ERROR_IDENTITY,
    ATM_CONTROL_STATE_ERROR_SCHEMA,
    ATM_CONTROL_STATE_ERROR_INTEGRITY,
    ATM_CONTROL_STATE_ERROR_LEGACY_STATE,
    ATM_CONTROL_STATE_ERROR_CONFLICT
} AtmControlStateError;

#define ATM_CONTROL_STATE_ERROR \
    (atm_control_state_error_quark ())

typedef struct AtmControlStateStore AtmControlStateStore;

typedef enum {
    ATM_CONTROL_STATE_CUTOVER_EMPTY,
    ATM_CONTROL_STATE_CUTOVER_IMPORTED_LEGACY
} AtmControlStateCutoverDisposition;

GQuark atm_control_state_error_quark (void);

gboolean atm_control_state_open (
    const char *path,
    AtmControlStateStore **out_store,
    GError **error
);

void atm_control_state_close (
    AtmControlStateStore *store
);

gboolean atm_control_state_validate (
    AtmControlStateStore *store,
    GError **error
);

gboolean atm_control_state_load_repository_values (
    const char *path,
    const char *repository_id,
    gboolean *out_present,
    char **out_snapshot_sha,
    char **out_repository_version,
    char **out_snapshot_seal_sha256,
    GError **error
);

gboolean atm_control_state_set_current_values (
    const char *path,
    const char *repository_id,
    const char *snapshot_sha,
    const char *repository_version,
    const char *snapshot_seal_sha256,
    GError **error
);

gboolean atm_control_state_set_snapshot_seal_values (
    const char *path,
    const char *repository_id,
    const char *expected_snapshot_sha,
    const char *snapshot_seal_sha256,
    GError **error
);

gboolean atm_control_state_publish_cutover (
    const char *control_path,
    const char *legacy_json_path,
    AtmControlStateCutoverDisposition *out_disposition,
    GError **error
);

gboolean atm_control_state_import_legacy_json (
    AtmControlStateStore *store,
    const char *legacy_json_path,
    gint64 *out_generation_id,
    GError **error
);

gboolean atm_control_state_generation_matches_legacy_json (
    AtmControlStateStore *store,
    gint64 generation_id,
    const char *legacy_json_path,
    gboolean *out_matches,
    GError **error
);

const char *atm_control_state_path (
    const AtmControlStateStore *store
);

gint64 atm_control_state_application_id (
    const AtmControlStateStore *store
);

gint atm_control_state_schema_version (
    const AtmControlStateStore *store
);

G_END_DECLS
