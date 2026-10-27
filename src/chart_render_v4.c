#include "chart_render_v4.h"

#include "scientific_canonical.h"

#include <cairo.h>
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
                            bold ? CAIRO_FONT_WEIGHT_BOLD
                                 : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size (cr, size);
}

static void
label (cairo_t *cr,
       const char *value,
       double x,
       double y,
       double alignment)
{
    cairo_text_extents_t extents;
    cairo_text_extents (cr, value, &extents);
    cairo_move_to (cr,
                   x - extents.x_bearing - alignment * extents.width,
                   y);
    cairo_show_text (cr, value);
}

static gboolean
fits (cairo_t *cr, const char *value, double width)
{
    if (value == NULL)
        return FALSE;
    cairo_text_extents_t extents;
    cairo_text_extents (cr, value, &extents);
    return isfinite (extents.width) && extents.width <= width;
}

/* A unit presentation map, not a series-specific rendering branch. */
static const char *
axis_unit_label (const AtmChartCatalogEntry *entry)
{
    if (entry == NULL || entry->y_unit == NULL)
        return "Units";
    if (g_strcmp0 (entry->y_unit, "degree_Celsius_anomaly") == 0)
        return "Anomaly (deg C)";
    if (g_strcmp0 (entry->y_unit, "exajoules/year") == 0)
        return "EJ/year";
    if (g_strcmp0 (entry->y_unit, "percent") == 0)
        return "Net %";
    return entry->y_unit;
}

static const char *
chart_kind_name (AtmChartKind kind)
{
    switch (kind) {
    case ATM_CHART_LINE:
        return "LINE";
    case ATM_CHART_SCATTER:
        return "SCATTER";
    default:
        return NULL;
    }
}

static gboolean
catalog_allows_kind (const AtmChartCatalogEntry *entry, AtmChartKind kind)
{
    const char *name = chart_kind_name (kind);
    if (entry == NULL || name == NULL || entry->allowed_chart_kinds == NULL)
        return FALSE;
    for (gsize i = 0; i < entry->allowed_chart_kind_count; i++) {
        if (g_ascii_strcasecmp (entry->allowed_chart_kinds[i], name) == 0)
            return TRUE;
    }
    return FALSE;
}

static char *
make_summary (const AtmChartCatalogEntry *entry, AtmChartKind kind)
{
    if (entry == NULL || entry->coverage_start == NULL ||
        entry->coverage_end == NULL || entry->epistemic_status == NULL ||
        chart_kind_name (kind) == NULL)
        return NULL;

    if (entry->missing_coordinate != NULL) {
        return g_strdup_printf (
            "%s-%s | %u numeric + 1 MISSING | %s | %s",
            entry->coverage_start,
            entry->coverage_end,
            entry->numeric_observation_count,
            entry->epistemic_status,
            chart_kind_name (kind));
    }

    return g_strdup_printf (
        "%s-%s | %u observations | %s | %s",
        entry->coverage_start,
        entry->coverage_end,
        entry->numeric_observation_count,
        entry->epistemic_status,
        chart_kind_name (kind));
}

static char *
make_footer_source (const AtmChartCatalogEntry *entry)
{
    if (entry == NULL || entry->publisher_name == NULL)
        return NULL;
    return g_strdup_printf ("Source: %s", entry->publisher_name);
}

static char *
make_footer_data (const AtmChartCatalogEntry *entry)
{
    if (entry != NULL && entry->missing_coordinate != NULL) {
        return g_strdup_printf (
            "%s is missing, not zero | Exact values and provenance: Data view",
            entry->missing_coordinate);
    }
    return g_strdup ("Exact observations and provenance: Data view");
}

/*
 * Check the actual text extents rather than assuming that catalog prose or
 * tick labels fit a particular viewport. This policy is shared by all
 * catalog entries; source adapters provide data and metadata, not plot layout.
 */
