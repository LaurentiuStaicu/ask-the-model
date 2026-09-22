#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
    ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
    ATM_STARTUP_QUALIFICATION_ERROR_IO
} AtmStartupQualificationError;

#define ATM_STARTUP_QUALIFICATION_ERROR \
    (atm_startup_qualification_error_quark ())

GQuark atm_startup_qualification_error_quark (void);

typedef enum {
    ATM_EXECUTION_MODE_DEVELOPMENT,
    ATM_EXECUTION_MODE_FLATPAK
} AtmExecutionMode;

typedef struct {
    AtmExecutionMode execution_mode;
    gboolean platform_qualified;

    char *application_id;
    char *application_ref;
    char *application_commit;

    char *runtime_ref;
    char *runtime_commit;

    char *architecture;
    char *branch;
    char *flatpak_version;

    char **application_extensions;
    char **runtime_extensions;

    char *platform_fingerprint;
} AtmDeploymentQualification;

typedef struct {
    gboolean qualified;
    gboolean created;
    guint32 mode;
    guint64 owner_uid;
} AtmStorageQualification;

gboolean atm_startup_qualify_deployment (
    const char *flatpak_info_path,
    const char *expected_application_id,
    const char *expected_runtime_id,
    const char *expected_runtime_branch,
    guint policy_version,
    AtmDeploymentQualification **out_qualification,
    GError **error
);

void atm_deployment_qualification_free (
    AtmDeploymentQualification *qualification
);

gboolean atm_startup_qualify_storage_root (
    const char *storage_root,
    AtmStorageQualification *out_qualification,
    GError **error
);

gboolean atm_startup_qualify_storage_root_values (
    const char *storage_root,
    gboolean *out_qualified,
    gboolean *out_created,
    guint32 *out_mode,
    guint64 *out_owner_uid,
    GError **error
);

G_END_DECLS
