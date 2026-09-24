#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean atm_repository_ui_ensure_index (
    const char *state_root,
    gboolean coordinated,
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_index_path,
    char **out_repository_version,
    GError **error
);

G_END_DECLS