static gboolean
layout_fits (cairo_t *cr,
             const AtmChartCatalogEntry *entry,
             const AtmChartProjection *projection,
             AtmChartKind kind,
             guint width,
             guint height,
             char **out_title,
             char **out_summary,
             char **out_footer_source,
             char **out_footer_data)
{
    if (entry == NULL || projection == NULL || out_title == NULL ||
        out_summary == NULL || out_footer_source == NULL ||
        out_footer_data == NULL || entry->display_name == NULL ||
        entry->observation_meaning == NULL)
        return FALSE;

    char *title = g_utf8_strup (entry->display_name, -1);
    char *summary = make_summary (entry, kind);
    char *footer_source = make_footer_source (entry);
    char *footer_data = make_footer_data (entry);
    if (title == NULL || summary == NULL || footer_source == NULL ||
        footer_data == NULL) {
        g_free (title);
        g_free (summary);
        g_free (footer_source);
        g_free (footer_data);
        return FALSE;
    }

    font (cr, 14, TRUE);
    gboolean ok = fits (cr, title, width - 32.0);
    font (cr, 12, FALSE);
    ok = ok && fits (cr, entry->observation_meaning, width - 32.0) &&
         fits (cr, summary, width - 32.0) &&
         fits (cr, footer_source, width - 32.0) &&
         fits (cr, footer_data, width - 32.0) &&
         fits (cr, axis_unit_label (entry), width - 32.0);

    if (!ok) {
        g_free (title);
        g_free (summary);
        g_free (footer_source);
        g_free (footer_data);
        return FALSE;
    }

    double previous_right = -1.0;
    for (guint i = 0; i < atm_chart_projection_x_tick_count (projection); i++) {
        const AtmChartTick *tick =
            atm_chart_projection_x_tick (projection, i);
        if (tick == NULL) {
            ok = FALSE;
            break;
        }
        cairo_text_extents_t extents;
        cairo_text_extents (cr, tick->label, &extents);
        double center = 84.0 + tick->position;
        double left = center - extents.width / 2.0;
        double right = center + extents.width / 2.0;
        if (left < 12.0 || right > width - 12.0 ||
            (previous_right >= 0.0 && left < previous_right + 6.0)) {
            ok = FALSE;
            break;
        }
        previous_right = right;
    }

    double previous_bottom = -G_MAXDOUBLE;
    for (guint i = 0; ok &&
         i < atm_chart_projection_y_tick_count (projection); i++) {
        const AtmChartTick *tick =
            atm_chart_projection_y_tick (projection, i);
        if (tick == NULL) {
            ok = FALSE;
            break;
        }
        cairo_text_extents_t extents;
        cairo_text_extents (cr, tick->label, &extents);
        double baseline = 108.0 + tick->position + 4.0;
        double top = baseline + extents.y_bearing;
        double bottom = top + extents.height;
        if (extents.width > 58.0 || top < previous_bottom + 4.0 ||
            top < 2.0 || bottom > height - 2.0) {
            ok = FALSE;
            break;
        }
        previous_bottom = bottom;
    }

    if (!ok) {
        g_free (title);
        g_free (summary);
        g_free (footer_source);
        g_free (footer_data);
        return FALSE;
    }

    *out_title = title;
    *out_summary = summary;
    *out_footer_source = footer_source;
    *out_footer_data = footer_data;
    return TRUE;
}

static void
draw_fallback (cairo_t *cr)
{
    font (cr, 11, TRUE);
    label (cr, "Chart unavailable at this size.", 16.0, 32.0, 0.0);
    font (cr, 11, FALSE);
    label (cr, "Exact values are in Data.", 16.0, 54.0, 0.0);
    label (cr, "Source details remain available.", 16.0, 76.0, 0.0);
}

static void
draw_chart (cairo_t *cr,
            const AtmChartSpecV4 *spec,
            const AtmChartProjectionV4 *projection_v4,
            const char *title,
            const char *summary,
            const char *footer_source,
            const char *footer_data,
            guint width,
            guint height)
{
    const AtmChartSpec *source_spec =
        atm_chart_spec_v4_source_spec (spec);
    const AtmChartCatalogEntry *entry =
        atm_chart_spec_v4_catalog_entry (spec);
    const AtmChartProjection *projection =
        atm_chart_projection_v4_source_projection (projection_v4);
    AtmChartKind kind = atm_chart_spec_v4_kind (spec);
    const double left = 84.0;
    const double top = 108.0;
    const double plot_width = (double) width - 112.0;
    const double plot_height = (double) height - 188.0;

    font (cr, 14, TRUE);
    label (cr, title, 16.0, 26.0, 0.0);
    font (cr, 12, FALSE);
    label (cr, entry->observation_meaning, 16.0, 47.0, 0.0);
    label (cr, summary, 16.0, 68.0, 0.0);
    label (cr, axis_unit_label (entry), left, 96.0, 0.0);

    const double dots[] = {1.0, 5.0};
    cairo_set_line_width (cr, 1.0);
    for (guint i = 0; i < atm_chart_projection_y_tick_count (projection); i++) {
        const AtmChartTick *tick =
            atm_chart_projection_y_tick (projection, i);
        double y = top + tick->position;
        cairo_set_dash (cr, dots, 2, 0.0);
        cairo_move_to (cr, left, y);
        cairo_line_to (cr, left + plot_width, y);
        cairo_stroke (cr);
        cairo_set_dash (cr, NULL, 0, 0.0);
        label (cr, tick->label, left - 12.0, y + 4.0, 1.0);
    }

    cairo_set_dash (cr, NULL, 0, 0.0);
    cairo_set_line_width (cr, 1.0);
    cairo_rectangle (cr, left, top, plot_width, plot_height);
    cairo_stroke (cr);

    for (guint i = 0; i < atm_chart_projection_x_tick_count (projection); i++) {
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
    cairo_set_dash (cr, NULL, 0, 0.0);
    cairo_set_line_width (cr, 1.5);

    guint previous_index = G_MAXUINT;
    gboolean path_started = FALSE;
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
        } else if (point->point_index != previous_index + 1u) {
            /* A missing coordinate breaks the path; never bridge a gap. */
            cairo_stroke (cr);
            cairo_move_to (cr, x, y);
        } else {
            cairo_line_to (cr, x, y);
        }
        previous_index = point->point_index;
    }
    if (kind == ATM_CHART_LINE && path_started)
        cairo_stroke (cr);
    cairo_restore (cr);

    font (cr, 12, FALSE);
    label (cr, footer_source, 16.0, height - 36.0, 0.0);
    label (cr, footer_data, 16.0, height - 16.0, 0.0);

    (void) source_spec;
}

