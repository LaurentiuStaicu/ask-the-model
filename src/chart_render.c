#include "chart_render.h"
#include "chart_projection_internal.h"
#include <string.h>

struct AtmChartRender {
    AtmChartSpec *spec;
    AtmChartProjection *projection;
    cairo_surface_t *surface;
    char *data;
    gboolean fallback;
};
static gboolean reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}
void atm_chart_render_free (AtmChartRender *r)
{
    if (r == NULL) return;
    atm_chart_projection_free (r->projection);
    atm_chart_spec_free (r->spec);
    if (r->surface != NULL) cairo_surface_destroy (r->surface);
    g_free (r->data); g_free (r);
}

static char *ei_data_view (const AtmChartSpec *spec)
{
    const char *snapshot = atm_chart_spec_series_snapshot_sha (spec, 0);
    const char *path = atm_chart_spec_series_source_path (spec, 0);
    const char *qualified_id = atm_chart_spec_series_qualified_id (spec, 0);
    if (snapshot == NULL || path == NULL || qualified_id == NULL)
        return NULL;

    GString *s = g_string_new ("# Energy Institute global total energy supply (TES; Physical Energy Content)\n");
    g_string_append_printf (s,
        "# unit\t%s\n# semantics\tglobal_total_energy_supply / total_energy_supply\n"
        "# chart_spec_id\t%s\n# source_repository\tLaurentiuStaicu/empirical-world3-dynamics\n"
        "# source_snapshot\t%s\n# repository_version\tsnapshot:%s\n"
        "# source_path\t%s\n# series_profile\t%s\n"
        "series\tyear\tsource_decimal\tcoefficient\texponent\tsource_row\tqualified_series_id\tsupport_ids\n",
        atm_chart_spec_y_unit (spec), atm_chart_spec_id (spec),
        snapshot, snapshot, path, atm_chart_spec_series_profile (spec));

    for (guint i = 0; i < atm_chart_spec_count (spec); i++) {
        const char *series_id = atm_chart_spec_series_qualified_id (spec, i);
        for (guint j = 0; j < atm_chart_spec_point_count (spec, i); j++) {
            AtmChartPointView point = {0};
            if (!atm_chart_spec_point (spec, i, j, &point) ||
                point.y_status != ATM_CHART_POINT_NUMERIC ||
                point.source_decimal == NULL || point.coefficient == NULL ||
                point.support == NULL || point.support->len != 5u) {
                g_string_free (s, TRUE);
                return NULL;
            }
            g_string_append_printf (s, "%u\t%u\t%s\t%s\t%" G_GINT64_FORMAT "\t%u\t%s\t",
                i, point.calendar_year, point.source_decimal, point.coefficient,
                point.exponent, point.source_row, series_id);
            for (guint k = 0; k < point.support->len; k++) {
                if (k) g_string_append_c (s, ';');
                g_string_append (s, g_ptr_array_index (point.support, k));
            }
            g_string_append_c (s, '\n');
        }
    }
    return g_string_free (s, FALSE);
}


