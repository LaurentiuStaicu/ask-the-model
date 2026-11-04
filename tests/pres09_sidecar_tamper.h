#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean atm_pres09_tamper_first_persistent_v4_sidecar (
    const gchar *database_path,
    const gchar *conversation_id,
    GError **error
);

gboolean atm_pres09_create_v5_legacy_history_fixture (
    const gchar *source_database_path,
    const gchar *target_database_path,
    const gchar *conversation_id,
    GError **error
);

gboolean atm_pres09_verify_migrated_v5_legacy_history (
    const gchar *database_path,
    const gchar *conversation_id,
    GError **error
);

G_END_DECLS
