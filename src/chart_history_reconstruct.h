#pragma once
#include "chart_spec.h"
G_BEGIN_DECLS

/* Reconstruct a persisted qualified GISTEMP chart from the pinned repository
 * snapshot. The source bytes are re-admitted before VerifiedSeries/ChartSpec
 * construction; persisted identities are checked against the rebuilt objects.
 * No persisted numeric points or display state are trusted. */
gboolean atm_chart_history_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *chart_kind,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *scientific_id,
    const char *qualified_id,
    const char *source_path,
    AtmChartSpec **out,
    GError **error
);


/* Live reconstruction entry point; declaration needed by the C test caller
 * and the C emitted by Vala for Application.vala. */
gboolean atm_chart_live_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    AtmChartSpec **out,
    GError **error
);

G_END_DECLS