static char *bnr_data_view (const AtmChartSpec *spec)
{
    const char *snapshot = atm_chart_spec_series_snapshot_sha (spec, 0);
    const char *path = atm_chart_spec_series_source_path (spec, 0);
    const char *qualified_id = atm_chart_spec_series_qualified_id (spec, 0);
    if (snapshot == NULL || path == NULL || qualified_id == NULL)
        return NULL;

    GString *s = g_string_new ("# BNR BLS C01 credit standards net percentage\n");
    g_string_append_printf (s,
        "# unit\tpercent\n# semantics\tnfc_credit_standards / survey_net_percentage\n"
        "# chart_spec_id\t%s\n# source_repository\t%s\n"
        "# source_snapshot\t%s\n# repository_version\tsnapshot:%s\n"
        "# source_path\t%s\n# series_profile\t%s\n"
        "# missing_policy\t2025-Q2=source_round_not_retained\n"
        "series\tquarter\tstatus\tsource_decimal\tcoefficient\texponent\tsource_row\tqualified_series_id\tsupport_ids\tmissing_reason\n",
        atm_chart_spec_id (spec), ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        snapshot, snapshot, path, atm_chart_spec_series_profile (spec));

    for (guint i = 0; i < atm_chart_spec_point_count (spec, 0); i++) {
        AtmChartPointView point = {0};
        if (!atm_chart_spec_point (spec, 0, i, &point) ||
            point.calendar_quarter == NULL || point.support == NULL ||
            point.support->len == 0u || qualified_id == NULL) {
            g_string_free (s, TRUE);
            return NULL;
        }

        const char *status = point.y_status == ATM_CHART_POINT_NUMERIC
            ? "NUMERIC" : "MISSING";
        if (point.y_status == ATM_CHART_POINT_MISSING) {
            if (i != 10u || point.source_row != 0u ||
                point.source_decimal != NULL || point.coefficient != NULL ||
                point.exponent != 0 ||
                g_strcmp0 (point.missing_reason, ATM_BNR_BLS_MISSING_REASON) != 0 ||
                point.support->len != ATM_BNR_BLS_SOURCE_COUNT) {
                g_string_free (s, TRUE);
                return NULL;
            }
        } else if (point.y_status != ATM_CHART_POINT_NUMERIC ||
                   point.source_row == 0u || point.source_decimal == NULL ||
                   point.coefficient == NULL || point.missing_reason != NULL) {
            g_string_free (s, TRUE);
            return NULL;
        }

        char *source_row_text = point.y_status == ATM_CHART_POINT_MISSING
            ? g_strdup ("none")
            : g_strdup_printf ("%u", point.source_row);
        g_string_append_printf (s, "0\t%s\t%s\t%s\t%s\t%" G_GINT64_FORMAT "\t%s\t%s\t",
            point.calendar_quarter, status,
            point.source_decimal != NULL ? point.source_decimal : "",
            point.coefficient != NULL ? point.coefficient : "",
            point.exponent, source_row_text, qualified_id);
        g_free (source_row_text);
        for (guint k = 0; k < point.support->len; k++) {
            if (k) g_string_append_c (s, ';');
            g_string_append (s, g_ptr_array_index (point.support, k));
        }
        g_string_append_printf (s, "\t%s\n",
            point.missing_reason != NULL ? point.missing_reason : "");
    }
    return g_string_free (s, FALSE);
}

/* Closed, source-qualified ASCII metadata and exact source spelling. No model
 * strings enter the prototype. The future widget must use plain text cells. */
