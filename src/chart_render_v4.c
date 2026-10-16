#include "chart_render_v4.h"

#include "scientific_canonical.h"

#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

struct AtmChartRenderV4 {
    AtmChartSpecV4 *owned_spec;
    AtmChartProjectionV4 *projection;
    AtmChartRender *source_render;
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

static gboolean
projection_geometry_equal (const AtmChartProjection *a,
                           const AtmChartProjection *b)
{
    if (a == NULL || b == NULL ||
        g_strcmp0 (atm_chart_projection_policy (a),
                   atm_chart_projection_policy (b)) != 0 ||
        atm_chart_projection_count (a) != atm_chart_projection_count (b) ||
        atm_chart_projection_x_tick_count (a) !=
            atm_chart_projection_x_tick_count (b) ||
        atm_chart_projection_y_tick_count (a) !=
            atm_chart_projection_y_tick_count (b))
        return FALSE;

    const AtmChartDisplayDomain *ad = atm_chart_projection_domain (a);
    const AtmChartDisplayDomain *bd = atm_chart_projection_domain (b);
    if (ad == NULL || bd == NULL ||
        ad->x_min != bd->x_min || ad->x_max != bd->x_max ||
        ad->y_min != bd->y_min || ad->y_max != bd->y_max)
        return FALSE;

    for (guint i = 0; i < atm_chart_projection_count (a); i++) {
        const AtmChartProjectedPoint *ap = atm_chart_projection_point (a, i);
        const AtmChartProjectedPoint *bp = atm_chart_projection_point (b, i);
        if (ap == NULL || bp == NULL ||
            ap->series_index != bp->series_index ||
            ap->point_index != bp->point_index ||
            ap->x != bp->x || ap->y != bp->y)
            return FALSE;
    }

    for (guint i = 0; i < atm_chart_projection_x_tick_count (a); i++) {
        const AtmChartTick *at = atm_chart_projection_x_tick (a, i);
        const AtmChartTick *bt = atm_chart_projection_x_tick (b, i);
        if (at == NULL || bt == NULL || at->position != bt->position ||
            g_strcmp0 (at->label, bt->label) != 0)
            return FALSE;
    }

    for (guint i = 0; i < atm_chart_projection_y_tick_count (a); i++) {
        const AtmChartTick *at = atm_chart_projection_y_tick (a, i);
        const AtmChartTick *bt = atm_chart_projection_y_tick (b, i);
        if (at == NULL || bt == NULL || at->position != bt->position ||
            g_strcmp0 (at->label, bt->label) != 0)
            return FALSE;
    }
    return TRUE;
}

static gboolean
data_identity_matches (const AtmChartSpec *source_spec,
                       const AtmChartRender *source_render,
                       GError **error)
{
    const char *spec_id = atm_chart_spec_id (source_spec);
    const char *rendered_id = atm_chart_spec_id (
        atm_chart_render_spec (source_render));
    const char *data = atm_chart_render_data (source_render);
    if (spec_id == NULL || g_strcmp0 (spec_id, rendered_id) != 0 ||
        data == NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_DATA,
            "The source renderer and its Data view must retain the v4-bound source ChartSpec identity.");

    char *identity_line = g_strdup_printf ("# chart_spec_id\t%s\n", spec_id);
    gboolean matches = strstr (data, identity_line) != NULL;
    g_free (identity_line);
    return matches || reject (error, ATM_CHART_RENDER_V4_ERROR_DATA,
        "The renderer Data view does not identify the v4-bound source ChartSpec.");
}

static gboolean
build_id (const AtmChartRenderV4 *render,
          char **out_id,
          GError **error)
{
    const AtmChartSpecV4 *spec = render->owned_spec;
    const AtmChartSpec *source = atm_chart_spec_v4_source_spec (spec);
    const AtmChartProjection *render_projection =
        atm_chart_render_projection (render->source_render);
    const char *projection_policy = NULL;

    if (!atm_chart_render_is_fallback (render->source_render)) {
        if (render->projection == NULL || render_projection == NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A non-fallback v4 render requires both validated projection objects.");
        projection_policy = atm_chart_projection_v4_policy (render->projection);
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
    json_builder_add_boolean_value (builder,
        atm_chart_render_is_fallback (render->source_render));
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
        render->source_render == NULL || render->width < 240u ||
        render->height < 140u || render->width > ATM_CHART_RENDER_MAX_SIZE ||
        render->height > ATM_CHART_RENDER_MAX_SIZE ||
        render->device_scale < 1u || render->device_scale > 2u ||
        render->id == NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The v4 render wrapper is incomplete or outside the qualified viewport limits.");

    if (!atm_chart_spec_v4_validate (render->owned_spec, error))
        return FALSE;

    const AtmChartSpec *source_spec =
        atm_chart_spec_v4_source_spec (render->owned_spec);
    const AtmChartSpec *rendered_spec =
        atm_chart_render_spec (render->source_render);
    cairo_surface_t *surface = atm_chart_render_surface (render->source_render);
    const char *data = atm_chart_render_data (render->source_render);
    if (source_spec == NULL || rendered_spec == NULL || surface == NULL ||
        data == NULL || cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "The source renderer lacks a valid surface, source specification, or Data view.");

    if (!data_identity_matches (source_spec, render->source_render, error))
        return FALSE;

    const AtmChartProjection *render_projection =
        atm_chart_render_projection (render->source_render);
    if (render_projection != NULL &&
        !atm_chart_projection_validate (render_projection, error))
        return FALSE;

    if (atm_chart_render_is_fallback (render->source_render)) {
        if (render->projection != NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A fallback render cannot claim an active v4 plot projection.");
    } else {
        if (render->projection == NULL || render_projection == NULL)
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "A non-fallback render requires an active source and v4 projection.");

        if (!atm_chart_projection_v4_validate (render->projection, error))
            return FALSE;

        const AtmChartProjection *v4_projection =
            atm_chart_projection_v4_source_projection (render->projection);
        if (!projection_geometry_equal (v4_projection, render_projection))
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "The exact source projection used by Cairo differs from the catalog-bound v4 projection.");

        if (g_strcmp0 (atm_chart_projection_v4_policy (render->projection),
                       atm_chart_projection_policy (render_projection)) != 0 ||
            atm_chart_projection_v4_coordinate_count (render->projection) !=
                atm_chart_spec_v4_coordinate_count (render->owned_spec) ||
            atm_chart_projection_v4_numeric_observation_count (render->projection) !=
                atm_chart_spec_v4_numeric_observation_count (render->owned_spec))
            return reject (error, ATM_CHART_RENDER_V4_ERROR_PROJECTION,
                "The rendered projection policy or coverage differs from the catalog-bound v4 metadata.");
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
    if (out == NULL || *out != NULL)
        return reject (error, ATM_CHART_RENDER_V4_ERROR_ARGUMENT,
            "Render output must be non-NULL and initially empty.");

    AtmChartSpecV4 *owned_spec = NULL;
    if (!atm_chart_spec_v4_rebuild (spec, &owned_spec, error))
        return FALSE;

    AtmChartRender *source_render = NULL;
    if (!atm_chart_render_new (atm_chart_spec_v4_source_spec (owned_spec),
                              width, height, device_scale, &source_render, error)) {
        atm_chart_spec_v4_free (owned_spec);
        return FALSE;
    }

    AtmChartProjectionV4 *projection = NULL;
    if (!atm_chart_render_is_fallback (source_render)) {
        if (!atm_chart_projection_v4_new (owned_spec,
                (double) width - 112.0, (double) height - 188.0,
                &projection, error)) {
            atm_chart_render_free (source_render);
            atm_chart_spec_v4_free (owned_spec);
            return FALSE;
        }
    }

    AtmChartRenderV4 *render = g_new0 (AtmChartRenderV4, 1);
    render->owned_spec = owned_spec;
    render->projection = projection;
    render->source_render = source_render;
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
    atm_chart_render_free (render->source_render);
    g_free (render->id);
    g_free (render);
}

const AtmChartSpecV4 *
atm_chart_render_v4_spec (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->owned_spec : NULL;
}

const AtmChartRender *
atm_chart_render_v4_source_render (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->source_render : NULL;
}

const AtmChartProjectionV4 *
atm_chart_render_v4_projection (const AtmChartRenderV4 *render)
{
    return render != NULL ? render->projection : NULL;
}

cairo_surface_t *
atm_chart_render_v4_surface (const AtmChartRenderV4 *render)
{
    return render != NULL ?
        atm_chart_render_surface (render->source_render) : NULL;
}

const char *
atm_chart_render_v4_data (const AtmChartRenderV4 *render)
{
    return render != NULL ?
        atm_chart_render_data (render->source_render) : NULL;
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
    return render != NULL ?
        atm_chart_render_is_fallback (render->source_render) : FALSE;
}
