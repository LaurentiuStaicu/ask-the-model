#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CONTROL_STATE_APPLICATION_ID 0x41544D31
#define ATM_CONTROL_STATE_SCHEMA_VERSION 2
#define ATM_CONTROL_STATE_SCHEMA_ID "atm-control-state/2"

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

typedef struct {
    char *repository_id;
    char *snapshot_sha;
} AtmControlStateSnapshotReference;

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

gboolean atm_control_state_active_generation_id (
    const char *path,
    gint64 *out_generation_id,
    GError **error
);

/*
 * Reads the active COMPLETE repository generation through the strict
 * read-only/no-follow Control DB path. The database must already exist at the
 * current schema; this function never bootstraps or migrates authority.
 */
gboolean atm_control_state_active_generation_id_readonly (
    const char *path,
    gint64 *out_generation_id,
    GError **error
);

/*
 * Reads one repository row from an immutable COMPLETE generation through the
 * strict read-only/no-follow Control DB path. The database must already exist
 * at the current schema; this function never bootstraps or migrates authority.
 */
gboolean atm_control_state_load_repository_values_at_generation_readonly (
    const char *path,
    gint64 generation_id,
    const char *repository_id,
    gboolean *out_present,
    char **out_snapshot_sha,
    char **out_repository_version,
    char **out_snapshot_seal_sha256,
    GError **error
);

gboolean atm_control_state_load_repository_values_at_generation (
    const char *path,
    gint64 generation_id,
    const char *repository_id,
    gboolean *out_present,
    char **out_snapshot_sha,
    char **out_repository_version,
    char **out_snapshot_seal_sha256,
    GError **error
);

/*
 * Counts immutable COMPLETE-generation references to one exact repository
 * snapshot through a genuinely read-only Control DB connection. This query
 * never bootstraps or migrates schema. A zero count is proof of Control DB
 * non-protection only; any future filesystem recovery action must additionally
 * hold an authority-wide exclusion that covers every Control DB writer.
 */
gboolean atm_control_state_count_complete_snapshot_references (
    const char *path,
    const char *repository_id,
    const char *snapshot_sha,
    guint64 *out_reference_count,
    GError **error
);

/*
 * Resolves one immutable COMPLETE repository generation to the exact
 * repository/SHA pairs it protects. The query opens the Control DB through
 * the strict read-only/no-follow path and never bootstraps or migrates state.
 *
 * The returned array is owned by the caller and must be released with
 * atm_control_state_snapshot_references_free().
 */
gboolean atm_control_state_list_generation_snapshot_references_readonly (
    const char *path,
    gint64 generation_id,
    AtmControlStateSnapshotReference **out_references,
    gsize *out_count,
    GError **error
);

void atm_control_state_snapshot_references_free (
    AtmControlStateSnapshotReference *references,
    gsize count
);

/*
 * Lists every persisted COMPLETE repository generation in ascending identifier
 * order through the strict read-only/no-follow Control DB path. The database
 * must already exist at the current schema; this function never bootstraps or
 * migrates authority.
 *
 * The returned array is owned by the caller and must be released with g_free().
 */
gboolean atm_control_state_list_complete_generation_ids_readonly (
    const char *path,
    gint64 **out_generation_ids,
    gint *out_count,
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

gboolean atm_control_state_set_current_values_guarded (
    const char *path,
    gint64 expected_generation_id,
    const char *repository_id,
    const char *snapshot_sha,
    const char *repository_version,
    const char *snapshot_seal_sha256,
    gint64 *out_generation_id,
    GError **error
);

gboolean atm_control_state_set_snapshot_seal_values (
    const char *path,
    const char *repository_id,
    const char *expected_snapshot_sha,
    const char *snapshot_seal_sha256,
    GError **error
);

gboolean atm_control_state_set_snapshot_seal_values_guarded (
    const char *path,
    gint64 expected_generation_id,
    const char *repository_id,
    const char *expected_snapshot_sha,
    const char *snapshot_seal_sha256,
    gint64 *out_generation_id,
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
