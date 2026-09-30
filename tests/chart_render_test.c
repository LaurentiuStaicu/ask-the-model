#include "chart_render.h"
#include <fontconfig/fontconfig.h>
#include <stdint.h>
#include <string.h>
static AtmGistempAdmission *admit (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json", "science/data/registry.csv"
    };
    GBytes *sources[4];
    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (root, "ewd", paths[i], NULL);
        char *bytes; gsize length;
        g_assert_true (g_file_get_contents (path, &bytes, &length, NULL));
        sources[i] = g_bytes_new_take (bytes, length);
        g_free (path);
    }
    AtmGistempAdmission *a = NULL;
    g_assert_true (atm_gistemp_admission_new ("LaurentiuStaicu/empirical-world3-dynamics",
        "d9e249339663015f6d1c05752338a955bf64ad0b", sources, &a, NULL));
    for (guint i = 0; i < 4; i++) g_bytes_unref (sources[i]);
    return a;
}
static AtmVerifiedSeries *build (void)
{
    AtmGistempAdmission *a = admit ();
    AtmVerifiedSeries *s = NULL;
    GError *error = NULL;
    g_assert_true (atm_verified_series_from_gistemp (a, &s, &error));
    g_assert_no_error (error);
    atm_gistemp_admission_free (a);
    return s;
}


static AtmChartSpec *specification (AtmChartKind kind)
{
    AtmVerifiedSeries *s = build ();
    const AtmVerifiedSeries *inputs[] = {s};
    AtmChartSpec *spec = NULL;
    g_assert_true (atm_chart_spec_new (kind, inputs, 1, &spec, NULL));
    atm_verified_series_free (s); return spec;
}

