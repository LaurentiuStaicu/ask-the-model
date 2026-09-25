#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct AtmRepositoryGcCandidateScan AtmRepositoryGcCandidateScan;

gboolean atm_repository_gc_candidate_scan (
    const char *data_root,
    const char *repository_id,
    AtmRepositoryGcCandidateScan **out_scan,
    GError **error
);

void atm_repository_gc_candidate_scan_free (
    AtmRepositoryGcCandidateScan *scan
);

gsize atm_repository_gc_candidate_scan_candidate_count (
    const AtmRepositoryGcCandidateScan *scan
);

const char *atm_repository_gc_candidate_scan_candidate_sha (
    const AtmRepositoryGcCandidateScan *scan,
    gsize index
);

gsize atm_repository_gc_candidate_scan_diagnostic_count (
    const AtmRepositoryGcCandidateScan *scan
);

const char *atm_repository_gc_candidate_scan_diagnostic_name (
    const AtmRepositoryGcCandidateScan *scan,
    gsize index
);

const char *atm_repository_gc_candidate_scan_diagnostic_reason (
    const AtmRepositoryGcCandidateScan *scan,
    gsize index
);

G_END_DECLS
