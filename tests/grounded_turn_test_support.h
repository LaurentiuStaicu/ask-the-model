#pragma once

#include <glib.h>

gboolean atm_grounded_turn_fixture_create (
    char **out_cache_root,
    char **out_snapshot_root,
    char **out_index_path,
    char **out_version,
    char **out_snapshot_sha,
    GError **error
);

void atm_grounded_turn_fixture_destroy (
    char *cache_root,
    char *snapshot_root,
    char *index_path,
    char *version,
    char *snapshot_sha
);
