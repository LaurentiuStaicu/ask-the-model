#include "chart_spec.h"
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

static void test_extents_ownership (void)
{
    AtmVerifiedSeries *source = build ();
    const AtmVerifiedSeries *inputs[] = {source};
    AtmChartSpec *line = NULL, *again = NULL, *scatter = NULL;
    g_assert_true (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &line, NULL));
    g_assert_true (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &again, NULL));
    g_assert_true (atm_chart_spec_new (ATM_CHART_SCATTER, inputs, 1, &scatter, NULL));
    g_assert_cmpstr (atm_chart_spec_id (line), ==, atm_chart_spec_id (again));
    g_assert_cmpstr (atm_chart_spec_id (line), !=, atm_chart_spec_id (scatter));
    g_assert_cmpuint (atm_chart_spec_count (line), ==, 1);
    g_assert_cmpint (atm_chart_spec_kind (scatter), ==, ATM_CHART_SCATTER);
    g_assert_cmpuint (atm_chart_spec_x_min (line), ==, 1880);
    g_assert_cmpuint (atm_chart_spec_x_max (line), ==, 2025);
    g_assert_cmpstr (atm_chart_spec_y_min (line)->coefficient, ==, "-49");
    g_assert_cmpint (atm_chart_spec_y_min (line)->exponent, ==, -2);
    g_assert_cmpstr (atm_chart_spec_y_max (line)->coefficient, ==, "128");
    g_assert_cmpint (atm_chart_spec_y_max (line)->exponent, ==, -2);
    g_assert_true (atm_chart_spec_series (line, 0) != source);
    ((AtmVerifiedPoint *) atm_verified_series_point (source, 0))->exponent = 99;
    g_assert_true (atm_chart_spec_validate (line, NULL));
    atm_verified_series_free (source);
    g_assert_true (atm_chart_spec_validate (scatter, NULL));
    g_assert_null (atm_chart_spec_series (line, 1));
    atm_chart_spec_free (line); atm_chart_spec_free (again); atm_chart_spec_free (scatter);
}
static void test_rejections (void)
{
    AtmVerifiedSeries *source = build (), *duplicate = build ();
    const AtmVerifiedSeries *inputs[] = {source, duplicate, source, source};
    AtmChartSpec *s = NULL;
    for (guint count = 0; count <= 5; count++) {
        if (count == 1) continue;
        g_assert_false (atm_chart_spec_new (ATM_CHART_LINE, inputs, count, &s, NULL));
        g_assert_null (s);
    }
    AtmChartKind kinds[] = {0, ATM_CHART_STEP, ATM_CHART_BAR, 99};
    for (guint i = 0; i < G_N_ELEMENTS (kinds); i++)
        g_assert_false (atm_chart_spec_new (kinds[i], inputs, 1, &s, NULL));
    g_assert_false (atm_chart_spec_new (ATM_CHART_LINE, NULL, 1, &s, NULL));
    g_assert_false (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, NULL, NULL));
    const AtmVerifiedSeries *missing[] = {NULL};
    g_assert_false (atm_chart_spec_new (ATM_CHART_LINE, missing, 1, &s, NULL));
    g_assert_true (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &s, NULL));
    AtmChartSpec *saved = s;
    g_assert_false (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &s, NULL));
    g_assert_true (s == saved);
    atm_chart_spec_free (s); s = NULL;
    ((AtmVerifiedPoint *) atm_verified_series_point (source, 0))->coefficient[2] = '8';
    g_assert_false (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &s, NULL));
    g_assert_null (s);
    g_assert_false (atm_chart_spec_validate (NULL, NULL));
    g_assert_null (atm_chart_spec_y_min (NULL));
    g_assert_null (atm_chart_spec_y_max (NULL));
    g_assert_null (atm_chart_spec_id (NULL));
    g_assert_null (atm_chart_spec_series (NULL, 0));
    g_assert_cmpuint (atm_chart_spec_count (NULL), ==, 0);
    atm_chart_spec_free (NULL);
    atm_verified_series_free (source); atm_verified_series_free (duplicate);
}
static void test_tampering (void)
{
    AtmVerifiedSeries *source = build ();
    const AtmVerifiedSeries *inputs[] = {source};
    AtmChartSpec *s = NULL;
    g_assert_true (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &s, NULL));
    atm_verified_series_free (source);
    AtmScientificDecimal *bound = (AtmScientificDecimal *) atm_chart_spec_y_min (s);
    bound->exponent++;
    g_assert_false (atm_chart_spec_validate (s, NULL)); bound->exponent--;
    bound->coefficient[2] = '8';
    g_assert_false (atm_chart_spec_validate (s, NULL)); bound->coefficient[2] = '9';
    char *id = (char *) atm_chart_spec_id (s); char saved = id[0];
    id[0] = saved == 'a' ? 'b' : 'a';
    g_assert_false (atm_chart_spec_validate (s, NULL)); id[0] = saved;
    AtmVerifiedPoint *p = (AtmVerifiedPoint *) atm_verified_series_point (atm_chart_spec_series (s, 0), 0);
    p->break_before = TRUE;
    g_assert_false (atm_chart_spec_validate (s, NULL)); p->break_before = FALSE;
    g_assert_true (atm_chart_spec_validate (s, NULL));
    atm_chart_spec_free (s);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-spec/extents-ownership", test_extents_ownership);
    g_test_add_func ("/chart-spec/rejections", test_rejections);
    g_test_add_func ("/chart-spec/tampering", test_tampering);
    return g_test_run ();
}
