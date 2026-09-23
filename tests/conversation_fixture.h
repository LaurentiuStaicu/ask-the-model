#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean atm_test_conversation_fixture_create (
    const char *repository_id,
    const char *acronym,
    const char *display_name,
    const char *repository_version,
    const char *snapshot_sha,
    char **out_snapshot_root,
    char **out_cache_root,
    char **out_index_path,
    char **out_repository_version,
    GError **error
);

void atm_test_conversation_fixture_remove (
    const char *snapshot_root,
    const char *cache_root
);

G_END_DECLS