static char *data_view (const AtmChartSpec *spec)
{
    if (atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return ei_data_view (spec);
    if (atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return bnr_data_view (spec);
    const char *snapshot = atm_chart_spec_series_snapshot_sha (spec, 0);
    const char *source_path = atm_chart_spec_series_source_path (spec, 0);
    if (snapshot == NULL || source_path == NULL)
        return NULL;

    GString *s = g_string_new ("# GISTEMP global annual temperature anomaly\n");
    g_string_append_printf (s, "# unit\t%s\n# reference_period\t%s\n# chart_spec_id\t%s\n",
        atm_verified_series_unit (atm_chart_spec_series (spec, 0)),
        atm_verified_series_reference_period (atm_chart_spec_series (spec, 0)), atm_chart_spec_id (spec));
    g_string_append_printf (s,
        "# source_repository\tLaurentiuStaicu/empirical-world3-dynamics\n"
        "# source_snapshot\t%s\n"
        "# source_path\t%s\n"
        "# access_vintage\t2026-08-31\n"
        "series\tyear\tsource_decimal\tcoefficient\texponent\tsource_row\tqualified_series_id\tsupport_ids\n",
        snapshot, source_path);
    for (guint i = 0; i < atm_chart_spec_count (spec); i++) {
        const AtmVerifiedSeries *series = atm_chart_spec_series (spec, i);
        for (guint j = 0; j < atm_verified_series_count (series); j++) {
            const AtmVerifiedPoint *p = atm_verified_series_point (series, j);
            g_string_append_printf (s, "%u\t%u\t%s\t%s\t%" G_GINT64_FORMAT "\t%u\t%s\t",
                i, p->calendar_year, p->source_decimal, p->coefficient, p->exponent,
                p->source_row, atm_verified_series_qualified_id (series));
            for (guint k = 0; k < p->support->len; k++) {
                if (k) g_string_append_c (s, ';');
                g_string_append (s, g_ptr_array_index (p->support, k));
            }
            g_string_append_c (s, '\n');
        }
    }
    return g_string_free (s, FALSE);
}
static void font (cairo_t *cr, double size, gboolean bold)
{
    cairo_select_font_face (cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size (cr, size);
}
static void label (cairo_t *cr, const char *value, double x, double y, double alignment)
{
    cairo_text_extents_t e;
    cairo_text_extents (cr, value, &e);
    cairo_move_to (cr, x - e.x_bearing - alignment * e.width, y);
    cairo_show_text (cr, value);
}
static gboolean fits (cairo_t *cr, const char *value, double width)
{
    cairo_text_extents_t e; cairo_text_extents (cr, value, &e);
    return e.width <= width;
}
static gboolean layout_fits (cairo_t *cr, const AtmChartProjection *p, guint width)
{
    double previous_right = -1;
    font (cr, 12, FALSE);
    for (guint i = 0; i < atm_chart_projection_x_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_x_tick (p, i);
        cairo_text_extents_t e; cairo_text_extents (cr, t->label, &e);
        double center = 84 + t->position, left = center - e.width / 2, right = center + e.width / 2;
        if (left < 12 || right > width - 12 || left < previous_right + 8) return FALSE;
        previous_right = right;
    }
    double previous_top = 1e9;
    for (guint i = 0; i < atm_chart_projection_y_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_y_tick (p, i);
        cairo_text_extents_t e; cairo_text_extents (cr, t->label, &e);
        double baseline = 108 + t->position + 4, top = baseline + e.y_bearing;
        if (e.width > 60 || baseline + e.y_bearing + e.height + 4 > previous_top) return FALSE;
        previous_top = top;
    }
    return fits (cr, "Anomaly relative to 1951-1980 (deg C)", width - 32) &&
        fits (cr, "Source: NASA GISTEMP | vintage 2026-08-31", width - 32);
}

static void chart_energy_institute (cairo_t *cr, const AtmChartSpec *spec,
    const AtmChartProjection *p, guint width, guint height)
{
    double left = 84, top = 108, pw = width - 112, ph = height - 188;
    font (cr, 14, TRUE);
    label (cr, "ENERGY INSTITUTE / GLOBAL TOTAL ENERGY SUPPLY", 16, 26, 0);
    font (cr, 12, FALSE);
    label (cr, "TES, Physical Energy Content (exajoules/year)", 16, 47, 0);
    const char *kind = atm_chart_spec_kind (spec) == ATM_CHART_LINE ? "LINE" : "SCATTER";
    char *summary = g_strdup_printf ("1965-2025 | 61 observations | empirical | %s", kind);
    label (cr, summary, 16, 68, 0); g_free (summary);
    label (cr, "EJ/year", left, 96, 0);
    double dots[] = {1, 5};
    for (guint i = 0; i < atm_chart_projection_y_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_y_tick (p, i);
        double y = top + t->position;
        cairo_set_dash (cr, dots, 2, 0); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, left, y); cairo_line_to (cr, left + pw, y); cairo_stroke (cr);
        cairo_set_dash (cr, NULL, 0, 0);
        label (cr, t->label, left - 12, y + 4, 1);
    }
    cairo_rectangle (cr, left, top, pw, ph); cairo_stroke (cr);
    for (guint i = 0; i < atm_chart_projection_x_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_x_tick (p, i);
        double x = left + t->position;
        cairo_move_to (cr, x, top + ph); cairo_line_to (cr, x, top + ph + 5); cairo_stroke (cr);
        label (cr, t->label, x, top + ph + 22, 0.5);
    }
    cairo_save (cr);
    cairo_rectangle (cr, left - 3, top - 3, pw + 6, ph + 6); cairo_clip (cr);
    cairo_set_line_width (cr, 1.5);
    for (guint i = 0; i < atm_chart_projection_count (p); i++) {
        const AtmChartProjectedPoint *point = atm_chart_projection_point (p, i);
        double x = left + point->x, y = top + point->y;
        if (atm_chart_spec_kind (spec) == ATM_CHART_SCATTER) {
            cairo_rectangle (cr, x - 1.5, y - 1.5, 3, 3); cairo_fill (cr);
        } else if (i == 0 || point->point_index == 0) {
            if (i > 0) cairo_stroke (cr);
            cairo_move_to (cr, x, y);
        } else {
            cairo_line_to (cr, x, y);
        }
    }
    if (atm_chart_spec_kind (spec) == ATM_CHART_LINE) cairo_stroke (cr);
    cairo_restore (cr);
    font (cr, 12, FALSE);
    label (cr, "Source: Energy Institute | Physical Energy Content", 16, height - 36, 0);
    label (cr, "Exact values and source details: Data view", 16, height - 16, 0);
}


static gboolean layout_fits_bnr (cairo_t *cr, const AtmChartProjection *p, guint width)
{
    double previous_right = -1.0;
    font (cr, 12, FALSE);
    for (guint i = 0; i < atm_chart_projection_x_tick_count (p); i++) {
        const AtmChartTick *tick = atm_chart_projection_x_tick (p, i);
        cairo_text_extents_t extent;
        cairo_text_extents (cr, tick->label, &extent);
        double center = 84.0 + tick->position;
        double left = center - extent.width / 2.0;
        double right = center + extent.width / 2.0;
        if (left < 2.0 || right > width - 2.0 ||
            (previous_right >= 0.0 && left < previous_right + 6.0))
            return FALSE;
        previous_right = right;
    }
    double previous_bottom = -1e9;
    for (guint i = 0; i < atm_chart_projection_y_tick_count (p); i++) {
        const AtmChartTick *tick = atm_chart_projection_y_tick (p, i);
        cairo_text_extents_t extent;
        cairo_text_extents (cr, tick->label, &extent);
        double baseline = 108.0 + tick->position + 4.0;
        double top = baseline + extent.y_bearing;
        double bottom = top + extent.height;
        if (extent.width > 36.0 || top < previous_bottom + 4.0)
            return FALSE;
        previous_bottom = bottom;
    }
    return fits (cr, "Net percentage, non-financial corporations", width - 32) &&
        fits (cr, "Source: BNR BLS C01", width - 32) &&
        fits (cr, "2025-Q2 missing, not zero | Data view", width - 32);
}

static void chart_bnr (cairo_t *cr, const AtmChartSpec *spec,
    const AtmChartProjection *projection, guint width, guint height)
{
    double left = 84.0, top = 108.0, plot_width = width - 112.0,
           plot_height = height - 188.0;
    font (cr, 14, TRUE);
    label (cr, "BNR BLS C01 / CREDIT STANDARDS", 16, 26, 0);
    font (cr, 12, FALSE);
    label (cr, "Net percentage, non-financial corporations", 16, 47, 0);
    const char *kind = atm_chart_spec_kind (spec) == ATM_CHART_LINE ? "LINE" : "SCATTER";
    label (cr, "2022-Q4..2025-Q3 | 11 numeric + 1 MISSING | empirical", 16, 68, 0);
    label (cr, kind, width - 16, 68, 1);
    label (cr, "Net %", left, 96, 0);

    double dash[] = {1.0, 5.0};
    for (guint i = 0; i < atm_chart_projection_y_tick_count (projection); i++) {
        const AtmChartTick *tick = atm_chart_projection_y_tick (projection, i);
        double y = top + tick->position;
        cairo_set_dash (cr, dash, 2, 0);
        cairo_set_line_width (cr, 1.0);
        cairo_move_to (cr, left, y);
        cairo_line_to (cr, left + plot_width, y);
        cairo_stroke (cr);
        cairo_set_dash (cr, NULL, 0, 0);
        label (cr, tick->label, left - 12, y + 4, 1);
    }
    cairo_rectangle (cr, left, top, plot_width, plot_height);
    cairo_stroke (cr);
    for (guint i = 0; i < atm_chart_projection_x_tick_count (projection); i++) {
        const AtmChartTick *tick = atm_chart_projection_x_tick (projection, i);
        double x = left + tick->position;
        cairo_move_to (cr, x, top + plot_height);
        cairo_line_to (cr, x, top + plot_height + 5);
        cairo_stroke (cr);
        label (cr, tick->label, x, top + plot_height + 22, 0.5);
    }

    cairo_save (cr);
    cairo_rectangle (cr, left - 3, top - 3, plot_width + 6, plot_height + 6);
    cairo_clip (cr);
    cairo_set_line_width (cr, 1.5);
    guint previous_index = G_MAXUINT;
    gboolean path_started = FALSE;
    for (guint i = 0; i < atm_chart_projection_count (projection); i++) {
        const AtmChartProjectedPoint *point =
            atm_chart_projection_point (projection, i);
        double x = left + point->x, y = top + point->y;
        if (atm_chart_spec_kind (spec) == ATM_CHART_SCATTER) {
            cairo_rectangle (cr, x - 1.5, y - 1.5, 3.0, 3.0);
            cairo_fill (cr);
        } else if (!path_started) {
            cairo_move_to (cr, x, y);
            path_started = TRUE;
        } else if (point->point_index != previous_index + 1u) {
            /* Stroke the prior path and start a separate path: never bridge MISSING. */
            cairo_stroke (cr);
            cairo_move_to (cr, x, y);
        } else {
            cairo_line_to (cr, x, y);
        }
        previous_index = point->point_index;
    }
    if (atm_chart_spec_kind (spec) == ATM_CHART_LINE && path_started)
        cairo_stroke (cr);
    cairo_restore (cr);

    font (cr, 12, FALSE);
    label (cr, "Source: BNR BLS C01", 16, height - 36, 0);
    label (cr, "2025-Q2 missing, not zero | Data view", 16, height - 16, 0);
}

static gboolean layout_fits_ei (cairo_t *cr, const AtmChartProjection *p, guint width)
{
    double previous_right = -1;
    font (cr, 12, FALSE);
    for (guint i = 0; i < atm_chart_projection_x_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_x_tick (p, i);
        cairo_text_extents_t e; cairo_text_extents (cr, t->label, &e);
        double center = 84 + t->position, left = center - e.width / 2, right = center + e.width / 2;
        if (left < 12 || right > width - 12 || left < previous_right + 8) return FALSE;
        previous_right = right;
    }
    double previous_top = 1e9;
    for (guint i = 0; i < atm_chart_projection_y_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_y_tick (p, i);
        cairo_text_extents_t e; cairo_text_extents (cr, t->label, &e);
        double baseline = 108 + t->position + 4;
        if (e.width > 60 || baseline + e.y_bearing + e.height + 4 > previous_top) return FALSE;
        previous_top = baseline + e.y_bearing;
    }
    return fits (cr, "TES, Physical Energy Content (exajoules/year)", width - 32) &&
        fits (cr, "Source: Energy Institute | Physical Energy Content", width - 32);
}

static void chart (cairo_t *cr, const AtmChartSpec *spec, const AtmChartProjection *p,
    guint width, guint height)
{
    if (atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3) {
        chart_bnr (cr, spec, p, width, height);
        return;
    }
    if (atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2) {
        chart_energy_institute (cr, spec, p, width, height);
        return;
    }
    double left = 84, top = 108, pw = width - 112, ph = height - 188;
    font (cr, 14, TRUE);
    label (cr, "GISTEMP / GLOBAL ANNUAL TEMPERATURE", 16, 26, 0);
    font (cr, 12, FALSE);
    label (cr, "Anomaly relative to 1951-1980 (deg C)", 16, 47, 0);
    const char *kind = atm_chart_spec_kind (spec) == ATM_CHART_LINE ? "LINE" : "SCATTER";
    char *summary = g_strdup_printf ("1880-2025 | 146 observations | empirical | %s", kind);
    label (cr, summary, 16, 68, 0); g_free (summary);
    label (cr, "deg C", left, 96, 0);
    double dots[] = {1, 5};
    for (guint i = 0; i < atm_chart_projection_y_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_y_tick (p, i);
        double y = top + t->position;
        cairo_set_dash (cr, dots, 2, 0); cairo_set_line_width (cr, 1);
        cairo_move_to (cr, left, y); cairo_line_to (cr, left + pw, y); cairo_stroke (cr);
        cairo_set_dash (cr, NULL, 0, 0);
        label (cr, t->label, left - 12, y + 4, 1);
    }
    cairo_rectangle (cr, left, top, pw, ph); cairo_stroke (cr);
    for (guint i = 0; i < atm_chart_projection_x_tick_count (p); i++) {
        const AtmChartTick *t = atm_chart_projection_x_tick (p, i);
        double x = left + t->position;
        cairo_move_to (cr, x, top + ph); cairo_line_to (cr, x, top + ph + 5); cairo_stroke (cr);
        label (cr, t->label, x, top + ph + 22, 0.5);
    }
    cairo_save (cr);
    cairo_rectangle (cr, left - 3, top - 3, pw + 6, ph + 6); cairo_clip (cr);
    cairo_set_line_width (cr, 1.5);
    for (guint i = 0; i < atm_chart_projection_count (p); i++) {
        const AtmChartProjectedPoint *point = atm_chart_projection_point (p, i);
        double x = left + point->x, y = top + point->y;
        if (atm_chart_spec_kind (spec) == ATM_CHART_SCATTER) {
            cairo_rectangle (cr, x - 1.5, y - 1.5, 3, 3); cairo_fill (cr);
        } else if (i == 0 || point->point_index == 0) {
            if (i > 0) cairo_stroke (cr);
            cairo_move_to (cr, x, y);
        } else cairo_line_to (cr, x, y);
    }
    if (atm_chart_spec_kind (spec) == ATM_CHART_LINE) cairo_stroke (cr);
    cairo_restore (cr);
    font (cr, 12, FALSE);
    label (cr, "Source: NASA GISTEMP | vintage 2026-08-31", 16, height - 36, 0);
    label (cr, "Exact values and source details: Data view", 16, height - 16, 0);
}
static gboolean prepare_viewport (const AtmChartSpec *spec, guint width, guint height, guint scale,
    AtmChartProjection **projection_out, cairo_surface_t **surface_out,
    gboolean *fallback_out, GError **error)
{
    if (spec == NULL || projection_out == NULL || *projection_out != NULL ||
        surface_out == NULL || *surface_out != NULL || fallback_out == NULL ||
        width < 240 || height < 140 || width > ATM_CHART_RENDER_MAX_SIZE ||
        height > ATM_CHART_RENDER_MAX_SIZE || scale < 1 || scale > 2)
        return reject (error, "Invalid chart viewport arguments.");

    cairo_surface_t *surface = cairo_image_surface_create (
        CAIRO_FORMAT_ARGB32, width * scale, height * scale);
    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy (surface);
        return reject (error, "Cannot allocate chart surface.");
    }
    cairo_surface_set_device_scale (surface, scale, scale);
    cairo_t *cr = cairo_create (surface);
    cairo_set_source_rgb (cr, 1, 1, 1); cairo_paint (cr);
    cairo_set_source_rgb (cr, 0, 0, 0); cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_NONE);
    cairo_set_font_options (cr, options); cairo_font_options_destroy (options);

    gboolean is_ei = atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2;
    gboolean is_bnr = atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3;
    gboolean fallback = width < 432 || height < 348;
    AtmChartProjection *projection = NULL;
    if (!fallback) {
        if (!_atm_chart_projection_new_trusted (
                spec, width - 112, height - 188, &projection, error))
            goto invalid;
        font (cr, 14, TRUE);
        if (is_bnr) {
            fallback = !fits (cr, "BNR BLS C01 / CREDIT STANDARDS", width - 32);
            font (cr, 12, FALSE);
            fallback = fallback ||
                !fits (cr, "2022-Q4..2025-Q3 | 11 numeric + 1 MISSING | empirical", width - 32) ||
                !layout_fits_bnr (cr, projection, width);
        } else if (is_ei) {
            fallback = !fits (cr, "ENERGY INSTITUTE / GLOBAL TOTAL ENERGY SUPPLY", width - 32);
            font (cr, 12, FALSE);
            const char *kind = atm_chart_spec_kind (spec) == ATM_CHART_LINE ? "LINE" : "SCATTER";
            char *summary = g_strdup_printf ("1965-2025 | 61 observations | empirical | %s", kind);
            fallback = fallback || !fits (cr, summary, width - 32) ||
                !layout_fits_ei (cr, projection, width);
            g_free (summary);
        } else {
            fallback = !fits (cr, "GISTEMP / GLOBAL ANNUAL TEMPERATURE", width - 32);
            font (cr, 12, FALSE);
            fallback = fallback ||
                !fits (cr, "1880-2025 | 146 observations | empirical | SCATTER", width - 32) ||
                !layout_fits (cr, projection, width);
        }
    }
    if (fallback) {
        font (cr, 14, TRUE);
        label (cr, is_bnr ? "BNR BLS C01" : (is_ei ? "Energy Institute TES" : "GISTEMP"), 16, 30, 0);
        font (cr, 12, FALSE); label (cr, "Chart needs more space.", 16, 60, 0);
        label (cr, "Exact values remain", 16, 86, 0);
        label (cr, "available in Data view.", 16, 104, 0);
    } else {
        chart (cr, spec, projection, width, height);
    }

    cairo_status_t status = cairo_status (cr);
    cairo_destroy (cr);
    cr = NULL;
    cairo_surface_flush (surface);
    if (status != CAIRO_STATUS_SUCCESS || cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
        reject (error, "Cairo chart rendering failed.");
        goto invalid;
    }
    *projection_out = projection;
    *surface_out = surface;
    *fallback_out = fallback;
    return TRUE;

invalid:
    if (cr != NULL) cairo_destroy (cr);
    atm_chart_projection_free (projection);
    cairo_surface_destroy (surface);
    return FALSE;
}
gboolean atm_chart_render_new (const AtmChartSpec *spec, guint width, guint height,
    guint scale, AtmChartRender **out, GError **error)
{
    if (spec == NULL || out == NULL || *out != NULL || width < 240 || height < 140 ||
        width > ATM_CHART_RENDER_MAX_SIZE || height > ATM_CHART_RENDER_MAX_SIZE ||
        scale < 1 || scale > 2)
        return reject (error, "Invalid chart canvas dimensions or output.");
    /* Both admitted source-specific chart profiles have independently qualified
     * projections; arbitrary ChartSpec versions remain fail-closed. */
    if (atm_chart_spec_version (spec) != ATM_CHART_SPEC_VERSION_GISTEMP_V1 &&
        atm_chart_spec_version (spec) != ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2 &&
        atm_chart_spec_version (spec) != ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return reject (error, "Chart renderer only admits qualified GISTEMP v1, Energy Institute v2, or pinned BNR C01 v3.");
    AtmChartRender *r = g_new0 (AtmChartRender, 1);
    if (!atm_chart_spec_rebuild (spec, &r->spec, error)) goto invalid;
    r->data = data_view (r->spec);
    if (!prepare_viewport (r->spec, width, height, scale,
            &r->projection, &r->surface, &r->fallback, error))
        goto invalid;
    *out = r;
    return TRUE;
invalid:
    atm_chart_render_free (r);
    return FALSE;
}
gboolean atm_chart_render_resize (AtmChartRender *r, guint width, guint height,
    guint scale, GError **error)
{
    if (r == NULL || r->spec == NULL)
        return reject (error, "Invalid chart renderer.");
    AtmChartProjection *projection = NULL;
    cairo_surface_t *surface = NULL;
    gboolean fallback = FALSE;
    if (!prepare_viewport (r->spec, width, height, scale,
            &projection, &surface, &fallback, error))
        return FALSE;

    AtmChartProjection *old_projection = r->projection;
    cairo_surface_t *old_surface = r->surface;
    r->projection = projection;
    r->surface = surface;
    r->fallback = fallback;
    atm_chart_projection_free (old_projection);
    if (old_surface != NULL) cairo_surface_destroy (old_surface);
    return TRUE;
}
cairo_surface_t *atm_chart_render_surface (const AtmChartRender *r) { return r ? r->surface : NULL; }
const char *atm_chart_render_data (const AtmChartRender *r) { return r ? r->data : NULL; }
gboolean atm_chart_render_is_fallback (const AtmChartRender *r) { return r ? r->fallback : FALSE; }
const AtmChartSpec *atm_chart_render_spec (const AtmChartRender *r) { return r ? r->spec : NULL; }
const AtmChartProjection *atm_chart_render_projection (const AtmChartRender *r) { return r ? r->projection : NULL; }
