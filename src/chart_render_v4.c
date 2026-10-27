#include "chart_render_v4.h"

#include "scientific_canonical.h"

#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

struct AtmChartRenderV4 {
    AtmChartSpecV4 *owned_spec;
    AtmChartProjectionV4 *projection;
    cairo_surface_t *surface;
    char *data;
    gboolean fallback;
    guint width;
    guint height;
    guint device_scale;
    char *id;
};

GQuark
atm_chart_render_v4_error_quark (void)
{
    return g_quark_from_static_string ("atm-chart-render-v4-error-quark");
}

static gboolean
reject (GError **error,
        AtmChartRenderV4Error code,
        const char *message)
{
    g_set_error_literal (error, ATM_CHART_RENDER_V4_ERROR, code, message);
    return FALSE;
}

static void
font (cairo_t *cr, double size, gboolean bold)
{
    cairo_select_font_face (cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size (cr, size);
}

static void
label (cairo_t *cr, const char *value, double x, double y, double alignment)
{
    cairo_text_extents_t extents;
    cairo_text_extents (cr, value, &extents);
    cairo_move_to (cr, x - extents.x_bearing - alignment * extents.width, y);
    cairo_show_text (cr, value);
}

static gboolean
fits (cairo_t *cr, const char *value, double width)
{
    cairo_text_extents_t extents;
    if (value == NULL)
        return FALSE;
    cairo_text_extents (cr, value, &extents);
    return isfinite (extents.width) && extents.width <= width;
}

static const char *
x_axis_label (const AtmChartCatalogEntry *entry)
{
    if (entry == NULL)
        return NULL;
    switch (entry->x_kind) {
    case ATM_CHART_CATALOG_X_CALENDAR_YEAR:
        return "Calendar year";
    case ATM_CHART_CATALOG_X_CALENDAR_QUARTER:
        return "Calendar quarter";
    default:
        return NULL;
    }
}

/* Prefer concise labels for the current reviewed dimensions. For any other
 * catalog-qualified unit, fall back to the catalog unit with underscores
 * expanded rather than making the painter depend on a series ID. */
static char *
y_axis_label (const AtmChartCatalogEntry *entry)
{
    if (entry == NULL)
        return NULL;
    if (g_strcmp0 (entry->y_dimension, "temperature_difference") == 0)
        return g_strdup ("deg C anomaly");
    if (g_strcmp0 (entry->y_dimension, "energy_flow_rate") == 0)
        return g_strdup ("EJ/year");
    if (g_strcmp0 (entry->y_dimension, "survey_net_percentage") == 0)
        return g_strdup ("Net %");
    if (entry->y_unit == NULL || *entry->y_unit == '\0')
        return NULL;

    char *label_text = g_strdup (entry->y_unit);
    for (char *cursor = label_text; *cursor != '\0'; cursor++) {
        if (*cursor == '_')
            *cursor = ' ';
    }
    return label_text;
}

static char *
coverage_summary (const AtmChartCatalogEntry *entry)
{
    if (entry == NULL || entry->geography == NULL ||
        entry->coverage_start == NULL || entry->coverage_end == NULL)
        return NULL;

    if (entry->coordinate_count > entry->numeric_observation_count) {
        guint missing_count =
            entry->coordinate_count - entry->numeric_observation_count;
        return g_strdup_printf ("%s | %s-%s | %u values, %u missing",
            entry->geography, entry->coverage_start, entry->coverage_end,
            entry->numeric_observation_count, missing_count);
    }
    return g_strdup_printf ("%s | %s-%s | %u values",
        entry->geography, entry->coverage_start, entry->coverage_end,
        entry->numeric_observation_count);
}

static char *
source_footer (const AtmChartCatalogEntry *entry)
{
    if (entry == NULL || entry->publisher_name == NULL)
        return NULL;
    return g_strdup_printf ("Source: %s", entry->publisher_name);
}

static char *
detail_footer (const AtmChartCatalogEntry *entry)
{
    if (entry != NULL && entry->missing_coordinate != NULL) {
        const char *reason =
            g_strcmp0 (entry->missing_reason, "source_round_not_retained") == 0
                ? "source round not retained"
                : "source marks this observation missing";
        return g_strdup_printf (
            "Missing %s (%s; not zero) | Exact values and source details: Data view",
            entry->missing_coordinate, reason);
    }
    return g_strdup ("Exact values and source details: Data view");
}

static gboolean
data_identity_matches (const AtmChartSpec *source_spec,
                       const char *data,
                       GError **error)
{
    const char *spec_id = atm_chart_spec_id (source_spec);
    if (spec_id == NULL || data == NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_DATA,
            "The catalog-qualified source spec or its exact Data view is unavailable.");

    char *identity_line = g_strdup_printf ("# chart_spec_id\t%s\n", spec_id);
    gboolean matches = strstr (data, identity_line) != NULL;
    g_free (identity_line);
    return matches || reject (error, ATM_CHART_RENDER_V4_ERROR_DATA,
        "The Data view does not identify the exact v4-bound source ChartSpec.");
}

static gboolean
projection_labels_fit (const AtmChartProjection *projection,
                       guint width,
                       guint height)
{
    if (projection == NULL)
        return FALSE;

    const double left = 84.0;
    const double top = 108.0;
    const double plot_height = (double) height - 210.0;
    cairo_surface_t *measure_surface = cairo_image_surface_create (
        CAIRO_FORMAT_ARGB32, 1, 1);
    if (cairo_surface_status (measure_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy (measure_surface);
        return FALSE;
    }
    cairo_t *cr = cairo_create (measure_surface);
    gboolean ok = cairo_status (cr) == CAIRO_STATUS_SUCCESS;
    if (ok) {
        font (cr, 12.0, FALSE);
        double previous_right = -1.0;
        for (guint i = 0;
             i < atm_chart_projection_x_tick_count (projection); i++) {
            const AtmChartTick *tick =
                atm_chart_projection_x_tick (projection, i);
            if (tick == NULL) { ok = FALSE; break; }
            cairo_text_extents_t extents;
            cairo_text_extents (cr, tick->label, &extents);
            double center = left + tick->position;
            double tick_left = center - extents.width / 2.0;
            double tick_right = center + extents.width / 2.0;
            if (!isfinite (tick_left) || !isfinite (tick_right) ||
                tick_left < 12.0 || tick_right > width - 12.0 ||
                tick_left < previous_right + 5.0) {
                ok = FALSE;
                break;
            }
            previous_right = tick_right;
        }

        double tick_tops[ATM_CHART_PROJECTION_MAX_TICKS];
        double tick_bottoms[ATM_CHART_PROJECTION_MAX_TICKS];
        guint measured_ticks = 0;
        for (guint i = 0; ok &&
             i < atm_chart_projection_y_tick_count (projection); i++) {
            const AtmChartTick *tick =
                atm_chart_projection_y_tick (projection, i);
            if (tick == NULL ||
                measured_ticks >= ATM_CHART_PROJECTION_MAX_TICKS) {
                ok = FALSE;
                break;
            }
            cairo_text_extents_t extents;
            cairo_text_extents (cr, tick->label, &extents);
            double baseline = top + tick->position + 4.0;
            double tick_top = baseline + extents.y_bearing;
            double tick_bottom = tick_top + extents.height;
            if (!isfinite (tick_top) || !isfinite (tick_bottom) ||
                extents.width > 64.0 ||
                baseline > top + plot_height + 4.0) {
                ok = FALSE;
                break;
            }

            /* Source projections do not all store Y ticks in visual order:
             * GISTEMP and Energy Institute enumerate low-to-high while BNR
             * enumerates high-to-low. Check interval overlap independent of
             * that internal order instead of falsely forcing every layout to
             * fallback for one of those valid orderings. */
            for (guint previous = 0; previous < measured_ticks; previous++) {
                gboolean separated =
                    tick_bottom + 2.0 <= tick_tops[previous] ||
                    tick_top >= tick_bottoms[previous] + 2.0;
                if (!separated) {
                    ok = FALSE;
                    break;
                }
            }
            if (!ok)
                break;
            tick_tops[measured_ticks] = tick_top;
            tick_bottoms[measured_ticks] = tick_bottom;
            measured_ticks++;
        }
    }
    cairo_destroy (cr);
    cairo_surface_destroy (measure_surface);
    return ok;
}

static gboolean
layout_fits (const AtmChartSpecV4 *spec,
             const AtmChartProjection *projection,
             guint width,
             guint height)
{
    const AtmChartCatalogEntry *entry =
        atm_chart_spec_v4_catalog_entry (spec);
    const char *title = entry != NULL ? entry->display_name : NULL;
    const char *subtitle = entry != NULL ? entry->observation_meaning : NULL;
    char *y_label = y_axis_label (entry);
    char *summary = coverage_summary (entry);
    char *footer = source_footer (entry);
    char *details = detail_footer (entry);
    cairo_surface_t *measure_surface = cairo_image_surface_create (
        CAIRO_FORMAT_ARGB32, 1, 1);
    if (cairo_surface_status (measure_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy (measure_surface);
        g_free (summary);
        g_free (footer);
        g_free (y_label);
        g_free (details);
        return FALSE;
    }
    cairo_t *cr = cairo_create (measure_surface);
    gboolean ok = cairo_status (cr) == CAIRO_STATUS_SUCCESS;
    if (ok) {
        font (cr, 14.0, TRUE);
        ok = fits (cr, title, width - 32.0);
        font (cr, 12.0, FALSE);
        ok = ok && fits (cr, subtitle, width - 32.0) &&
            fits (cr, summary, width - 32.0) &&
            fits (cr, y_label, width - 32.0) &&
            fits (cr, x_axis_label (entry), width - 32.0) &&
            fits (cr, footer, width - 32.0) &&
            fits (cr, details, width - 32.0);
    }
    cairo_destroy (cr);
    cairo_surface_destroy (measure_surface);
    if (ok)
        ok = projection_labels_fit (projection, width, height);
    g_free (summary);
    g_free (footer);
    g_free (y_label);
    g_free (details);
    return ok;
}

static void
paint_fallback (cairo_t *cr)
{
    font (cr, 14.0, TRUE);
    label (cr, "Chart needs more space.", 16.0, 30.0, 0.0);
    font (cr, 12.0, FALSE);
    label (cr, "Exact values remain available in Data view.", 16.0, 58.0, 0.0);
}

/*
 * The painter consumes only the validated catalog-bound projection and
 * metadata. Source admission/decimal conversion stay in the source adapter;
 * no source-specific renderer is called here.
 */
static gboolean
paint_v4_surface (const AtmChartSpecV4 *spec,
                  const AtmChartProjectionV4 *projection_v4,
                  guint width,
                  guint height,
                  guint device_scale,
                  cairo_surface_t **out_surface,
                  gboolean *out_fallback,
                  GError **error)
{
    if (spec == NULL || out_surface == NULL || *out_surface != NULL ||
        out_fallback == NULL || width < 240u || height < 140u ||
        width > ATM_CHART_RENDER_MAX_SIZE ||
        height > ATM_CHART_RENDER_MAX_SIZE ||
        device_scale < 1u || device_scale > 2u) {
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The v4 painter received invalid canvas arguments.");
    }

    cairo_surface_t *surface = cairo_image_surface_create (
        CAIRO_FORMAT_ARGB32, width * device_scale, height * device_scale);
    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy (surface);
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Cairo could not allocate the v4 chart surface.");
    }
    cairo_surface_set_device_scale (
        surface, (double) device_scale, (double) device_scale);
    cairo_t *cr = cairo_create (surface);
    if (cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy (cr);
        cairo_surface_destroy (surface);
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Cairo could not create a drawing context.");
    }

    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint (cr);
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_NONE);
    cairo_set_font_options (cr, options);
    cairo_font_options_destroy (options);

    const AtmChartCatalogEntry *entry =
        atm_chart_spec_v4_catalog_entry (spec);
    const AtmChartProjection *projection =
        atm_chart_projection_v4_source_projection (projection_v4);
    gboolean fallback = projection_v4 == NULL ||
        width < 432u || height < 370u ||
        !layout_fits (spec, projection, width, height);

    if (fallback) {
        paint_fallback (cr);
    } else {
        const double left = 84.0;
        const double top = 108.0;
        const double plot_width = (double) width - 112.0;
        const double plot_height = (double) height - 210.0;

        font (cr, 14.0, TRUE);
        label (cr, entry->display_name, 16.0, 26.0, 0.0);
        font (cr, 12.0, FALSE);
        label (cr, entry->observation_meaning, 16.0, 47.0, 0.0);
        char *summary = coverage_summary (entry);
        label (cr, summary, 16.0, 68.0, 0.0);
        g_free (summary);
        char *y_label = y_axis_label (entry);
        label (cr, y_label, left, 96.0, 0.0);
        g_free (y_label);

        const double dots[] = {1.0, 5.0};
        cairo_set_line_width (cr, 1.0);
        for (guint i = 0;
             i < atm_chart_projection_y_tick_count (projection); i++) {
            const AtmChartTick *tick =
                atm_chart_projection_y_tick (projection, i);
            double y = top + tick->position;
            cairo_set_dash (cr, dots, G_N_ELEMENTS (dots), 0.0);
            cairo_move_to (cr, left, y);
            cairo_line_to (cr, left + plot_width, y);
            cairo_stroke (cr);
            cairo_set_dash (cr, NULL, 0, 0);
            label (cr, tick->label, left - 12.0, y + 4.0, 1.0);
        }

        cairo_rectangle (cr, left, top, plot_width, plot_height);
        cairo_stroke (cr);
        for (guint i = 0;
             i < atm_chart_projection_x_tick_count (projection); i++) {
            const AtmChartTick *tick =
                atm_chart_projection_x_tick (projection, i);
            double x = left + tick->position;
            cairo_move_to (cr, x, top + plot_height);
            cairo_line_to (cr, x, top + plot_height + 5.0);
            cairo_stroke (cr);
            label (cr, tick->label, x, top + plot_height + 22.0, 0.5);
        }

        cairo_save (cr);
        cairo_rectangle (cr, left - 3.0, top - 3.0,
            plot_width + 6.0, plot_height + 6.0);
        cairo_clip (cr);
        cairo_set_line_width (cr, 1.5);
        guint previous_point_index = G_MAXUINT;
        gboolean path_started = FALSE;
        AtmChartKind kind = atm_chart_spec_v4_kind (spec);
        for (guint i = 0; i < atm_chart_projection_count (projection); i++) {
            const AtmChartProjectedPoint *point =
                atm_chart_projection_point (projection, i);
            if (point == NULL)
                continue;
            double x = left + point->x;
            double y = top + point->y;
            if (kind == ATM_CHART_SCATTER) {
                cairo_rectangle (cr, x - 1.5, y - 1.5, 3.0, 3.0);
                cairo_fill (cr);
            } else if (!path_started) {
                cairo_move_to (cr, x, y);
                path_started = TRUE;
            } else if (point->point_index != previous_point_index + 1u) {
                /* A catalog-authorized MISSING coordinate creates a true gap. */
                cairo_stroke (cr);
                cairo_move_to (cr, x, y);
            } else {
                cairo_line_to (cr, x, y);
            }
            previous_point_index = point->point_index;
        }
        if (kind == ATM_CHART_LINE && path_started)
            cairo_stroke (cr);
        cairo_restore (cr);

        font (cr, 12.0, FALSE);
        label (cr, x_axis_label (entry),
            left + plot_width / 2.0, (double) height - 56.0, 0.5);
        char *footer = source_footer (entry);
        label (cr, footer, 16.0, (double) height - 36.0, 0.0);
        g_free (footer);
        char *details = detail_footer (entry);
        label (cr, details, 16.0, (double) height - 16.0, 0.0);
        g_free (details);
    }

    cairo_status_t drawing_status = cairo_status (cr);
    cairo_destroy (cr);
    cr = NULL;
    cairo_surface_flush (surface);
    if (drawing_status != CAIRO_STATUS_SUCCESS ||
        cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy (surface);
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Cairo failed while painting the catalog-bound v4 chart.");
    }

    *out_surface = surface;
    *out_fallback = fallback;
    return TRUE;
}

static gboolean
build_id (const AtmChartRenderV4 *render,
          char **out_id,
          GError **error)
{
    const AtmChartSpecV4 *spec = render->owned_spec;
    const AtmChartSpec *source = atm_chart_spec_v4_source_spec (spec);
    const char *projection_policy = render->projection != NULL
        ? atm_chart_projection_v4_policy (render->projection) : NULL;

    JsonBuilder *builder = json_builder_new ();
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "schema");
    json_builder_add_string_value (builder, ATM_CHART_RENDER_V4_SCHEMA);
    json_builder_set_member_name (builder, "id_domain");
    json_builder_add_string_value (builder, ATM_CHART_RENDER_V4_ID_DOMAIN);
    json_builder_set_member_name (builder, "chart_spec_v4_id");
    json_builder_add_string_value (builder, atm_chart_spec_v4_id (spec));
    json_builder_set_member_name (builder, "source_chart_spec_id");
    json_builder_add_string_value (builder, atm_chart_spec_id (source));
    json_builder_set_member_name (builder, "render_style");
    json_builder_add_string_value (builder, ATM_CHART_RENDER_STYLE);
    json_builder_set_member_name (builder, "width");
    json_builder_add_int_value (builder, render->width);
    json_builder_set_member_name (builder, "height");
    json_builder_add_int_value (builder, render->height);
    json_builder_set_member_name (builder, "device_scale");
    json_builder_add_int_value (builder, render->device_scale);
    json_builder_set_member_name (builder, "fallback");
    json_builder_add_boolean_value (builder, render->fallback);
    json_builder_set_member_name (builder, "projection_policy");
    if (projection_policy == NULL)
        json_builder_add_null_value (builder);
    else
        json_builder_add_string_value (builder, projection_policy);
    json_builder_end_object (builder);

    JsonNode *root = json_builder_get_root (builder);
    char *json = json_to_string (root, FALSE);
    gboolean ok = atm_scientific_content_id_json (
        ATM_CHART_RENDER_V4_ID_DOMAIN, json, out_id, error);
    g_free (json);
    json_node_free (root);
    g_object_unref (builder);
    return ok;
}

