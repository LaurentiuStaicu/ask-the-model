#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Read-only discovery of one canonical I4 trash candidate.
 *
 * Selection is deterministic by repository ID, then canonical trash basename.
 * Only fixed-catalog repository namespaces are accepted. Every observed
 * namespace component and candidate is inspected with no-follow semantics.
 *
 * This function is advisory. It does not acquire B0/B2, does not authorize
 * purge, and never mutates trash or repository authority.
 *
 * When no candidate exists, out_found is FALSE and returned identity strings
 * are empty allocated strings.
 */
gboolean atm_repository_gc_select_canonical_trash_candidate (
    const char *data_root,
    gboolean *out_found,
    char **out_repository_id,
    char **out_snapshot_sha,
    char **out_trash_name,
    GError **error
);

G_END_DECLS
