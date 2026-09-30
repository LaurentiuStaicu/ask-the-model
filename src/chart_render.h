#pragma once
#include "chart_projection.h"
#include <cairo.h>
G_BEGIN_DECLS
#define ATM_CHART_RENDER_STYLE "atm-retro-mono/1"
#define ATM_CHART_RENDER_MAX_SIZE 2048

typedef struct AtmChartRender AtmChartRender;
/* Headless prototype only, not linked into the application. Logical canvas:
 * 240..2048 by 140..2048; integer device scale 1 or 2. Small canvases produce
 * an explicit fallback with the same complete exact Data view. Owns all data.
 * *out must be NULL. Failure leaves it unchanged. */
gboolean atm_chart_render_new (const AtmChartSpec *spec, guint width, guint height,
    guint device_scale, AtmChartRender **out, GError **error);
/* Viewport-only update over this renderer's already reconstructed/owned
 * ChartSpec. Prepares projection+surface before replacing current viewport
 * state. Failure leaves spec, Data and the previous viewport unchanged. */
gboolean atm_chart_render_resize (AtmChartRender *render, guint width, guint height,
    guint device_scale, GError **error);
void atm_chart_render_free (AtmChartRender *render);
/* Borrowed read-only; never modify or destroy the surface or strings. */
cairo_surface_t *atm_chart_render_surface (const AtmChartRender *render);
const char *atm_chart_render_data (const AtmChartRender *render);
gboolean atm_chart_render_is_fallback (const AtmChartRender *render);
const AtmChartSpec *atm_chart_render_spec (const AtmChartRender *render);
G_END_DECLS
