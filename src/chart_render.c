#include "chart_render.h"
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
    atm_chart_spec_free (r->spec); atm_chart_projection_free (r->projection);
    if (r->surface != NULL) cairo_surface_destroy (r->surface);
    g_free (r->data); g_free (r);
}
/* Closed, source-qualified ASCII metadata and exact source spelling. No model
 * strings enter the prototype. The future widget must use plain text cells. */
static char *data_view (const AtmChartSpec *spec)
{
    GString *s = g_string_new ("# GISTEMP global annual temperature anomaly\n");
    g_string_append_printf (s, "# unit\t%s\n# reference_period\t%s\n# chart_spec_id\t%s\n",
        atm_verified_series_unit (atm_chart_spec_series (spec, 0)),
        atm_verified_series_reference_period (atm_chart_spec_series (spec, 0)), atm_chart_spec_id (spec));
    g_string_append (s, "# source_repository\tLaurentiuStaicu/empirical-world3-dynamics\n"
        "# source_snapshot\td9e249339663015f6d1c05752338a955bf64ad0b\n"
        "# source_path\tscience/data/processed/nasa_gistemp_global_2026.csv\n"
        "# access_vintage\t2026-08-31\n"
        "series\tyear\tsource_decimal\tcoefficient\texponent\tsource_row\tqualified_series_id\tsupport_ids\n");
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
        fits (cr, "Source: EWD pinned GISTEMP | vintage 2026-08-31", width - 32);
}
static void chart (cairo_t *cr, AtmChartRender *r, guint width, guint height)
{
    double left = 84, top = 108, pw = width - 112, ph = height - 188;
    AtmChartProjection *p = r->projection;
    font (cr, 14, TRUE);
    label (cr, "GISTEMP / GLOBAL ANNUAL TEMPERATURE", 16, 26, 0);
    font (cr, 12, FALSE);
    label (cr, "Anomaly relative to 1951-1980 (deg C)", 16, 47, 0);
    const char *kind = atm_chart_spec_kind (r->spec) == ATM_CHART_LINE ? "LINE" : "SCATTER";
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
    /* Keep endpoint markers visible while bounding plot ink away from labels. */
    cairo_rectangle (cr, left - 3, top - 3, pw + 6, ph + 6); cairo_clip (cr);
    cairo_set_line_width (cr, 1.5);
    for (guint i = 0; i < atm_chart_projection_count (p); i++) {
        const AtmChartProjectedPoint *point = atm_chart_projection_point (p, i);
        double x = left + point->x, y = top + point->y;
        if (atm_chart_spec_kind (r->spec) == ATM_CHART_SCATTER) {
            cairo_rectangle (cr, x - 1.5, y - 1.5, 3, 3); cairo_fill (cr);
        } else if (i == 0 || point->point_index == 0) {
            if (i > 0) cairo_stroke (cr);
            cairo_move_to (cr, x, y);
        } else cairo_line_to (cr, x, y);
    }
    if (atm_chart_spec_kind (r->spec) == ATM_CHART_LINE) cairo_stroke (cr);
    cairo_restore (cr);
    font (cr, 12, FALSE);
    label (cr, "Source: EWD pinned GISTEMP | vintage 2026-08-31", 16, height - 36, 0);
    label (cr, "Exact values and provenance: Data view", 16, height - 16, 0);
}
gboolean atm_chart_render_new (const AtmChartSpec *spec, guint width, guint height,
    guint scale, AtmChartRender **out, GError **error)
{
    if (out == NULL || *out != NULL || width < 240 || height < 140 ||
        width > ATM_CHART_RENDER_MAX_SIZE || height > ATM_CHART_RENDER_MAX_SIZE || scale < 1 || scale > 2)
        return reject (error, "Invalid chart canvas dimensions or output.");
    AtmChartRender *r = g_new0 (AtmChartRender, 1);
    if (!atm_chart_spec_rebuild (spec, &r->spec, error)) goto invalid;
    r->data = data_view (r->spec);
    r->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width * scale, height * scale);
    if (cairo_surface_status (r->surface) != CAIRO_STATUS_SUCCESS) {
        reject (error, "Cannot allocate chart surface."); goto invalid;
    }
    cairo_surface_set_device_scale (r->surface, scale, scale);
    cairo_t *cr = cairo_create (r->surface);
    cairo_set_source_rgb (cr, 1, 1, 1); cairo_paint (cr);
    cairo_set_source_rgb (cr, 0, 0, 0); cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_NONE);
    cairo_set_font_options (cr, options); cairo_font_options_destroy (options);
    r->fallback = width < 432 || height < 348;
    if (!r->fallback) {
        if (!atm_chart_projection_new (r->spec, width - 112, height - 188, &r->projection, error)) {
            cairo_destroy (cr); goto invalid;
        }
        font (cr, 14, TRUE);
        r->fallback = !fits (cr, "GISTEMP / GLOBAL ANNUAL TEMPERATURE", width - 32);
        font (cr, 12, FALSE);
        r->fallback = r->fallback || !fits (cr, "1880-2025 | 146 observations | empirical | SCATTER", width - 32) ||
                      !layout_fits (cr, r->projection, width);
    }
    if (r->fallback) {
        font (cr, 14, TRUE); label (cr, "GISTEMP", 16, 30, 0);
        font (cr, 12, FALSE); label (cr, "Chart needs more space.", 16, 60, 0);
        label (cr, "Exact values remain", 16, 86, 0);
        label (cr, "available in Data view.", 16, 104, 0);
    } else chart (cr, r, width, height);
    cairo_status_t status = cairo_status (cr); cairo_destroy (cr); cairo_surface_flush (r->surface);
    if (status != CAIRO_STATUS_SUCCESS || cairo_surface_status (r->surface) != CAIRO_STATUS_SUCCESS) {
        reject (error, "Cairo chart rendering failed."); goto invalid;
    }
    *out = r; return TRUE;
invalid:
    atm_chart_render_free (r); return FALSE;
}
cairo_surface_t *atm_chart_render_surface (const AtmChartRender *r) { return r ? r->surface : NULL; }
const char *atm_chart_render_data (const AtmChartRender *r) { return r ? r->data : NULL; }
gboolean atm_chart_render_is_fallback (const AtmChartRender *r) { return r ? r->fallback : FALSE; }
const AtmChartSpec *atm_chart_render_spec (const AtmChartRender *r) { return r ? r->spec : NULL; }
