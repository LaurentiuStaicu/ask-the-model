#include "chart_m4.h"

#include <math.h>
#include <string.h>

struct AtmChartM4Plan {
    guint count;
    gboolean reduced;
    AtmChartM4Point *points;
};

GQuark
atm_chart_m4_error_quark (void)
{
    return g_quark_from_static_string ("atm-chart-m4-error-quark");
}

static gboolean
reject (GError **error, AtmChartM4Error code, const char *message)
{
    g_set_error_literal (error, ATM_CHART_M4_ERROR, code, message);
    return FALSE;
}

static guint
pixel_column (double x, guint width)
{
    if (x >= (double) width)
        return width - 1;
    return (guint) x;
}

static void
append_index (AtmChartM4Plan *plan, const AtmChartM4Point *input, guint index)
{
    if (plan->count > 0 &&
        plan->points[plan->count - 1].source_index == input[index].source_index)
        return;
    plan->points[plan->count++] = input[index];
}

static void
sort_four (guint *values, guint count)
{
    for (guint i = 1; i < count; i++) {
        guint value = values[i];
        guint j = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = value;
    }
}

gboolean
atm_chart_m4_plan_new (
    const AtmChartM4Point *points,
    gsize count,
    guint plot_width,
    AtmChartM4Plan **out,
    GError **error)
{
    if (out == NULL || *out != NULL || points == NULL || count == 0)
        return reject (error, ATM_CHART_M4_ERROR_ARGUMENT, "Invalid M4 arguments or output.");
    if (count > ATM_CHART_M4_MAX_POINTS || plot_width == 0 || plot_width > ATM_CHART_M4_MAX_WIDTH)
        return reject (error, ATM_CHART_M4_ERROR_LIMIT, "M4 input exceeds display limits.");

    for (gsize i = 0; i < count; i++) {
        if (!isfinite (points[i].x) || !isfinite (points[i].y) ||
            points[i].x < 0.0 || points[i].x > (double) plot_width)
            return reject (error, ATM_CHART_M4_ERROR_SHAPE, "M4 point is outside finite projected geometry.");
        if (i > 0 && points[i].source_index <= points[i - 1].source_index)
            return reject (error, ATM_CHART_M4_ERROR_ORDER, "M4 source order is not strictly increasing.");
        if (i > 0 && points[i].x < points[i - 1].x)
            return reject (error, ATM_CHART_M4_ERROR_ORDER, "M4 projected X is not nondecreasing.");
    }

    AtmChartM4Plan *plan = g_new0 (AtmChartM4Plan, 1);
    plan->points = g_new (AtmChartM4Point, count);

    if (count <= plot_width) {
        memcpy (plan->points, points, sizeof *points * count);
        plan->count = (guint) count;
        plan->reduced = FALSE;
        *out = plan;
        return TRUE;
    }

    guint start = 0;
    while (start < count) {
        guint bucket = pixel_column (points[start].x, plot_width);
        guint end = start + 1;
        while (end < count && pixel_column (points[end].x, plot_width) == bucket)
            end++;

        guint first = start, last = end - 1, min_y = start, max_y = start;
        for (guint i = start + 1; i < end; i++) {
            if (points[i].y < points[min_y].y)
                min_y = i;
            if (points[i].y > points[max_y].y)
                max_y = i;
        }

        guint selected[4] = { first, min_y, max_y, last };
        sort_four (selected, G_N_ELEMENTS (selected));
        for (guint i = 0; i < G_N_ELEMENTS (selected); i++) {
            if (i == 0 || selected[i] != selected[i - 1])
                append_index (plan, points, selected[i]);
        }
        start = end;
    }

    plan->reduced = plan->count < count;
    *out = plan;
    return TRUE;
}

void
atm_chart_m4_plan_free (AtmChartM4Plan *plan)
{
    if (plan == NULL)
        return;
    g_free (plan->points);
    g_free (plan);
}

guint
atm_chart_m4_plan_count (const AtmChartM4Plan *plan)
{
    return plan ? plan->count : 0;
}

gboolean
atm_chart_m4_plan_is_reduced (const AtmChartM4Plan *plan)
{
    return plan ? plan->reduced : FALSE;
}

const AtmChartM4Point *
atm_chart_m4_plan_point (const AtmChartM4Plan *plan, guint index)
{
    return plan && index < plan->count ? &plan->points[index] : NULL;
}