static AtmChartRender *render (AtmChartSpec *spec, guint width, guint height, guint scale)
{
    AtmChartRender *r = NULL; GError *error = NULL;
    g_assert_true (atm_chart_render_new (spec, width, height, scale, &r, &error));
    g_assert_no_error (error); return r;
}
static char *pixel_digest (AtmChartRender *r)
{
    cairo_surface_t *surface = atm_chart_render_surface (r); cairo_surface_flush (surface);
    return g_compute_checksum_for_data (G_CHECKSUM_SHA256, cairo_image_surface_get_data (surface),
        cairo_image_surface_get_stride (surface) * cairo_image_surface_get_height (surface));
}
static void monochrome (AtmChartRender *r)
{
    cairo_surface_t *s = atm_chart_render_surface (r); cairo_surface_flush (s);
    unsigned char *bytes = cairo_image_surface_get_data (s);
    int stride = cairo_image_surface_get_stride (s); guint black = 0, white = 0;
    for (int y = 0; y < cairo_image_surface_get_height (s); y++) {
        const uint32_t *row = (const uint32_t *) (bytes + y * stride);
        for (int x = 0; x < cairo_image_surface_get_width (s); x++) {
            g_assert_true (row[x] == 0xff000000u || row[x] == 0xffffffffu);
            if (row[x] == 0xff000000u) black++; else white++;
        }
    }
    g_assert_cmpuint (black, >, 500); g_assert_cmpuint (white, >, black);
}
static void save_preview (AtmChartRender *r, const char *name, gboolean data)
{
    const char *directory = g_getenv ("ATM_CHART03_OUTPUT"); if (!directory) return;
    g_assert_cmpint (g_mkdir_with_parents (directory, 0700), ==, 0);
    char *path = g_build_filename (directory, name, NULL);
    g_assert_cmpint (cairo_surface_write_to_png (atm_chart_render_surface (r), path), ==, CAIRO_STATUS_SUCCESS);
    g_free (path);
    if (data) {
        path = g_build_filename (directory, "atm-chart-data.tsv", NULL);
        g_assert_true (g_file_set_contents (path, atm_chart_render_data (r), -1, NULL)); g_free (path);
    }
}
static void test_line_data_ownership (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    AtmChartRender *a = render (spec, 860, 500, 1), *b = render (spec, 860, 500, 1);
    g_assert_false (atm_chart_render_is_fallback (a));
    g_assert_true (atm_chart_render_spec (a) != spec);
    char *da = pixel_digest (a), *db = pixel_digest (b);
    g_assert_cmpstr (da, ==, db); g_free (db); atm_chart_render_free (b);
    ((AtmScientificDecimal *) atm_chart_spec_y_min (spec))->exponent++;
    atm_chart_spec_free (spec);
    db = pixel_digest (a); g_assert_cmpstr (da, ==, db); g_free (da); g_free (db);
    g_assert_true (atm_chart_spec_validate (atm_chart_render_spec (a), NULL));
    monochrome (a);
    char **lines = g_strsplit (atm_chart_render_data (a), "\n", -1);
    guint count = 0;
    const AtmVerifiedSeries *series = atm_chart_spec_series (atm_chart_render_spec (a), 0);
    for (guint i = 0; lines[i]; i++) {
        if (lines[i][0] == '#' || lines[i][0] == '\0' || g_str_has_prefix (lines[i], "series\t")) continue;
        char **cells = g_strsplit (lines[i], "\t", -1);
        const AtmVerifiedPoint *p = atm_verified_series_point (series, count);
        g_assert_nonnull (p); g_assert_cmpuint (g_strv_length (cells), ==, 8);
        g_assert_cmpstr (cells[0], ==, "0");
        g_assert_cmpuint (g_ascii_strtoull (cells[1], NULL, 10), ==, p->calendar_year);
        g_assert_cmpstr (cells[2], ==, p->source_decimal); g_assert_cmpstr (cells[3], ==, p->coefficient);
        g_assert_cmpint (g_ascii_strtoll (cells[4], NULL, 10), ==, p->exponent);
        g_assert_cmpuint (g_ascii_strtoull (cells[5], NULL, 10), ==, p->source_row);
        g_assert_cmpstr (cells[6], ==, atm_verified_series_qualified_id (series));
        char **support = g_strsplit (cells[7], ";", -1); g_assert_cmpuint (g_strv_length (support), ==, 5);
        for (guint j = 0; j < 5; j++) g_assert_cmpstr (support[j], ==, g_ptr_array_index (p->support, j));
        g_strfreev (support); g_strfreev (cells); count++;
    }
    g_assert_cmpuint (count, ==, 146); g_strfreev (lines);
    g_assert_nonnull (strstr (atm_chart_render_data (a), "# unit\tdegree_Celsius_anomaly\n"));
    g_assert_nonnull (strstr (atm_chart_render_data (a), "# reference_period\t1951-1980\n"));
    save_preview (a, "atm-chart-line.png", TRUE); atm_chart_render_free (a);
}
static void test_scatter_scale (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_SCATTER);
    AtmChartRender *a = render (spec, 860, 500, 1), *b = render (spec, 860, 500, 2);
    g_assert_false (atm_chart_render_is_fallback (a)); g_assert_false (atm_chart_render_is_fallback (b));
    g_assert_cmpint (cairo_image_surface_get_width (atm_chart_render_surface (b)), ==, 1720);
    g_assert_cmpint (cairo_image_surface_get_height (atm_chart_render_surface (b)), ==, 1000);
    double dx, dy; cairo_surface_get_device_scale (atm_chart_render_surface (b), &dx, &dy);
    g_assert_cmpfloat (dx, ==, 2); g_assert_cmpfloat (dy, ==, 2);
    g_assert_cmpstr (atm_chart_render_data (a), ==, atm_chart_render_data (b));
    monochrome (a); monochrome (b);
    AtmChartProjection *p = NULL;
    g_assert_true (atm_chart_projection_new (spec, 748, 312, &p, NULL));
    cairo_surface_t *s = atm_chart_render_surface (a); cairo_surface_flush (s);
    unsigned char *bytes = cairo_image_surface_get_data (s); int stride = cairo_image_surface_get_stride (s);
    /* Every admitted observation has visible marker ink, including endpoints. */
    for (guint i = 0; i < atm_chart_projection_count (p); i++) {
        const AtmChartProjectedPoint *point = atm_chart_projection_point (p, i);
        int cx = (int) (84 + point->x), cy = (int) (108 + point->y); gboolean ink = FALSE;
        for (int y = cy - 2; y <= cy + 2; y++) for (int x = cx - 2; x <= cx + 2; x++)
            if (((uint32_t *) (bytes + y * stride))[x] == 0xff000000u) ink = TRUE;
        g_assert_true (ink);
    }
    atm_chart_projection_free (p);
    save_preview (a, "atm-chart-scatter.png", FALSE);
    atm_chart_render_free (a); atm_chart_render_free (b); atm_chart_spec_free (spec);
}
static void test_resize_transactional (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    AtmChartRender *r = render (spec, 860, 500, 1);
    atm_chart_spec_free (spec);

    const AtmChartSpec *owned = atm_chart_render_spec (r);
    char *owned_id = g_strdup (atm_chart_spec_id (owned));
    char *data = g_strdup (atm_chart_render_data (r));
    char *before = pixel_digest (r);

    GError *error = NULL;
    g_assert_true (atm_chart_render_resize (r, 600, 360, 2, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_render_spec (r) == owned);
    g_assert_cmpstr (atm_chart_spec_id (atm_chart_render_spec (r)), ==, owned_id);
    g_assert_cmpstr (atm_chart_render_data (r), ==, data);
    g_assert_false (atm_chart_render_is_fallback (r));
    cairo_surface_t *surface = atm_chart_render_surface (r);
    g_assert_cmpint (cairo_image_surface_get_width (surface), ==, 1200);
    g_assert_cmpint (cairo_image_surface_get_height (surface), ==, 720);
    double dx = 0, dy = 0; cairo_surface_get_device_scale (surface, &dx, &dy);
    g_assert_cmpfloat (dx, ==, 2); g_assert_cmpfloat (dy, ==, 2);

    char *resized = pixel_digest (r);
    g_assert_cmpstr (before, !=, resized);
    g_free (before);

    g_assert_false (atm_chart_render_resize (r, 239, 360, 2, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_clear_error (&error);
    char *after_invalid = pixel_digest (r);
    g_assert_cmpstr (after_invalid, ==, resized);
    g_assert_true (atm_chart_render_spec (r) == owned);
    g_assert_cmpstr (atm_chart_render_data (r), ==, data);
    g_free (after_invalid); g_free (resized);

    g_assert_true (atm_chart_render_resize (r, 240, 140, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_render_is_fallback (r));
    g_assert_true (atm_chart_render_spec (r) == owned);
    g_assert_cmpstr (atm_chart_render_data (r), ==, data);

    g_assert_false (atm_chart_render_resize (NULL, 860, 500, 1, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_clear_error (&error);

    g_free (owned_id); g_free (data); atm_chart_render_free (r);
}

static void test_fallback_arguments (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    AtmChartRender *small = render (spec, 240, 140, 1), *large = render (spec, 860, 500, 1);
    g_assert_true (atm_chart_render_is_fallback (small));
    AtmChartRender *minimum = render (spec, 432, 348, 1);
    g_assert_false (atm_chart_render_is_fallback (minimum));
    monochrome (minimum); atm_chart_render_free (minimum);
    g_assert_cmpstr (atm_chart_render_data (small), ==, atm_chart_render_data (large));
    monochrome (small); save_preview (small, "atm-chart-small.png", FALSE);
    const guint invalid[][3] = {{239,500,1},{860,139,1},{2049,500,1},{860,2049,1},{860,500,0},{860,500,3},{G_MAXUINT,500,2}};
    AtmChartRender *out = NULL;
    for (guint i = 0; i < G_N_ELEMENTS (invalid); i++) {
        g_assert_false (atm_chart_render_new (spec, invalid[i][0], invalid[i][1], invalid[i][2], &out, NULL));
        g_assert_null (out);
    }
    g_assert_false (atm_chart_render_new (spec, 860, 500, 1, NULL, NULL));
    g_assert_false (atm_chart_render_new (NULL, 860, 500, 1, &out, NULL));
    out = large;
    g_assert_false (atm_chart_render_new (spec, 860, 500, 1, &out, NULL)); g_assert_true (out == large); out = NULL;
    ((AtmScientificDecimal *) atm_chart_spec_y_min (spec))->exponent++;
    g_assert_false (atm_chart_render_new (spec, 240, 140, 1, &out, NULL)); g_assert_null (out);
    g_assert_null (atm_chart_render_surface (NULL)); g_assert_null (atm_chart_render_data (NULL));
    g_assert_null (atm_chart_render_spec (NULL)); atm_chart_render_free (NULL);
    atm_chart_render_free (small); atm_chart_render_free (large); atm_chart_spec_free (spec);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-render/line-data-ownership", test_line_data_ownership);
    g_test_add_func ("/chart-render/scatter-scale", test_scatter_scale);
    g_test_add_func ("/chart-render/resize-transactional", test_resize_transactional);
    g_test_add_func ("/chart-render/fallback-arguments", test_fallback_arguments);
    int result = g_test_run ();
    /* Release process-wide Cairo/Fontconfig test caches after every object is
     * destroyed, so leak checking remains enabled without suppressions. */
    cairo_debug_reset_static_data (); FcFini ();
    return result;
}
