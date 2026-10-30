#pragma once

#include "chart_render.h"
#include "chart_projection_v4.h"

G_BEGIN_DECLS

#define ATM_CHART_RENDER_V4_SCHEMA "atm-chart-render/4"
#define ATM_CHART_RENDER_V4_ID_DOMAIN "atm.chart-render.v4"

typedef struct AtmChartRenderV4 AtmChartRenderV4;

typedef enum {
    ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
    ATM_CHART_RENDER_V4_ERROR_IDENTITY,
    ATM_CHART_RENDER_V4_ERROR_PROJECTION,
    ATM_CHART_RENDER_V4_ERROR_DATA
} AtmChartRenderV4Error;

#define ATM_CHART_RENDER_V4_ERROR (atm_chart_render_v4_error_quark ())
GQuark atm_chart_render_v4_error_quark (void);

/*
 * Catalog-bound ChartSpec v4 renderer. The Cairo surface is painted directly
 * from the validated v4 projection using the shared monochrome layout policy.
 * The exact Data TSV is produced separately from the independently rebuilt
 * source ChartSpec; no legacy Cairo surface is created by this path.
 *
 * Small-canvas/layout fallback preserves the complete Data view but claims no
 * active projection. Output must be NULL on entry; failure leaves it unchanged.
 * This is not a History persistence schema or arbitrary-source eligibility API.
 */
gboolean atm_chart_render_v4_new (
    const AtmChartSpecV4 *spec,
    guint width,
    guint height,
    guint device_scale,
    AtmChartRenderV4 **out,
    GError **error
);

gboolean atm_chart_render_v4_validate (
    const AtmChartRenderV4 *render,
    GError **error
);

void atm_chart_render_v4_free (AtmChartRenderV4 *render);

/* Borrowed read-only objects. */
const AtmChartSpecV4 *atm_chart_render_v4_spec (
    const AtmChartRenderV4 *render
);
const AtmChartProjectionV4 *atm_chart_render_v4_projection (
    const AtmChartRenderV4 *render
);
cairo_surface_t *atm_chart_render_v4_surface (
    const AtmChartRenderV4 *render
);
const char *atm_chart_render_v4_data (
    const AtmChartRenderV4 *render
);
const char *atm_chart_render_v4_schema (
    const AtmChartRenderV4 *render
);
const char *atm_chart_render_v4_id (
    const AtmChartRenderV4 *render
);
gboolean atm_chart_render_v4_is_fallback (
    const AtmChartRenderV4 *render
);

G_END_DECLS
