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

/* Live path: rebuild the current chart from the pinned snapshot without a
 * persisted recipe. The caller supplies only the admitted identity, so there
 * is nothing to compare against; the admission policy, the series build and
 * the spec build are the same as the History path above.
 *
 * Declared here since the first caller is tests/chart_history_reconstruct_test.c.
 * Without this declaration the definition in chart_history_reconstruct.c has no
 * visible prototype in its own translation unit, and callers are compiled
 * against an implicit declaration: the compiler then cannot check the argument
 * count or types, so a signature change would compile and fail at runtime. */
gboolean atm_chart_live_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    AtmChartSpec **out,
    GError **error
);

G_END_DECLS
