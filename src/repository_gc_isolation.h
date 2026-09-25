#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    guint64 namespace_fsync_calls;
    guint64 isolation_fsync_calls;
} AtmRepositoryGcIsolationStats;

/*
 * Narrow C1 storage primitive.
 *
 * The caller is responsible for GC policy and for holding any required
 * repository-mutation coordination before calling this function.
 *
 * This primitive validates one exact catalog repository/SHA source as a real
 * snapshot directory, creates/qualifies the AtM-owned trash namespace below
 * the same data root, atomically renames the snapshot with no replacement,
 * then fsyncs destination and source directories in the selected A1
 * destination->source order.
 *
 * It never decides reachability and never purges trash.
 */
gboolean atm_repository_gc_isolate_snapshot_to_trash (
    const char *data_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_trash_path,
    AtmRepositoryGcIsolationStats *stats,
    GError **error
);

G_END_DECLS
