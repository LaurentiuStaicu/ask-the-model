#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_SNAPSHOT_SEAL_ABSENT,
    ATM_SNAPSHOT_SEAL_VALID,
    ATM_SNAPSHOT_SEAL_INVALID,
    ATM_SNAPSHOT_SEAL_MISMATCH
} AtmSnapshotSealStatus;

typedef enum {
    ATM_SNAPSHOT_SEAL_ERROR_ARGUMENT,
    ATM_SNAPSHOT_SEAL_ERROR_IO,
    ATM_SNAPSHOT_SEAL_ERROR_EXISTS,
    ATM_SNAPSHOT_SEAL_ERROR_INTEGRITY
} AtmSnapshotSealError;

#define ATM_SNAPSHOT_SEAL_ERROR \
    (atm_snapshot_seal_error_quark ())

GQuark atm_snapshot_seal_error_quark (void);

gboolean atm_snapshot_seal_create (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    const char *baseline_kind,
    char **out_seal_path,
    char **out_root_sha256,
    GError **error
);

gboolean atm_snapshot_seal_check (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    AtmSnapshotSealStatus *out_status,
    char **out_detail,
    char **out_root_sha256,
    GError **error
);

gboolean atm_snapshot_seal_ensure_install (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    gboolean *out_created,
    char **out_seal_path,
    char **out_root_sha256,
    GError **error
);

G_END_DECLS
