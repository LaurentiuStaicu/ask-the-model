#include "chart_projection.h"
#include <json-glib/json-glib.h>
#include <math.h>
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
static AtmChartProjection *project (AtmChartSpec *spec, double width, double height)
{
    AtmChartProjection *p = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_projection_new (spec, width, height, &p, &error));
    g_assert_no_error (error); return p;
}
static void test_reference (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    char *id = g_strdup (atm_chart_spec_id (spec));
    AtmChartProjection *p = project (spec, 580, 160);
    g_assert_true (atm_chart_projection_spec (p) != spec);
    /* Deliberate read-only violation: original input cannot change owned data. */
    ((AtmScientificDecimal *) atm_chart_spec_y_min (spec))->exponent++;
    atm_chart_spec_free (spec);
    g_assert_true (atm_chart_projection_validate (p, NULL));
    g_assert_cmpstr (atm_chart_spec_id (atm_chart_projection_spec (p)), ==, id);
    g_free (id);
    const AtmChartDisplayDomain *d = atm_chart_projection_domain (p);
    g_assert_cmpfloat (d->x_min, ==, 1880); g_assert_cmpfloat (d->x_max, ==, 2025);
    g_assert_cmpfloat (d->y_min, ==, -0.5); g_assert_cmpfloat (d->y_max, ==, 1.5);
    /* Display padding must not overwrite the exact scientific range. */
    g_assert_cmpstr (atm_chart_spec_y_min (atm_chart_projection_spec (p))->coefficient, ==, "-49");
    g_assert_cmpstr (atm_chart_spec_y_max (atm_chart_projection_spec (p))->coefficient, ==, "128");
    char *path = g_build_filename (g_getenv ("ATM_CHART01_FIXTURE"), "projection-reference.json", NULL);
    JsonParser *parser = json_parser_new ();
    g_assert_true (json_parser_load_from_file (parser, path, NULL));
    JsonObject *root = json_node_get_object (json_parser_get_root (parser));
    g_assert_cmpstr (json_object_get_string_member (root, "policy"), ==, ATM_CHART_PROJECTION_POLICY);
    JsonArray *points = json_object_get_array_member (root, "points");
    g_assert_cmpuint (atm_chart_projection_count (p), ==, 146);
    g_assert_cmpuint (json_array_get_length (points), ==, 146);
    for (guint i = 0; i < 146; i++) {
        JsonObject *expected = json_array_get_object_element (points, i);
        const AtmChartProjectedPoint *actual = atm_chart_projection_point (p, i);
        g_assert_cmpuint (actual->series_index, ==, 0); g_assert_cmpuint (actual->point_index, ==, i);
        g_assert_cmpuint (json_object_get_int_member (expected, "year"), ==, 1880 + i);
        g_assert_cmpfloat_with_epsilon (actual->x, g_ascii_strtod (json_object_get_string_member (expected, "x"), NULL), 1e-10);
        g_assert_cmpfloat_with_epsilon (actual->y, g_ascii_strtod (json_object_get_string_member (expected, "y"), NULL), 1e-10);
        if (i > 0) g_assert_cmpfloat (actual->x, >, atm_chart_projection_point (p, i-1)->x);
    }
    const char *xs[] = {"1880", "1900", "1925", "1950", "1975", "2000", "2025"};
    const double xp[] = {0, 80, 180, 280, 380, 480, 580};
    const char *ys[] = {"-0.50", "-0.25", "0.00", "0.25", "0.50", "0.75", "1.00", "1.25", "1.50"};
    g_assert_cmpuint (atm_chart_projection_x_tick_count (p), ==, G_N_ELEMENTS (xs));
    g_assert_cmpuint (atm_chart_projection_y_tick_count (p), ==, G_N_ELEMENTS (ys));
    for (guint i = 0; i < G_N_ELEMENTS (xs); i++) {
        const AtmChartTick *t = atm_chart_projection_x_tick (p, i);
        g_assert_cmpstr (t->label, ==, xs[i]); g_assert_cmpfloat_with_epsilon (t->position, xp[i], 1e-10);
    }
    for (guint i = 0; i < G_N_ELEMENTS (ys); i++) {
        const AtmChartTick *t = atm_chart_projection_y_tick (p, i);
        g_assert_cmpstr (t->label, ==, ys[i]); g_assert_cmpfloat (t->position, ==, 160 - 20 * i);
    }
    g_assert_null (atm_chart_projection_point (p, 146));
    g_assert_null (atm_chart_projection_x_tick (p, 7));
    g_assert_null (atm_chart_projection_y_tick (p, 9));
    g_object_unref (parser); g_free (path); atm_chart_projection_free (p);
}
static void test_resize (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_SCATTER);
    const double sizes[][2] = {{320,160}, {640.5,320.25}, {8192,8192}};
    AtmChartProjection *base = project (spec, 580, 160);
    for (guint k = 0; k < G_N_ELEMENTS (sizes); k++) {
        AtmChartProjection *p = project (spec, sizes[k][0], sizes[k][1]);
        g_assert_cmpint (atm_chart_spec_kind (atm_chart_projection_spec (p)), ==, ATM_CHART_SCATTER);
        g_assert_cmpstr (atm_chart_spec_id (atm_chart_projection_spec (p)), ==, atm_chart_spec_id (spec));
        for (guint i = 0; i < 146; i++) {
            const AtmChartProjectedPoint *a = atm_chart_projection_point (base, i), *b = atm_chart_projection_point (p, i);
            g_assert_true (isfinite (b->x) && isfinite (b->y));
            g_assert_cmpfloat (b->x, >=, 0); g_assert_cmpfloat (b->x, <=, sizes[k][0]);
            g_assert_cmpfloat (b->y, >=, 0); g_assert_cmpfloat (b->y, <=, sizes[k][1]);
            g_assert_cmpfloat_with_epsilon (b->x / sizes[k][0], a->x / 580, 1e-12);
            g_assert_cmpfloat_with_epsilon (b->y / sizes[k][1], a->y / 160, 1e-12);
        }
        atm_chart_projection_free (p);
    }
    atm_chart_projection_free (base); atm_chart_spec_free (spec);
}
static void test_rejections (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    AtmChartProjection *p = NULL;
    const double invalid[][2] = {{NAN,160},{580,NAN},{INFINITY,160},{580,-INFINITY},
        {0,160},{-1,160},{319.99,160},{580,159.99},{8192.01,160},{580,8192.01}};
    for (guint i = 0; i < G_N_ELEMENTS (invalid); i++) {
        g_assert_false (atm_chart_projection_new (spec, invalid[i][0], invalid[i][1], &p, NULL));
        g_assert_null (p);
    }
    g_assert_false (atm_chart_projection_new (NULL, 580, 160, &p, NULL));
    g_assert_false (atm_chart_projection_new (spec, 580, 160, NULL, NULL));
    p = project (spec, 580, 160);
    AtmChartProjection *saved = p;
    g_assert_false (atm_chart_projection_new (spec, 580, 160, &p, NULL));
    g_assert_true (p == saved); atm_chart_projection_free (p); p = NULL;
    ((AtmScientificDecimal *) atm_chart_spec_y_max (spec))->coefficient[0] = '2';
    g_assert_false (atm_chart_projection_new (spec, 580, 160, &p, NULL));
    g_assert_null (p); atm_chart_spec_free (spec);
    g_assert_false (atm_chart_projection_validate (NULL, NULL));
    g_assert_null (atm_chart_projection_spec (NULL)); g_assert_null (atm_chart_projection_domain (NULL));
    g_assert_null (atm_chart_projection_point (NULL, 0));
    g_assert_null (atm_chart_projection_x_tick (NULL, 0)); g_assert_null (atm_chart_projection_y_tick (NULL, 0));
    g_assert_cmpuint (atm_chart_projection_count (NULL), ==, 0);
    g_assert_cmpuint (atm_chart_projection_x_tick_count (NULL), ==, 0);
    g_assert_cmpuint (atm_chart_projection_y_tick_count (NULL), ==, 0);
    atm_chart_projection_free (NULL);
}
static void test_tampering (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    AtmChartProjection *p = project (spec, 580, 160);
    atm_chart_spec_free (spec);
    AtmChartProjectedPoint *point = (AtmChartProjectedPoint *) atm_chart_projection_point (p, 0);
    double old = point->y; point->y = NAN;
    g_assert_false (atm_chart_projection_validate (p, NULL)); point->y = old;
    point->point_index++;
    g_assert_false (atm_chart_projection_validate (p, NULL)); point->point_index--;
    AtmChartDisplayDomain *d = (AtmChartDisplayDomain *) atm_chart_projection_domain (p);
    d->y_max += 1;
    g_assert_false (atm_chart_projection_validate (p, NULL)); d->y_max -= 1;
    AtmChartTick *tick = (AtmChartTick *) atm_chart_projection_y_tick (p, 0);
    tick->position++;
    g_assert_false (atm_chart_projection_validate (p, NULL)); tick->position--;
    char label[32]; memcpy (label, tick->label, sizeof label);
    memset (tick->label, 'x', sizeof tick->label); /* no NUL: bounded comparison */
    g_assert_false (atm_chart_projection_validate (p, NULL)); memcpy (tick->label, label, sizeof label);
    const AtmVerifiedSeries *series = atm_chart_spec_series (atm_chart_projection_spec (p), 0);
    AtmVerifiedPoint *source = (AtmVerifiedPoint *) atm_verified_series_point (series, 0);
    source->y_status = ATM_VERIFIED_Y_MISSING;
    g_assert_false (atm_chart_projection_validate (p, NULL)); source->y_status = ATM_VERIFIED_Y_NUMERIC;
    g_assert_true (atm_chart_projection_validate (p, NULL));
    atm_chart_projection_free (p);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-projection/reference-ownership", test_reference);
    g_test_add_func ("/chart-projection/resize", test_resize);
    g_test_add_func ("/chart-projection/rejections", test_rejections);
    g_test_add_func ("/chart-projection/tampering", test_tampering);
    return g_test_run ();
}
