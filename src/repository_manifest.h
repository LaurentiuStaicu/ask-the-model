#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_MANIFEST_ERROR_PARSE,
    ATM_MANIFEST_ERROR_SHAPE,
    ATM_MANIFEST_ERROR_IDENTITY,
    ATM_MANIFEST_ERROR_PATH,
    ATM_MANIFEST_ERROR_MISSING_PATH,
    ATM_MANIFEST_ERROR_VERSION,
    ATM_MANIFEST_ERROR_STATUS
} AtmManifestError;

#define ATM_MANIFEST_ERROR (atm_manifest_error_quark ())

GQuark atm_manifest_error_quark (void);

gboolean atm_repository_validate_snapshot (
    const char *snapshot_root,
    const char *expected_id,
    const char *expected_acronym,
    const char *expected_display_name,
    char **out_version,
    GError **error
);

G_END_DECLS
