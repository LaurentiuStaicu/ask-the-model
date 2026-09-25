#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    guint64 directories_created;
    guint64 directory_fsync_calls;
} AtmSnapshotNamespaceStats;

/*
 * Prepares the fixed final snapshot-parent hierarchy below an already-existing
 * trusted data root:
 *
 *   Repositories/<repository_id>/snapshots
 *
 * Every component is opened without following symlinks. For each component,
 * the child directory and its containing parent are fsynced before proceeding.
 * The data root itself is never created by this helper.
 */
gboolean atm_snapshot_namespace_prepare_final_parent (
    const char *data_root,
    const char *repository_id,
    AtmSnapshotNamespaceStats *stats,
    GError **error
);

G_END_DECLS
