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
