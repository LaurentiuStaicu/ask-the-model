#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean atm_pres09_tamper_first_persistent_v4_sidecar (
    const gchar *database_path,
    const gchar *conversation_id,
    GError **error
);

G_END_DECLS
