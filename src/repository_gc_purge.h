#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    guint64 regular_files_removed;
    guint64 directories_removed;
    guint64 directory_fsync_calls;

    /*
     * Populated only after a fully successful purge.
     *
     * logical_regular_bytes_removed is the sum of pre-purge st_size for
     * regular files in the exact validated trash tree.
     *
     * allocated_tree_bytes_removed is the sum of Linux st_blocks * 512 for
     * the validated root directory and every regular-file/directory entry.
     * It describes allocation attributed to the removed tree; it is not a
     * promise that filesystem free-space counters increase by exactly this
     * amount (for example with shared/compressed storage).
     */
    guint64 logical_regular_bytes_removed;
    guint64 allocated_tree_bytes_removed;
} AtmRepositoryGcPurgeStats;

/*
 * Parses the exact canonical I4 trash basename grammar:
 * <sha40>-<positive timestamp>-<positive pid>-<attempt 0..99>.
 *
 * Optional numeric outputs are populated only for a canonical identity.
 * Read-only discovery and destructive I5 revalidation share this parser so
 * candidate selection cannot drift to a weaker identity grammar.
 */
gboolean atm_repository_gc_trash_name_parse (
    const char *trash_name,
    gint64 *out_isolation_time,
    gint *out_pid,
    guint *out_attempt
);

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
