#include "chart_projection.h"
#include "chart_projection_v4.h"
#include "chart_projection_internal.h"
#include "energy_institute_verified_series.h"
#include "energy_institute_admission.h"
#include "bnr_bls_verified_series.h"
#include "bnr_bls_series_contract.h"
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

static AtmChartSpec *
ei_specification (AtmChartKind kind, const char *snapshot)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    gboolean current = g_strcmp0 (snapshot,
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb") == 0;
    const char *bundle = current ? "ewd-current" : "ewd";
    const char *paths[] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[ATM_EI_SOURCE_COUNT] = {NULL};
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, bundle, paths[i], NULL);
        char *bytes = NULL;
        gsize length = 0;
        GError *error = NULL;
        g_assert_true (g_file_get_contents (path, &bytes, &length, &error));
        g_assert_no_error (error);
        g_free (path);
        sources[i] = g_bytes_new_take (bytes, length);
    }

    AtmEnergyInstituteAdmission *admission = NULL;
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    GError *error = NULL;
    g_assert_true (atm_energy_institute_admission_new (
        "LaurentiuStaicu/empirical-world3-dynamics", snapshot, sources,
        &admission, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);
    g_assert_true (atm_energy_institute_verified_series_from_admission (
        admission, &series, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_energy_institute_v2 (
        kind, series, &spec, &error));
    g_assert_no_error (error);
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
    return spec;
}

static AtmBnrBlsVerifiedSeries *
build_bnr_verified_series (void)
{
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);
    const char *names[] = {
        "bnr_bls_realised_rounds.csv",
        "processed_data_inventory_registry.json",
        "bnr_bls_canonical_panel_promotion_review.json",
        "bnr_bls_cross_round_mapping_audit.json",
        "bnr_bls_missing_round_semantic_mapping_audit.json"
    };
    GBytes *sources[ATM_BNR_BLS_SOURCE_COUNT] = {NULL};
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, names[i], NULL);
        char *contents = NULL;
        gsize length = 0;
        GError *error = NULL;
        g_assert_true (g_file_get_contents (path, &contents, &length, &error));
        g_assert_no_error (error);
        g_free (path);
        sources[i] = g_bytes_new_take (contents, length);
    }

    AtmBnrBlsPanel *panel = NULL;
    AtmBnrBlsSra *sra = NULL;
    AtmBnrBlsSeriesContract *contract = NULL;
    AtmBnrBlsVerifiedSeries *series = NULL;
    GError *error = NULL;
    g_assert_true (atm_bnr_bls_panel_new (
        ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        ATM_BNR_BLS_SNAPSHOT_SHA, sources, &panel, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);
    g_assert_true (atm_bnr_bls_sra_new (panel, &sra, &error));
    g_assert_no_error (error);
    g_assert_true (atm_bnr_bls_series_contract_from_sra (
        panel, sra, &contract, &error));
    g_assert_no_error (error);
    g_assert_true (atm_bnr_bls_verified_series_from_contract (
        contract, &series, &error));
    g_assert_no_error (error);
    atm_bnr_bls_series_contract_free (contract);
    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
    return series;
}

static AtmChartSpec *
bnr_specification (AtmChartKind kind)
{
    AtmBnrBlsVerifiedSeries *series = build_bnr_verified_series ();
    AtmChartSpec *spec = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_spec_new_bnr_bls_c01_v3 (kind, series, &spec, &error));
    g_assert_no_error (error);
    atm_bnr_bls_verified_series_free (series);
    return spec;
}

