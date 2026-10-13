#pragma once

#include "chart_projection.h"

G_BEGIN_DECLS

/* Internal viewport-only path. The caller must own a ChartSpec that has already
 * passed the public reconstruction/validation boundary and must keep it alive
 * longer than the returned projection. Not application/public ABI. */
/* Exact-decimal EI TES token to a display-only micro-EJ/year integer.
 * Bounds and rounding are part of the qualified EI display policy; no binary
 * floating-point is used in conversion. Kept internal for focused tests. */
G_GNUC_INTERNAL gboolean _atm_chart_projection_ei_decimal_micro (
    const char *coefficient,
    gint64 exponent,
    gint64 *out,
    GError **error
);

G_GNUC_INTERNAL gboolean _atm_chart_projection_new_trusted (
    const AtmChartSpec *spec,
    double width,
    double height,
    AtmChartProjection **out,
    GError **error
);

G_END_DECLS
