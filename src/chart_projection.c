#include "chart_projection_internal.h"
#include "energy_institute_verified_series.h"
#include <errno.h>
#include <math.h>
#include <string.h>

/* This is deliberately a bounded display policy for the current adapter, not
 * a generic decimal-to-double converter or a qualified scientific transform. */
#define MAX_FIXED 100000000
#define QUARTER 2500
#define MAX_POINTS (ATM_CHART_SPEC_MAX_SERIES * ATM_ANNUAL_SERIES_MAX_POINTS)
struct AtmChartProjection {
    const AtmChartSpec *spec;
    AtmChartSpec *owned_spec;
    double width, height;
    AtmChartDisplayDomain domain;
    const char *policy_id;
    guint count, nx, ny;
    AtmChartProjectedPoint points[MAX_POINTS];
    AtmChartTick x_ticks[ATM_CHART_PROJECTION_MAX_TICKS];
    AtmChartTick y_ticks[ATM_CHART_PROJECTION_MAX_TICKS];
};
static gboolean reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}
/* Exact 1e-4 units; only invoked after complete source reconstruction. */
static gboolean fixed (const char *coefficient, gint64 exponent, gint64 *out, GError **error)
{
    if (exponent < -4 || exponent > 0)
        return reject (error, "Decimal exceeds display profile precision.");
    errno = 0;
    char *end = NULL;
    gint64 value = g_ascii_strtoll (coefficient, &end, 10);
    if (errno != 0 || end == coefficient || *end != '\0' || value < -MAX_FIXED || value > MAX_FIXED)
        return reject (error, "Decimal exceeds display profile range.");
    for (gint64 i = 0; i < exponent + 4; i++) {
        if (value < -MAX_FIXED / 10 || value > MAX_FIXED / 10)
            return reject (error, "Decimal exceeds display profile range.");
        value *= 10;
    }
    *out = value; return TRUE;
}

/* Convert only the source-qualified non-negative EI TES token representation
 * to a display-only micro-EJ/year integer. The source decimal and SRA fact are
 * never rewritten. Round-half-up is deterministic because this profile admits
 * non-negative values only. */
G_GNUC_INTERNAL gboolean
_atm_chart_projection_ei_decimal_micro (const char *coefficient,
                                        gint64 exponent,
                                        gint64 *out,
                                        GError **error)
{
    if (coefficient == NULL || out == NULL || coefficient[0] == '\0')
        return reject (error, "Invalid EI display decimal arguments.");
    if (exponent < -7 || exponent > 0)
        return reject (error, "EI display decimal precision is outside its profile.");

    gsize length = strlen (coefficient);
    if (length > 20 || (length > 1 && coefficient[0] == '0'))
        return reject (error, "EI display coefficient is not canonical.");
    guint64 value = 0;
    for (gsize i = 0; i < length; i++) {
        char ch = coefficient[i];
        if (ch < '0' || ch > '9')
            return reject (error, "EI display coefficient must contain digits only.");
        guint digit = (guint) (ch - '0');
        if (value > (G_MAXUINT64 - digit) / 10)
            return reject (error, "EI display coefficient overflows.");
        value = value * 10 + digit;
    }

    guint64 micro = 0;
    if (exponent == -7) {
        /* Check the source value before quantization so rounding cannot admit
         * an exact value just above 1000 EJ/year. */
        if (value > G_GUINT64_CONSTANT (10000000000))
            return reject (error, "EI source decimal exceeds 1000 EJ/year.");
        micro = value / 10 + ((value % 10) >= 5 ? 1u : 0u);
    } else {
        for (gint64 i = 0; i < exponent + 6; i++) {
            if (value > G_MAXUINT64 / 10)
                return reject (error, "EI display decimal overflows its integer scale.");
            value *= 10;
        }
        micro = value;
    }

    if (micro < 1 || micro > G_GUINT64_CONSTANT (1000000000))
        return reject (error, "EI display decimal is outside the qualified 0..1000 EJ/year range.");
    *out = (gint64) micro;
    return TRUE;
}
void atm_chart_projection_free (AtmChartProjection *p)
{
    if (p == NULL) return;
    atm_chart_spec_free (p->owned_spec); g_free (p);
}
static gboolean x_tick (AtmChartProjection *p, guint year, GError **error)
{
    if (p->nx >= ATM_CHART_PROJECTION_MAX_TICKS)
        return reject (error, "Too many calendar ticks.");
    AtmChartTick *t = &p->x_ticks[p->nx++];
    t->position = (year - p->domain.x_min) / (p->domain.x_max - p->domain.x_min) * p->width;
    g_snprintf (t->label, sizeof t->label, "%u", year);
    return TRUE;
}

