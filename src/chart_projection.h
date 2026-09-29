#pragma once
#include "chart_spec.h"
G_BEGIN_DECLS
#define ATM_CHART_PROJECTION_POLICY "atm-display/gistemp-linear/1"
#define ATM_CHART_PROJECTION_MAX_TICKS 17
#define ATM_CHART_PROJECTION_MIN_WIDTH 320
#define ATM_CHART_PROJECTION_MIN_HEIGHT 160
#define ATM_CHART_PROJECTION_MAX_SIZE 8192

typedef struct AtmChartProjection AtmChartProjection;
typedef struct { guint series_index, point_index; double x, y; } AtmChartProjectedPoint;
typedef struct { double position; char label[32]; } AtmChartTick;
typedef struct { double x_min, x_max, y_min, y_max; } AtmChartDisplayDomain;
/* Width/height are plot-area logical dimensions, excluding labels/margins.
 * Complete pinned GISTEMP only. Fixed linear axes, 25-year / quarter-degree
 * ticks; at most 17 ticks per axis. No arbitrary values or domains are accepted.
 * Coordinates are display approximations, never scientific source values.
 * Owns a rebuilt ChartSpec; *out must be NULL, failure preserves it. */
gboolean atm_chart_projection_new (const AtmChartSpec *spec, double width, double height,
    AtmChartProjection **out, GError **error);
gboolean atm_chart_projection_validate (const AtmChartProjection *projection, GError **error);
void atm_chart_projection_free (AtmChartProjection *projection);
/* Borrowed read-only; invalid index -> NULL. */
const AtmChartSpec *atm_chart_projection_spec (const AtmChartProjection *projection);
const AtmChartDisplayDomain *atm_chart_projection_domain (const AtmChartProjection *projection);
guint atm_chart_projection_count (const AtmChartProjection *projection);
const AtmChartProjectedPoint *atm_chart_projection_point (const AtmChartProjection *projection, guint index);
guint atm_chart_projection_x_tick_count (const AtmChartProjection *projection);
guint atm_chart_projection_y_tick_count (const AtmChartProjection *projection);
const AtmChartTick *atm_chart_projection_x_tick (const AtmChartProjection *projection, guint index);
const AtmChartTick *atm_chart_projection_y_tick (const AtmChartProjection *projection, guint index);
G_END_DECLS
