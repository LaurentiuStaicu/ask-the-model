#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Selects the globally oldest canonical I4 trash object that exists at scan
 * time. The scan is read-only/advisory and never authorizes deletion.
 *
 * Only the fixed repository catalog (ewd/cbd/rmd) is accepted. Every observed
 * namespace component and trash object is inspected without following links.
 * Unexpected repository names, malformed trash names, symlinks and special
 * objects fail closed as repair conditions.
 *
 * When no .trash namespace or no trash object exists, out_found is FALSE and
 * the returned repository/name strings are empty allocations.
 */
gboolean atm_repository_gc_select_oldest_trash_candidate (
    const char *data_root,
    gboolean *out_found,
    char **out_repository_id,
    char **out_trash_name,
    gint64 *out_isolation_time,
    GError **error
);

G_END_DECLS
