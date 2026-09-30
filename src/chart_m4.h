#pragma once

#include "chart_projection.h"

G_BEGIN_DECLS

#define ATM_CHART_M4_ALGORITHM "atm-m4-line/1"
#define ATM_CHART_M4_MAX_POINTS (ATM_CHART_SPEC_MAX_SERIES * ATM_ANNUAL_SERIES_MAX_POINTS)
#define ATM_CHART_M4_MAX_WIDTH ATM_CHART_PROJECTION_MAX_SIZE

typedef enum {
    ATM_CHART_M4_ERROR_ARGUMENT,
    ATM_CHART_M4_ERROR_LIMIT,
    ATM_CHART_M4_ERROR_SHAPE,
    ATM_CHART_M4_ERROR_ORDER
} AtmChartM4Error;

#define ATM_CHART_M4_ERROR (atm_chart_m4_error_quark ())
GQuark atm_chart_m4_error_quark (void);

/* Display geometry only. source_index identifies an existing projected
 * observation; x/y are display coordinates, never scientific source values. */
typedef struct {
    guint source_index;
    double x;
    double y;
} AtmChartM4Point;

typedef struct AtmChartM4Plan AtmChartM4Plan;

/* Dormant LINE-only display reduction primitive. Input must be source ordered,
 * finite, nondecreasing in projected X and bounded to the current projection
 * width. If count <= plot_width, the plan is an exact full copy. Dense input
 * retains first/min-Y/max-Y/last in each projected X-pixel column, deduplicated
 * and emitted in original source order. *out must be NULL. */
gboolean atm_chart_m4_plan_new (
    const AtmChartM4Point *points,
    gsize count,
    guint plot_width,
    AtmChartM4Plan **out,
    GError **error
);

void atm_chart_m4_plan_free (AtmChartM4Plan *plan);
guint atm_chart_m4_plan_count (const AtmChartM4Plan *plan);
gboolean atm_chart_m4_plan_is_reduced (const AtmChartM4Plan *plan);
const AtmChartM4Point *atm_chart_m4_plan_point (const AtmChartM4Plan *plan, guint index);

G_END_DECLS
