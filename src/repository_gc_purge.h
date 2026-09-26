#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    guint64 regular_files_removed;
    guint64 directories_removed;
    guint64 directory_fsync_calls;
} AtmRepositoryGcPurgeStats;

/*
 * Purges one already-isolated C1 trash object.
 *
 * trash_name must be the canonical basename emitted by the I4 isolation
 * primitive for the supplied fixed-catalog repository:
 * <sha40>-<positive decimal timestamp>-<positive decimal pid>-<attempt 0..99>,
 * with no leading zeroes except the single digit zero allowed for attempt.
 * The implementation
 * resolves only below Repositories/.trash/<repository_id> using dirfd-relative
 * no-follow operations. It never scans or deletes ordinary snapshot paths.
 *
 * The whole trash tree is structurally validated before the first unlink.
 * During deletion, entry types are rechecked. Symbolic links and special
 * objects are repair conditions and are never traversed.
 */
gboolean atm_repository_gc_purge_trash_entry (
    const char *data_root,
    const char *repository_id,
    const char *trash_name,
    AtmRepositoryGcPurgeStats *stats,
    GError **error
);

G_END_DECLS
