#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CHART_M4_ALGORITHM "atm-m4-line/1"
/* Display-layer allocation ceilings aligned with the current projection envelope.
 * They are not scientific coverage claims and deliberately avoid depending on
 * VerifiedSeries/ChartSpec headers. */
#define ATM_CHART_M4_MAX_POINTS 1024u
#define ATM_CHART_M4_MAX_COLUMNS 8192u

typedef enum {
    ATM_CHART_M4_ERROR_ARGUMENT,
    ATM_CHART_M4_ERROR_LIMIT,
    ATM_CHART_M4_ERROR_SHAPE,
    ATM_CHART_M4_ERROR_ORDER
} AtmChartM4Error;

#define ATM_CHART_M4_ERROR (atm_chart_m4_error_quark ())
GQuark atm_chart_m4_error_quark (void);

/* Display geometry only. The caller supplies the raster column using the exact
 * renderer mapping it has qualified. source_index identifies an existing
 * projected observation; y is a display coordinate, never a scientific value. */
typedef struct {
    guint source_index;
    guint pixel_column;
    double y;
} AtmChartM4Point;

typedef struct AtmChartM4Plan AtmChartM4Plan;

/* Dormant LINE-only reduction primitive. Input must be source ordered, finite,
 * nondecreasing in pixel_column, and every column must be < column_count.
 * If count <= column_count, the plan is an exact full copy. Dense input retains
 * first/min-Y/max-Y/last in each occupied pixel column, deduplicated and emitted
 * in original source order. One source-ordered LINE series is planned per call.
 * is_reduced is true only when the output count actually shrinks.
 * The mapping from projected X to pixel_column is intentionally NOT defined here;
 * renderer integration must qualify that mapping against actual raster output.
 * *out must be NULL. */
gboolean atm_chart_m4_plan_new (
    const AtmChartM4Point *points,
    gsize count,
    guint column_count,
    AtmChartM4Plan **out,
    GError **error
);

void atm_chart_m4_plan_free (AtmChartM4Plan *plan);
guint atm_chart_m4_plan_count (const AtmChartM4Plan *plan);
gboolean atm_chart_m4_plan_is_reduced (const AtmChartM4Plan *plan);
const AtmChartM4Point *atm_chart_m4_plan_point (const AtmChartM4Plan *plan, guint index);

G_END_DECLS