static void
test_bnr_quarterly_gap_policy (void)
{
    static const guint expected_indices[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 11u};
    static const char *const expected_x_labels[] = {
        "Q4 2022", "Q2 2023", "Q4 2023", "Q2 2024", "Q4 2024", "Q3 2025"
    };
    static const char *const expected_y_labels[] = {"20", "15", "10", "5", "0"};

    AtmChartSpec *spec = bnr_specification (ATM_CHART_LINE);
    AtmChartProjection *projection = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_projection_new (spec, 360.0, 200.0, &projection, &error));
    g_assert_no_error (error);
    g_assert_nonnull (projection);
    g_assert_true (atm_chart_projection_validate (projection, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_chart_projection_policy (projection), ==,
                     ATM_CHART_PROJECTION_BNR_POLICY);
    const AtmChartDisplayDomain *domain = atm_chart_projection_domain (projection);
    g_assert_nonnull (domain);
    g_assert_cmpfloat (domain->x_min, ==, 0.0);
    g_assert_cmpfloat (domain->x_max, ==, 11.0);
    g_assert_cmpfloat (domain->y_min, ==, 0.0);
    g_assert_cmpfloat (domain->y_max, ==, 20.0);
    g_assert_cmpuint (atm_chart_projection_count (projection), ==,
                      G_N_ELEMENTS (expected_indices));
    g_assert_cmpuint (atm_chart_projection_x_tick_count (projection), ==,
                      G_N_ELEMENTS (expected_x_labels));
    g_assert_cmpuint (atm_chart_projection_y_tick_count (projection), ==,
                      G_N_ELEMENTS (expected_y_labels));

    for (guint i = 0; i < G_N_ELEMENTS (expected_indices); i++) {
        const AtmChartProjectedPoint *point = atm_chart_projection_point (projection, i);
        g_assert_nonnull (point);
        g_assert_cmpuint (point->series_index, ==, 0u);
        g_assert_cmpuint (point->point_index, ==, expected_indices[i]);
        g_assert_true (isfinite (point->x) && isfinite (point->y));
        g_assert_cmpfloat_with_epsilon (point->x,
            (double) expected_indices[i] / 11.0 * 360.0, 1e-12);
        g_assert_cmpfloat (point->x, >=, 0.0);
        g_assert_cmpfloat (point->x, <=, 360.0);
        g_assert_cmpfloat (point->y, >=, 0.0);
        g_assert_cmpfloat (point->y, <=, 200.0);
        g_assert_cmpuint (point->point_index, !=, 10u);
    }

    for (guint i = 0; i < G_N_ELEMENTS (expected_x_labels); i++) {
        const AtmChartTick *tick = atm_chart_projection_x_tick (projection, i);
        g_assert_nonnull (tick);
        g_assert_cmpstr (tick->label, ==, expected_x_labels[i]);
    }
    for (guint i = 0; i < G_N_ELEMENTS (expected_y_labels); i++) {
        const AtmChartTick *tick = atm_chart_projection_y_tick (projection, i);
        g_assert_nonnull (tick);
        g_assert_cmpstr (tick->label, ==, expected_y_labels[i]);
    }

    const AtmChartProjectedPoint *before_gap = atm_chart_projection_point (projection, 9u);
    const AtmChartProjectedPoint *after_gap = atm_chart_projection_point (projection, 10u);
    g_assert_cmpuint (before_gap->point_index, ==, 9u);
    g_assert_cmpuint (after_gap->point_index, ==, 11u);
    g_assert_cmpfloat_with_epsilon (after_gap->x - before_gap->x,
                                    2.0 * 360.0 / 11.0, 1e-12);

    AtmChartProjectedPoint *mutable_point =
        (AtmChartProjectedPoint *) atm_chart_projection_point (projection, 10u);
    guint saved_index = mutable_point->point_index;
    mutable_point->point_index = 10u;
    g_assert_false (atm_chart_projection_validate (projection, NULL));
    mutable_point->point_index = saved_index;
    g_assert_true (atm_chart_projection_validate (projection, &error));
    g_assert_no_error (error);

    AtmChartSpec *scatter_spec = bnr_specification (ATM_CHART_SCATTER);
    AtmChartProjection *scatter = NULL;
    g_assert_true (atm_chart_projection_new (scatter_spec, 360.0, 200.0, &scatter, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (atm_chart_projection_count (scatter), ==, 11u);
    g_assert_true (atm_chart_projection_validate (scatter, &error));
    g_assert_no_error (error);

    atm_chart_projection_free (scatter);
    atm_chart_spec_free (scatter_spec);
    atm_chart_projection_free (projection);
    atm_chart_spec_free (spec);
}

static void
test_ei_decimal_conversion (void)
{
    gint64 value = -77;
    GError *error = NULL;
    g_assert_true (_atm_chart_projection_ei_decimal_micro ("1496960972", -7,
        &value, &error));
    g_assert_no_error (error);
    g_assert_cmpint (value, ==, 149696097);
    g_assert_true (_atm_chart_projection_ei_decimal_micro ("6003129128", -7,
        &value, &error));
    g_assert_no_error (error);
    g_assert_cmpint (value, ==, 600312913);
    g_assert_true (_atm_chart_projection_ei_decimal_micro ("100", 0,
        &value, &error));
    g_assert_no_error (error);
    g_assert_cmpint (value, ==, 100000000);

    struct {
        const char *coefficient;
        gint64 exponent;
    } invalid[] = {
        {NULL, -7}, {"", -7}, {"-12", -7}, {"+12", -7}, {"1.2", -7},
        {"01", -7}, {"12", -8}, {"1", 1}, {"99999999999999999999", 0},
        {"0", -7}, {"10000000001", -7}, {"1001", 0}
    };
    for (guint i = 0; i < G_N_ELEMENTS (invalid); i++) {
        value = -77;
        g_assert_false (_atm_chart_projection_ei_decimal_micro (
            invalid[i].coefficient, invalid[i].exponent, &value, NULL));
        g_assert_cmpint (value, ==, -77);
    }
    g_assert_false (_atm_chart_projection_ei_decimal_micro ("1", -7, NULL, NULL));
}

static void
test_ei_projection_both_snapshots_and_resize (void)
{
    const char *old_snapshot = "d9e249339663015f6d1c05752338a955bf64ad0b";
    const char *current_snapshot = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    AtmChartSpec *old_spec = ei_specification (ATM_CHART_LINE, old_snapshot);
    AtmChartSpec *current_spec = ei_specification (ATM_CHART_LINE, current_snapshot);
    AtmChartProjection *old_projection = NULL, *base = NULL;
    GError *error = NULL;

    g_assert_cmpstr (ATM_CHART_SPEC_V2_EI_DISPLAY_PROFILE_VERSION, ==, "1");
    g_assert_cmpstr (atm_chart_spec_id (old_spec), !=, atm_chart_spec_id (current_spec));
    g_assert_true (atm_chart_projection_new (old_spec, 580, 320, &old_projection, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_projection_new (current_spec, 580, 320, &base, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_chart_projection_policy (base), ==, ATM_CHART_PROJECTION_EI_POLICY);
    g_assert_cmpuint (atm_chart_projection_count (base), ==, 61u);
    g_assert_cmpuint (atm_chart_projection_x_tick_count (base), ==, 7u);
    g_assert_cmpuint (atm_chart_projection_y_tick_count (base), ==, 7u);

    const AtmChartDisplayDomain *domain = atm_chart_projection_domain (base);
    g_assert_cmpfloat (domain->x_min, ==, 1965.0);
    g_assert_cmpfloat (domain->x_max, ==, 2025.0);
    g_assert_cmpfloat (domain->y_min, ==, 100.0);
    g_assert_cmpfloat (domain->y_max, ==, 700.0);
    const char *labels[] = {"100", "200", "300", "400", "500", "600", "700"};
    for (guint i = 0; i < G_N_ELEMENTS (labels); i++) {
        const AtmChartTick *tick = atm_chart_projection_y_tick (base, i);
        g_assert_cmpstr (tick->label, ==, labels[i]);
        g_assert_cmpfloat_with_epsilon (tick->position, 320.0 - (320.0 / 6.0) * i, 1e-12);
    }
    const guint expected_years[] = {1965, 1975, 1985, 1995, 2005, 2015, 2025};
    for (guint i = 0; i < G_N_ELEMENTS (expected_years); i++)
        g_assert_cmpuint (g_ascii_strtoull (
            atm_chart_projection_x_tick (base, i)->label, NULL, 10), ==,
            expected_years[i]);

    AtmChartPointView first = {0}, last = {0};
    g_assert_true (atm_chart_spec_point (
        atm_chart_projection_spec (base), 0, 0, &first));
    g_assert_true (atm_chart_spec_point (
        atm_chart_projection_spec (base), 0, 60, &last));
    g_assert_cmpstr (first.source_decimal, ==, "149.6960972");
    g_assert_cmpstr (last.source_decimal, ==, "600.3129128");
    g_assert_cmpuint (first.support->len, ==, 5u);
    g_assert_cmpuint (last.support->len, ==, 5u);

    for (guint size_case = 0; size_case < 4; size_case++) {
        const double widths[] = {320.0, 480.0, 720.0, 8192.0};
        const double heights[] = {160.0, 240.0, 360.0, 8192.0};
        AtmChartProjection *p = NULL;
        g_assert_true (atm_chart_projection_new (current_spec, widths[size_case],
            heights[size_case], &p, &error));
        g_assert_no_error (error);
        g_assert_true (atm_chart_projection_validate (p, &error));
        g_assert_no_error (error);
        guint expected_ticks = size_case == 0 ? 5u : (size_case == 1 ? 7u : 13u);
        g_assert_cmpuint (atm_chart_projection_x_tick_count (p), ==, expected_ticks);
        g_assert_cmpuint (atm_chart_projection_count (p), ==, 61u);
        for (guint i = 0; i < 61; i++) {
            const AtmChartProjectedPoint *a = atm_chart_projection_point (base, i);
            const AtmChartProjectedPoint *b = atm_chart_projection_point (p, i);
            g_assert_true (isfinite (b->x) && isfinite (b->y));
            g_assert_cmpfloat (b->x, >=, 0.0);
            g_assert_cmpfloat (b->x, <=, widths[size_case]);
            g_assert_cmpfloat (b->y, >=, 0.0);
            g_assert_cmpfloat (b->y, <=, heights[size_case]);
            g_assert_cmpfloat_with_epsilon (b->x / widths[size_case],
                a->x / 580.0, 1e-12);
            g_assert_cmpfloat_with_epsilon (b->y / heights[size_case],
                a->y / 320.0, 1e-12);
        }
        atm_chart_projection_free (p);
    }

    for (guint i = 0; i < 61; i++) {
        const AtmChartProjectedPoint *a = atm_chart_projection_point (old_projection, i);
        const AtmChartProjectedPoint *b = atm_chart_projection_point (base, i);
        g_assert_cmpfloat (a->x, ==, b->x);
        g_assert_cmpfloat (a->y, ==, b->y);
        g_assert_cmpuint (a->point_index, ==, b->point_index);
    }

    AtmChartProjection *saved = base;
    g_assert_false (atm_chart_projection_new (current_spec, 319.0, 320.0,
        &base, NULL));
    g_assert_true (base == saved);
    atm_chart_projection_free (old_projection);
    atm_chart_projection_free (base);
    atm_chart_spec_free (old_spec);
    atm_chart_spec_free (current_spec);
}

static void
test_ei_projection_tampering (void)
{
    AtmChartSpec *spec = ei_specification (ATM_CHART_LINE,
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb");
    AtmChartProjection *p = NULL;
    g_assert_true (atm_chart_projection_new (spec, 580.0, 320.0, &p, NULL));
    atm_chart_spec_free (spec);

    AtmChartProjectedPoint *point =
        (AtmChartProjectedPoint *) atm_chart_projection_point (p, 0);
    double old_y = point->y;
    point->y = NAN;
    g_assert_false (atm_chart_projection_validate (p, NULL));
    point->y = old_y;
    g_assert_true (atm_chart_projection_validate (p, NULL));

    AtmChartTick *tick = (AtmChartTick *)
        atm_chart_projection_y_tick (p, 0);
    tick->label[0] = 'x';
    g_assert_false (atm_chart_projection_validate (p, NULL));
    tick->label[0] = '1';
    g_assert_true (atm_chart_projection_validate (p, NULL));
    atm_chart_projection_free (p);
}

static void test_reference (void)
{
    AtmChartSpec *spec = specification (ATM_CHART_LINE);
    char *id = g_strdup (atm_chart_spec_id (spec));
    AtmChartProjection *p = project (spec, 580, 160);
    g_assert_cmpstr (atm_chart_projection_policy (p), ==, ATM_CHART_PROJECTION_POLICY);
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

static AtmChartSpecV4 *
v4_spec_from_source (const char *series_id, AtmChartSpec *source)
{
    const AtmChartCatalogEntry *entry = atm_chart_catalog_find_id (series_id);
    AtmChartSpecV4 *spec = NULL;
    GError *error = NULL;
    g_assert_nonnull (entry);
    g_assert_true (atm_chart_spec_v4_new (entry, source, &spec, &error));
    g_assert_no_error (error);
    atm_chart_spec_free (source);
    return spec;
}

static AtmChartProjectionV4 *
project_v4 (const AtmChartSpecV4 *spec, double width, double height)
{
    AtmChartProjectionV4 *projection = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_projection_v4_new (
        spec, width, height, &projection, &error));
    g_assert_no_error (error);
    return projection;
}

static void
test_v4_projection_all_catalog_profiles (void)
{
    AtmChartSpecV4 *gistemp_spec = v4_spec_from_source (
        "climate.gistemp.global-temperature-anomaly",
        specification (ATM_CHART_LINE));
    AtmChartProjectionV4 *gistemp = project_v4 (gistemp_spec, 580, 220);
    g_assert_cmpstr (atm_chart_projection_v4_schema (gistemp), ==,
                     ATM_CHART_PROJECTION_V4_SCHEMA);
    g_assert_cmpstr (atm_chart_projection_v4_series_id (gistemp), ==,
                     "climate.gistemp.global-temperature-anomaly");
    g_assert_cmpstr (atm_chart_projection_v4_policy (gistemp), ==,
                     "atm-display/gistemp-linear/1");
    g_assert_cmpuint (atm_chart_projection_v4_coordinate_count (gistemp), ==, 146u);
    g_assert_cmpuint (atm_chart_projection_v4_numeric_observation_count (gistemp), ==, 146u);
    g_assert_cmpuint (atm_chart_projection_count (
        atm_chart_projection_v4_source_projection (gistemp)), ==, 146u);
    g_assert_true (atm_chart_projection_v4_validate (gistemp, NULL));
    g_assert_cmpstr (atm_chart_spec_id (
        atm_chart_projection_spec (atm_chart_projection_v4_source_projection (gistemp))), ==,
        atm_chart_spec_id (atm_chart_spec_v4_source_spec (
            atm_chart_projection_v4_spec (gistemp))));
    atm_chart_projection_v4_free (gistemp);
    atm_chart_spec_v4_free (gistemp_spec);

    const char *snapshots[] = {
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb"
    };
    for (guint i = 0; i < G_N_ELEMENTS (snapshots); i++) {
        AtmChartSpecV4 *ei_spec = v4_spec_from_source (
            "energy.energy-institute.global-total-energy-supply",
            ei_specification (ATM_CHART_LINE, snapshots[i]));
        AtmChartProjectionV4 *ei = project_v4 (ei_spec, 580, 220);
        g_assert_cmpstr (atm_chart_projection_v4_policy (ei), ==,
                         "atm-display/energy-institute-tes-linear/1");
        g_assert_cmpuint (atm_chart_projection_v4_coordinate_count (ei), ==, 61u);
        g_assert_cmpuint (atm_chart_projection_v4_numeric_observation_count (ei), ==, 61u);
        g_assert_cmpuint (atm_chart_projection_count (
            atm_chart_projection_v4_source_projection (ei)), ==, 61u);
        g_assert_true (atm_chart_projection_v4_validate (ei, NULL));
        atm_chart_projection_v4_free (ei);
        atm_chart_spec_v4_free (ei_spec);
    }

    AtmChartSpecV4 *bnr_spec = v4_spec_from_source (
        "banking.bnr.bls-c01.credit-standards-net-percentage",
        bnr_specification (ATM_CHART_LINE));
    AtmChartProjectionV4 *bnr = project_v4 (bnr_spec, 580, 220);
    const AtmChartProjection *bnr_projection =
        atm_chart_projection_v4_source_projection (bnr);
    g_assert_cmpstr (atm_chart_projection_v4_policy (bnr), ==,
                     "atm-display/bnr-bls-c01-quarterly-gap/1");
    g_assert_cmpuint (atm_chart_projection_v4_coordinate_count (bnr), ==, 12u);
    g_assert_cmpuint (atm_chart_projection_v4_numeric_observation_count (bnr), ==, 11u);
    g_assert_cmpstr (atm_chart_spec_v4_missing_coordinate (
        atm_chart_projection_v4_spec (bnr)), ==, "2025-Q2");
    g_assert_cmpstr (atm_chart_spec_v4_missing_reason (
        atm_chart_projection_v4_spec (bnr)), ==, "source_round_not_retained");
    g_assert_cmpuint (atm_chart_projection_count (bnr_projection), ==, 11u);
    for (guint i = 0; i < 10u; i++)
        g_assert_cmpuint (atm_chart_projection_point (bnr_projection, i)->point_index, ==, i);
    g_assert_cmpuint (atm_chart_projection_point (bnr_projection, 10)->point_index, ==, 11u);
    g_assert_true (atm_chart_projection_v4_validate (bnr, NULL));
    atm_chart_projection_v4_free (bnr);
    atm_chart_spec_v4_free (bnr_spec);
}

static void
test_v4_projection_rejections_and_tampering (void)
{
    AtmChartSpecV4 *spec = v4_spec_from_source (
        "climate.gistemp.global-temperature-anomaly",
        specification (ATM_CHART_LINE));
    AtmChartProjectionV4 *projection = NULL;
    GError *error = NULL;

    g_assert_false (atm_chart_projection_v4_new (
        NULL, 580, 220, &projection, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_null (projection);

    g_assert_false (atm_chart_projection_v4_new (
        spec, 319.0, 220, &projection, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_null (projection);

    char *id = (char *) atm_chart_spec_v4_id (spec);
    char saved = id[0];
    id[0] = saved == 'a' ? 'b' : 'a';
    g_assert_false (atm_chart_projection_v4_new (
        spec, 580, 220, &projection, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_null (projection);
    id[0] = saved;

    projection = project_v4 (spec, 580, 220);
    AtmChartProjectionV4 *saved_projection = projection;
    g_assert_false (atm_chart_projection_v4_new (
        spec, 580, 220, &projection, &error));
    g_assert_error (error, ATM_CHART_PROJECTION_V4_ERROR,
                    ATM_CHART_PROJECTION_V4_ERROR_ARGUMENT);
    g_clear_error (&error);
    g_assert_true (projection == saved_projection);

    AtmChartProjectedPoint *point = (AtmChartProjectedPoint *)
        atm_chart_projection_point (
            atm_chart_projection_v4_source_projection (projection), 0);
    guint old_index = point->point_index;
    point->point_index++;
    g_assert_false (atm_chart_projection_v4_validate (projection, NULL));
    point->point_index = old_index;
    g_assert_true (atm_chart_projection_v4_validate (projection, NULL));

    atm_chart_projection_v4_free (projection);
    atm_chart_spec_v4_free (spec);
    g_assert_false (atm_chart_projection_v4_validate (NULL, NULL));
    g_assert_null (atm_chart_projection_v4_spec (NULL));
    g_assert_null (atm_chart_projection_v4_source_projection (NULL));
    g_assert_null (atm_chart_projection_v4_series_id (NULL));
    g_assert_null (atm_chart_projection_v4_policy (NULL));
    g_assert_cmpuint (atm_chart_projection_v4_coordinate_count (NULL), ==, 0u);
    g_assert_cmpuint (atm_chart_projection_v4_numeric_observation_count (NULL), ==, 0u);
    atm_chart_projection_v4_free (NULL);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-projection/reference-ownership", test_reference);
    g_test_add_func ("/chart-projection/resize", test_resize);
    g_test_add_func ("/chart-projection/rejections", test_rejections);
    g_test_add_func ("/chart-projection/tampering", test_tampering);
    g_test_add_func ("/chart-projection/bnr-quarterly-gap-policy", test_bnr_quarterly_gap_policy);
    g_test_add_func ("/chart-projection/ei-decimal-conversion", test_ei_decimal_conversion);
    g_test_add_func ("/chart-projection/ei-policy-resize", test_ei_projection_both_snapshots_and_resize);
    g_test_add_func ("/chart-projection/ei-tampering", test_ei_projection_tampering);
    g_test_add_func ("/chart-projection-v4/all-catalog-profiles", test_v4_projection_all_catalog_profiles);
    g_test_add_func ("/chart-projection-v4/rejections-tampering", test_v4_projection_rejections_and_tampering);
    return g_test_run ();
}