gboolean
atm_chart_render_v4_validate (const AtmChartRenderV4 *render,
                              GError **error)
{
    if (render == NULL || render->owned_spec == NULL ||
        render->surface == NULL || render->data == NULL ||
        render->width < 240u || render->height < 140u ||
        render->width > ATM_CHART_RENDER_MAX_SIZE ||
        render->height > ATM_CHART_RENDER_MAX_SIZE ||
        render->device_scale < 1u || render->device_scale > 2u ||
        render->id == NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The v4 render is incomplete or outside qualified viewport limits.");

    if (!atm_chart_spec_v4_validate (render->owned_spec, error))
        return FALSE;
    const AtmChartSpec *source_spec =
        atm_chart_spec_v4_source_spec (render->owned_spec);
    if (source_spec == NULL ||
        cairo_surface_get_type (render->surface) != CAIRO_SURFACE_TYPE_IMAGE ||
        cairo_image_surface_get_format (render->surface) != CAIRO_FORMAT_ARGB32 ||
        cairo_image_surface_get_width (render->surface) !=
            (int) (render->width * render->device_scale) ||
        cairo_image_surface_get_height (render->surface) !=
            (int) (render->height * render->device_scale) ||
        cairo_surface_status (render->surface) != CAIRO_STATUS_SUCCESS)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The v4 render does not own a valid ARGB32 surface at its declared scale.");

    if (!data_identity_matches (source_spec, render->data, error))
        return FALSE;

    if (render->fallback) {
        if (render->projection != NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A fallback render cannot claim an active v4 plot projection.");
    } else {
        if (render->projection == NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A non-fallback render requires a validated v4 projection.");
        if (!atm_chart_projection_v4_validate (render->projection, error))
            return FALSE;
        const AtmChartCatalogEntry *entry =
            atm_chart_spec_v4_catalog_entry (render->owned_spec);
        char *expected_policy = entry != NULL
            ? g_strdup_printf ("%s/%s", entry->display_profile_id,
                               entry->display_profile_version)
            : NULL;
        gboolean projection_identity_matches =
            entry != NULL &&
            g_strcmp0 (atm_chart_projection_v4_series_id (render->projection),
                       atm_chart_spec_v4_series_id (render->owned_spec)) == 0 &&
            expected_policy != NULL &&
            g_strcmp0 (atm_chart_projection_v4_policy (render->projection),
                       expected_policy) == 0 &&
            atm_chart_projection_v4_coordinate_count (render->projection) ==
                atm_chart_spec_v4_coordinate_count (render->owned_spec) &&
            atm_chart_projection_v4_numeric_observation_count (render->projection) ==
                atm_chart_spec_v4_numeric_observation_count (render->owned_spec);
        g_free (expected_policy);
        if (!projection_identity_matches)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "The rendered v4 projection differs from catalog identity or coverage.");
    }

    char *expected_id = NULL;
    if (!build_id (render, &expected_id, error))
        return FALSE;
    gboolean matches = g_strcmp0 (render->id, expected_id) == 0;
    g_free (expected_id);
    return matches || reject (error, ATM_CHART_RENDER_V4_ERROR_IDENTITY,
        "Render v4 identity differs from reconstructed spec, viewport and display policy.");
}

gboolean
atm_chart_render_v4_new (const AtmChartSpecV4 *spec,
                         guint width,
                         guint height,
                         guint device_scale,
                         AtmChartRenderV4 **out,
                         GError **error)
{
    if (out == NULL || *out != NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Render output must be non-NULL and initially empty.");
    if (spec == NULL || width < 240u || height < 140u ||
        width > ATM_CHART_RENDER_MAX_SIZE ||
        height > ATM_CHART_RENDER_MAX_SIZE ||
        device_scale < 1u || device_scale > 2u)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The v4 render dimensions or device scale are outside qualified limits.");

    AtmChartSpecV4 *owned_spec = NULL;
    if (!atm_chart_spec_v4_rebuild (spec, &owned_spec, error))
        return FALSE;
    char *data = atm_chart_render_data_for_spec (
        atm_chart_spec_v4_source_spec (owned_spec), error);
    if (data == NULL) {
        atm_chart_spec_v4_free (owned_spec);
        return FALSE;
    }
    if (!data_identity_matches (
            atm_chart_spec_v4_source_spec (owned_spec), data, error)) {
        g_free (data);
        atm_chart_spec_v4_free (owned_spec);
        return FALSE;
    }

    AtmChartProjectionV4 *projection = NULL;
    cairo_surface_t *surface = NULL;
    gboolean fallback = width < 432u || height < 370u;
    if (!fallback) {
        if (!atm_chart_projection_v4_new (
                owned_spec, (double) width - 112.0,
                (double) height - 210.0, &projection, error)) {
            g_free (data);
            atm_chart_spec_v4_free (owned_spec);
            return FALSE;
        }
    }

    if (!paint_v4_surface (owned_spec, projection, width, height,
            device_scale, &surface, &fallback, error)) {
        atm_chart_projection_v4_free (projection);
        g_free (data);
        atm_chart_spec_v4_free (owned_spec);
        return FALSE;
    }
    if (fallback) {
        atm_chart_projection_v4_free (projection);
        projection = NULL;
    }

    AtmChartRenderV4 *render = g_new0 (AtmChartRenderV4, 1);
    render->owned_spec = owned_spec;
    render->projection = projection;
    render->surface = surface;
    render->data = data;
    render->fallback = fallback;
    render->width = width;
    render->height = height;
    render->device_scale = device_scale;

    if (!build_id (render, &render->id, error) ||
        !atm_chart_render_v4_validate (render, error)) {
        atm_chart_render_v4_free (render);
        return FALSE;
    }
    *out = render;
    return TRUE;
}

void
atm_chart_render_v4_free (AtmChartRenderV4 *render)
{
    if (render == NULL)
        return;
    atm_chart_spec_v4_free (render->owned_spec);
    atm_chart_projection_v4_free (render->projection);
    if (render->surface != NULL)
        cairo_surface_destroy (render->surface);
    g_free (render->data);
    g_free (render->id);
    g_free (render);
}

const AtmChartSpecV4 *
atm_chart_render_v4_spec (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->owned_spec : NULL;
}

const AtmChartProjectionV4 *
atm_chart_render_v4_projection (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->projection : NULL;
}

cairo_surface_t *
atm_chart_render_v4_surface (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->surface : NULL;
}

const char *
atm_chart_render_v4_data (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->data : NULL;
}

const char *
atm_chart_render_v4_schema (const AtmChartRenderV4 *render)
{
    return render != NULL ? ATM_CHART_RENDER_V4_SCHEMA : NULL;
}

const char *
atm_chart_render_v4_id (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->id : NULL;
}

gboolean
atm_chart_render_v4_is_fallback (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->fallback : FALSE;
}
