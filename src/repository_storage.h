#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_STORAGE_ERROR_INVALID_ID,
    ATM_STORAGE_ERROR_INVALID_SHA,
    ATM_STORAGE_ERROR_INVALID_STAGING,
    ATM_STORAGE_ERROR_INVALID_SNAPSHOT,
    ATM_STORAGE_ERROR_EXISTS,
    ATM_STORAGE_ERROR_IO,
    ATM_STORAGE_ERROR_NO_SPACE
} AtmStorageError;

#define ATM_STORAGE_ERROR (atm_storage_error_quark ())

GQuark atm_storage_error_quark (void);

char *atm_repository_snapshot_path (
    const char *data_root,
    const char *repository_id,
    const char *sha
);

char *atm_repository_extraction_staging_path (
    const char *data_root,
    const char *repository_id,
    const char *sha
);

gboolean atm_repository_quarantine_snapshot (
    const char *data_root,
    const char *repository_id,
    const char *sha,
    char **out_quarantine_path,
    GError **error
);

gboolean atm_repository_promote_snapshot (
    const char *data_root,
    const char *repository_id,
    const char *sha,
    const char *staging_path,
    char **out_snapshot_path,
    GError **error
);

G_END_DECLS