static gboolean
projection_ei_fill (AtmChartProjection *p, GError **error)
{
    const AtmChartSpec *spec = p->spec;
    if (atm_chart_spec_version (spec) != ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2 ||
        atm_chart_spec_count (spec) != 1 ||
        (atm_chart_spec_kind (spec) != ATM_CHART_LINE &&
         atm_chart_spec_kind (spec) != ATM_CHART_SCATTER) ||
        g_strcmp0 (atm_chart_spec_schema (spec), ATM_CHART_SPEC_V2_SCHEMA) != 0 ||
        g_strcmp0 (atm_chart_spec_series_profile (spec), ATM_EI_VERIFIED_SERIES_PROFILE) != 0 ||
        g_strcmp0 (atm_chart_spec_y_unit (spec), "exajoules/year") != 0 ||
        g_strcmp0 (atm_chart_spec_y_dimension (spec), "energy_flow_rate") != 0 ||
        g_strcmp0 (atm_chart_spec_x_unit (spec), "calendar_year") != 0 ||
        g_strcmp0 (atm_chart_spec_x_dimension (spec), "time") != 0 ||
        atm_chart_spec_x_min (spec) != 1965u ||
        atm_chart_spec_x_max (spec) != 2025u ||
        atm_chart_spec_point_count (spec, 0) != 61u)
        return reject (error, "EI display projection only admits the exact 1965–2025 TES ChartSpec v2 profile.");

    const gint64 tick_step = G_GINT64_CONSTANT (100000000);
    gint64 display_values[61];
    gint64 minimum = G_MAXINT64, maximum = 0;
    for (guint i = 0; i < G_N_ELEMENTS (display_values); i++) {
        AtmChartPointView point = {0};
        if (!atm_chart_spec_point (spec, 0, i, &point) ||
            point.order != i || point.calendar_year != 1965u + i ||
            point.y_status != ATM_CHART_POINT_NUMERIC ||
            point.source_decimal == NULL || point.coefficient == NULL ||
            point.break_before || point.support == NULL ||
            point.support->len != 5u)
            return reject (error, "EI display projection rejects gaps, altered order, or incomplete evidence support.");
        if (!_atm_chart_projection_ei_decimal_micro (
                point.coefficient, point.exponent, &display_values[i], error))
            return FALSE;
        minimum = MIN (minimum, display_values[i]);
        maximum = MAX (maximum, display_values[i]);
    }
    if (maximum <= minimum || maximum > G_GINT64_CONSTANT (1000000000))
        return reject (error, "EI display projection has empty or unsupported extents.");

    gint64 low_index = minimum / tick_step;
    if (minimum % tick_step == 0) low_index--;
    gint64 high_index = maximum / tick_step + 1;
    if (low_index < 0 || high_index <= low_index ||
        high_index - low_index + 1 > ATM_CHART_PROJECTION_MAX_TICKS)
        return reject (error, "EI display projection has unsupported axis tick coverage.");

    gint64 low_micro = low_index * tick_step;
    gint64 high_micro = high_index * tick_step;
    p->domain.x_min = atm_chart_spec_x_min (spec);
    p->domain.x_max = atm_chart_spec_x_max (spec);
    p->domain.y_min = (double) low_micro / 1000000.0;
    p->domain.y_max = (double) high_micro / 1000000.0;

    guint x_low = atm_chart_spec_x_min (spec);
    guint x_high = atm_chart_spec_x_max (spec);
    guint step = p->width < 480.0 ? 15u : (p->width < 720.0 ? 10u : 5u);
    if (!x_tick (p, x_low, error))
        return FALSE;
    for (guint year = x_low + step; year < x_high; year += step)
        if (!x_tick (p, year, error))
            return FALSE;
    if (!x_tick (p, x_high, error))
        return FALSE;

    for (gint64 tick = low_index; tick <= high_index; tick++) {
        if (p->ny >= ATM_CHART_PROJECTION_MAX_TICKS)
            return reject (error, "Too many EI Y-axis ticks.");
        AtmChartTick *t = &p->y_ticks[p->ny++];
        t->position = (double) (high_index - tick) / (double) (high_index - low_index) * p->height;
        g_snprintf (t->label, sizeof t->label, "%" G_GINT64_FORMAT, tick * 100);
    }

    for (guint i = 0; i < G_N_ELEMENTS (display_values); i++) {
        AtmChartProjectedPoint *point = &p->points[p->count++];
        point->series_index = 0;
        point->point_index = i;
        point->x = (double) (1965u + i - x_low) /
                   (double) (x_high - x_low) * p->width;
        point->y = (double) (high_micro - display_values[i]) /
                   (double) (high_micro - low_micro) * p->height;
        if (!isfinite (point->x) || !isfinite (point->y) ||
            point->x < 0.0 || point->x > p->width ||
            point->y < 0.0 || point->y > p->height)
            return reject (error, "EI projected point is outside its bounded display domain.");
    }
    return TRUE;
}

