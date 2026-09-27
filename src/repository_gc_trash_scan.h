#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct AtmRepositoryGcTrashScan AtmRepositoryGcTrashScan;

gboolean atm_repository_gc_trash_scan (
    const char *data_root,
    const char *repository_id,
    AtmRepositoryGcTrashScan **out_scan,
    GError **error
);

void atm_repository_gc_trash_scan_free (
    AtmRepositoryGcTrashScan *scan
);

gsize atm_repository_gc_trash_scan_candidate_count (
    const AtmRepositoryGcTrashScan *scan
);

const char *atm_repository_gc_trash_scan_candidate_name (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
);

const char *atm_repository_gc_trash_scan_candidate_sha (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
);

gsize atm_repository_gc_trash_scan_diagnostic_count (
    const AtmRepositoryGcTrashScan *scan
);

const char *atm_repository_gc_trash_scan_diagnostic_name (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
);

const char *atm_repository_gc_trash_scan_diagnostic_reason (
    const AtmRepositoryGcTrashScan *scan,
    gsize index
);

G_END_DECLS
