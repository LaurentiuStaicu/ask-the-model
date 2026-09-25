#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean atm_repository_capacity_ui_download_preflight (
    const char *cache_path,
    const char *repository_id,
    const char *repository_sha,
    gboolean *out_admitted,
    gboolean *out_byte_prediction_qualified,
    char **out_detail,
    GError **error
);

gboolean atm_repository_capacity_ui_mutation_preflight (
    const char *data_path,
    const char *cache_path,
    const char *state_path,
    const char *archive_path,
    const char *repository_id,
    const char *repository_sha,
    int operation_kind,
    gboolean *out_admitted,
    gboolean *out_byte_prediction_qualified,
    gboolean *out_must_admit_before_quarantine,
    char **out_detail,
    GError **error
);

gboolean atm_repository_capacity_ui_state_commit_preflight (
    const char *state_path,
    gboolean *out_admitted,
    char **out_detail,
    GError **error
);

G_END_DECLS