static gboolean
prepare_surface (AtmChartRenderV4 *render, GError **error)
{
    cairo_surface_t *surface = cairo_image_surface_create (
        CAIRO_FORMAT_ARGB32,
        render->width * render->device_scale,
        render->height * render->device_scale);
    if (surface == NULL ||
        cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL)
            cairo_surface_destroy (surface);
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Cannot allocate the generic v4 Cairo image surface.");
    }
    cairo_surface_set_device_scale (
        surface, render->device_scale, render->device_scale);

    cairo_t *cr = cairo_create (surface);
    if (cr == NULL || cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
        if (cr != NULL)
            cairo_destroy (cr);
        cairo_surface_destroy (surface);
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Cannot create a Cairo context for the generic v4 painter.");
    }

    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint (cr);
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_NONE);
    cairo_set_font_options (cr, options);
    cairo_font_options_destroy (options);

    gboolean fallback =
        render->width < 432u || render->height < 348u;
    if (!fallback) {
        if (!atm_chart_projection_v4_new (
                render->owned_spec,
                (double) render->width - 112.0,
                (double) render->height - 188.0,
                &render->projection,
                error)) {
            cairo_destroy (cr);
            cairo_surface_destroy (surface);
            return FALSE;
        }

        const AtmChartCatalogEntry *entry =
            atm_chart_spec_v4_catalog_entry (render->owned_spec);
        AtmChartKind kind = atm_chart_spec_v4_kind (render->owned_spec);
        char *title = NULL;
        char *summary = NULL;
        char *footer_source = NULL;
        char *footer_data = NULL;

        if (!catalog_allows_kind (entry, kind) ||
            !layout_fits (
                cr, entry,
                atm_chart_projection_v4_source_projection (render->projection),
                kind, render->width, render->height,
                &title, &summary, &footer_source, &footer_data)) {
            atm_chart_projection_v4_free (render->projection);
            render->projection = NULL;
            fallback = TRUE;
        } else {
            draw_chart (
                cr, render->owned_spec, render->projection,
                title, summary, footer_source, footer_data,
                render->width, render->height);
        }

        g_free (title);
        g_free (summary);
        g_free (footer_source);
        g_free (footer_data);
    }

    if (fallback)
        draw_fallback (cr);

    cairo_status_t context_status = cairo_status (cr);
    cairo_destroy (cr);
    cairo_surface_flush (surface);
    cairo_status_t surface_status = cairo_surface_status (surface);
    if (context_status != CAIRO_STATUS_SUCCESS ||
        surface_status != CAIRO_STATUS_SUCCESS) {
        atm_chart_projection_v4_free (render->projection);
        render->projection = NULL;
        cairo_surface_destroy (surface);
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The generic v4 Cairo painter failed while drawing the requested viewport.");
    }

    render->surface = surface;
    render->fallback = fallback;
    return TRUE;
}

static gboolean
data_identity_matches (const AtmChartSpec *source_spec,
                       const char *data,
                       GError **error)
{
    const char *spec_id = atm_chart_spec_id (source_spec);
    if (spec_id == NULL || data == NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_DATA,
            "The generic v4 render requires the exact qualified Data view.");

    char *identity_line = g_strdup_printf (
        "# chart_spec_id\t%s\n", spec_id);
    gboolean matches = strstr (data, identity_line) != NULL;
    g_free (identity_line);
    return matches || reject (error, ATM_CHART_RENDER_V4_ERROR_DATA,
        "The exact Data view does not identify the v4-bound source ChartSpec.");
}

