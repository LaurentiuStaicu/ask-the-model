#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    guint64 file_fsync_calls;
    guint64 directory_fsync_calls;
    guint64 parent_fsync_calls;
} AtmSnapshotDurabilityStats;

/*
 * Qualification boundary:
 * - sync_tree is the S1 pre-rename barrier for an already validated,
 *   operation-owned prepared tree;
 * - calling it on an existing final snapshot does not by itself constitute
 *   final-tree durability requalification;
 * - sync_parent is the separate post-rename parent-directory barrier.
 */
gboolean atm_snapshot_durability_sync_tree (
    const char *snapshot_root,
    AtmSnapshotDurabilityStats *stats,
    GError **error
);

gboolean atm_snapshot_durability_sync_parent (
    const char *snapshot_path,
    AtmSnapshotDurabilityStats *stats,
    GError **error
);

G_END_DECLS