static gboolean projection_new (const AtmChartSpec *spec, gboolean rebuild,
    double width, double height, AtmChartProjection **out, GError **error)
{
    if (spec == NULL || out == NULL || *out != NULL || !isfinite (width) || !isfinite (height) ||
        width < ATM_CHART_PROJECTION_MIN_WIDTH || height < ATM_CHART_PROJECTION_MIN_HEIGHT ||
        width > ATM_CHART_PROJECTION_MAX_SIZE || height > ATM_CHART_PROJECTION_MAX_SIZE)
        return reject (error, "Invalid plot dimensions, specification, or projection output.");
    AtmChartProjection *p = g_new0 (AtmChartProjection, 1);
    if (rebuild) {
        if (!atm_chart_spec_rebuild (spec, &p->owned_spec, error)) goto invalid;
        p->spec = p->owned_spec;
    } else {
        p->spec = spec;
    }
    p->width = width; p->height = height;
    if (atm_chart_spec_version (p->spec) == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2) {
        p->policy_id = ATM_CHART_PROJECTION_EI_POLICY;
        if (!projection_ei_fill (p, error)) goto invalid;
        *out = p;
        return TRUE;
    }
    if (atm_chart_spec_version (p->spec) != ATM_CHART_SPEC_VERSION_GISTEMP_V1) {
        reject (error, "Unsupported ChartSpec version for projection.");
        goto invalid;
    }
    p->policy_id = ATM_CHART_PROJECTION_POLICY;
    const AtmScientificDecimal *low = atm_chart_spec_y_min (p->spec), *high = atm_chart_spec_y_max (p->spec);
    gint64 lo, hi;
    if (!fixed (low->coefficient, low->exponent, &lo, error) ||
        !fixed (high->coefficient, high->exponent, &hi, error)) goto invalid;
    /* Signed integer floor/ceil; no rounding scientific values into ticks. */
    gint64 first = lo / QUARTER - (lo % QUARTER < 0 ? 1 : 0);
    gint64 last = hi / QUARTER + (hi % QUARTER > 0 ? 1 : 0);
    if (last <= first || last - first + 1 > ATM_CHART_PROJECTION_MAX_TICKS) {
        reject (error, "Unsupported display tick coverage."); goto invalid;
    }
    p->domain.x_min = atm_chart_spec_x_min (p->spec);
    p->domain.x_max = atm_chart_spec_x_max (p->spec);
    p->domain.y_min = first / 4.0; p->domain.y_max = last / 4.0;
    guint xlow = atm_chart_spec_x_min (p->spec), xhigh = atm_chart_spec_x_max (p->spec);
    if (!x_tick (p, xlow, error)) goto invalid;
    for (guint year = (xlow / 25 + 1) * 25; year < xhigh; year += 25)
        if (!x_tick (p, year, error)) goto invalid;
    if (!x_tick (p, xhigh, error)) goto invalid;
    for (gint64 quarter = first; quarter <= last; quarter++) {
        AtmChartTick *t = &p->y_ticks[p->ny++];
        t->position = (double) (last - quarter) / (double) (last - first) * height;
        gint64 absolute = quarter < 0 ? -quarter : quarter;
        g_snprintf (t->label, sizeof t->label, "%s%" G_GINT64_FORMAT ".%02" G_GINT64_FORMAT,
                    quarter < 0 ? "-" : "", absolute / 4, (absolute % 4) * 25);
    }
    for (guint i = 0; i < atm_chart_spec_count (p->spec); i++) {
        const AtmVerifiedSeries *s = atm_chart_spec_series (p->spec, i);
        for (guint j = 0; j < atm_verified_series_count (s); j++) {
            if (p->count >= MAX_POINTS) { reject (error, "Too many projected points."); goto invalid; }
            const AtmVerifiedPoint *source = atm_verified_series_point (s, j);
            gint64 value;
            if (!fixed (source->coefficient, source->exponent, &value, error)) goto invalid;
            AtmChartProjectedPoint *point = &p->points[p->count++];
            point->series_index = i; point->point_index = j;
            point->x = (source->calendar_year - p->domain.x_min) /
                       (p->domain.x_max - p->domain.x_min) * width;
            /* All integer differences are bounded before conversion; exact
             * scientific values remain in the owned specification. */
            point->y = (double) (last * QUARTER - value) /
                       (double) ((last - first) * QUARTER) * height;
            if (!isfinite (point->x) || !isfinite (point->y) || point->x < 0 || point->x > width ||
                point->y < 0 || point->y > height) {
                reject (error, "Projected point is outside the display domain."); goto invalid;
            }
        }
    }
    *out = p; return TRUE;
invalid:
    atm_chart_projection_free (p); return FALSE;
}
gboolean atm_chart_projection_new (const AtmChartSpec *spec, double width, double height,
    AtmChartProjection **out, GError **error)
{
    return projection_new (spec, TRUE, width, height, out, error);
}
G_GNUC_INTERNAL gboolean _atm_chart_projection_new_trusted (const AtmChartSpec *spec,
    double width, double height, AtmChartProjection **out, GError **error)
{
    return projection_new (spec, FALSE, width, height, out, error);
}
static gboolean ticks_equal (const AtmChartTick *a, const AtmChartTick *b, guint count)
{
    for (guint i = 0; i < count; i++)
        if (a[i].position != b[i].position || memcmp (a[i].label, b[i].label, sizeof a[i].label)) return FALSE;
    return TRUE;
}
gboolean atm_chart_projection_validate (const AtmChartProjection *p, GError **error)
{
    if (p == NULL || p->count > MAX_POINTS || p->nx > ATM_CHART_PROJECTION_MAX_TICKS ||
        p->ny > ATM_CHART_PROJECTION_MAX_TICKS) return reject (error, "Invalid projection coverage.");
    AtmChartProjection *expected = NULL;
    if (!atm_chart_projection_new (p->spec, p->width, p->height, &expected, error)) return FALSE;
    gboolean same = g_strcmp0 (p->policy_id, expected->policy_id) == 0 &&
        p->count == expected->count && p->nx == expected->nx && p->ny == expected->ny &&
        p->domain.x_min == expected->domain.x_min && p->domain.x_max == expected->domain.x_max &&
        p->domain.y_min == expected->domain.y_min && p->domain.y_max == expected->domain.y_max &&
        ticks_equal (p->x_ticks, expected->x_ticks, p->nx) && ticks_equal (p->y_ticks, expected->y_ticks, p->ny);
    for (guint i = 0; same && i < p->count; i++) {
        const AtmChartProjectedPoint *a = &p->points[i], *b = &expected->points[i];
        same = a->series_index == b->series_index && a->point_index == b->point_index && a->x == b->x && a->y == b->y;
    }
    atm_chart_projection_free (expected);
    return same || reject (error, "Projection disagrees with reconstructed specification.");
}
const AtmChartSpec *atm_chart_projection_spec (const AtmChartProjection *p) { return p ? p->spec : NULL; }
const char *atm_chart_projection_policy (const AtmChartProjection *p) { return p ? p->policy_id : NULL; }
const AtmChartDisplayDomain *atm_chart_projection_domain (const AtmChartProjection *p) { return p ? &p->domain : NULL; }
guint atm_chart_projection_count (const AtmChartProjection *p) { return p ? p->count : 0; }
const AtmChartProjectedPoint *atm_chart_projection_point (const AtmChartProjection *p, guint i)
{ return p && i < p->count ? &p->points[i] : NULL; }
guint atm_chart_projection_x_tick_count (const AtmChartProjection *p) { return p ? p->nx : 0; }
guint atm_chart_projection_y_tick_count (const AtmChartProjection *p) { return p ? p->ny : 0; }
const AtmChartTick *atm_chart_projection_x_tick (const AtmChartProjection *p, guint i)
{ return p && i < p->nx ? &p->x_ticks[i] : NULL; }
const AtmChartTick *atm_chart_projection_y_tick (const AtmChartProjection *p, guint i)
{ return p && i < p->ny ? &p->y_ticks[i] : NULL; }