static gboolean
build_id (const AtmChartRenderV4 *render,
          char **out_id,
          GError **error)
{
    const AtmChartSpecV4 *spec = render->owned_spec;
    const AtmChartSpec *source = atm_chart_spec_v4_source_spec (spec);
    const char *projection_policy = NULL;

    if (!render->fallback) {
        if (render->projection == NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A non-fallback v4 render requires a validated generic projection.");
        projection_policy =
            atm_chart_projection_v4_policy (render->projection);
    } else if (render->projection != NULL) {
        return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
            "A fallback render cannot claim an active v4 plot projection.");
    }

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
        render->id == NULL ||
        cairo_surface_status (render->surface) != CAIRO_STATUS_SUCCESS)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The generic v4 render is incomplete or outside the qualified viewport limits.");

    if (!atm_chart_spec_v4_validate (render->owned_spec, error))
        return FALSE;

    const AtmChartSpec *source_spec =
        atm_chart_spec_v4_source_spec (render->owned_spec);
    if (source_spec == NULL ||
        !data_identity_matches (source_spec, render->data, error))
        return FALSE;

    if (render->fallback) {
        if (render->projection != NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A fallback render cannot claim an active v4 plot projection.");
    } else {
        if (render->projection == NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A non-fallback render requires an active v4 projection.");
        if (!atm_chart_projection_v4_validate (render->projection, error))
            return FALSE;

        const AtmChartProjection *source_projection =
            atm_chart_projection_v4_source_projection (render->projection);
        const AtmChartSpec *projected_source =
            atm_chart_projection_spec (source_projection);
        const AtmChartCatalogEntry *entry =
            atm_chart_spec_v4_catalog_entry (render->owned_spec);
        if (source_projection == NULL || projected_source == NULL ||
            entry == NULL || entry->display_profile_id == NULL ||
            entry->display_profile_version == NULL ||
            g_strcmp0 (atm_chart_spec_id (source_spec),
                       atm_chart_spec_id (projected_source)) != 0 ||
            g_strcmp0 (atm_chart_projection_v4_series_id (render->projection),
                       atm_chart_spec_v4_series_id (render->owned_spec)) != 0)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "The projected source identity differs from the catalog-bound v4 specification.");

        char *expected_policy = g_strdup_printf (
            "%s/%s", entry->display_profile_id, entry->display_profile_version);
        gboolean policy_matches = g_strcmp0 (
            atm_chart_projection_v4_policy (render->projection),
            expected_policy) == 0;
        g_free (expected_policy);
        if (!policy_matches)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "The generic painter's projection policy differs from the catalog's exact display-profile version.");

        if (atm_chart_projection_v4_coordinate_count (render->projection) !=
                atm_chart_spec_v4_coordinate_count (render->owned_spec) ||
            atm_chart_projection_v4_numeric_observation_count (render->projection) !=
                atm_chart_spec_v4_numeric_observation_count (render->owned_spec))
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "The rendered projection coverage differs from catalog-bound v4 metadata.");
    }

    char *expected_id = NULL;
    if (!build_id (render, &expected_id, error))
        return FALSE;
    gboolean matches = g_strcmp0 (render->id, expected_id) == 0;
    g_free (expected_id);
    return matches || reject (error, ATM_CHART_RENDER_V4_ERROR_IDENTITY,
        "Render v4 identity differs from reconstructed spec, viewport, and display policy.");
}

gboolean
atm_chart_render_v4_new (const AtmChartSpecV4 *spec,
                         guint width,
                         guint height,
                         guint device_scale,
                         AtmChartRenderV4 **out,
                         GError **error)
{
    if (out == NULL || *out != NULL || spec == NULL ||
        width < 240u || height < 140u ||
        width > ATM_CHART_RENDER_MAX_SIZE ||
        height > ATM_CHART_RENDER_MAX_SIZE ||
        device_scale < 1u || device_scale > 2u)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Render output, specification, or viewport arguments are invalid.");

    AtmChartSpecV4 *owned_spec = NULL;
    if (!atm_chart_spec_v4_rebuild (spec, &owned_spec, error))
        return FALSE;

    AtmChartRenderV4 *render = g_new0 (AtmChartRenderV4, 1);
    render->owned_spec = owned_spec;
    render->width = width;
    render->height = height;
    render->device_scale = device_scale;

    const AtmChartSpec *source_spec =
        atm_chart_spec_v4_source_spec (owned_spec);
    render->data = atm_chart_render_data_for_spec (source_spec, error);
    if (render->data == NULL ||
        !prepare_surface (render, error) ||
        !build_id (render, &render->id, error) ||
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
