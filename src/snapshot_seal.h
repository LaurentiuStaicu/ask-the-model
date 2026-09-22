#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_SNAPSHOT_SEAL_ERROR_IO,
    ATM_SNAPSHOT_SEAL_ERROR_UNSAFE_ENTRY,
    ATM_SNAPSHOT_SEAL_ERROR_INVALID_NAME
} AtmSnapshotSealError;

#define ATM_SNAPSHOT_SEAL_ERROR (atm_snapshot_seal_error_quark ())

GQuark atm_snapshot_seal_error_quark (void);

gboolean atm_snapshot_seal_compute (
    const char *snapshot_root,
    char **out_sha256,
    guint64 *out_file_count,
    guint64 *out_total_bytes,
    GError **error
);

G_END_DECLS
