#include "chart_render.h"
#include "chart_render_v4.h"
#include "energy_institute_verified_series.h"
#include "energy_institute_admission.h"
#include "bnr_bls_verified_series.h"
#include "bnr_bls_series_contract.h"
#include <fontconfig/fontconfig.h>
#include <stdint.h>
#include <string.h>
static const char GISTEMP_SNAPSHOT_INITIAL[] =
    "d9e249339663015f6d1c05752338a955bf64ad0b";
static const char GISTEMP_SNAPSHOT_CURRENT[] =
    "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";

static AtmGistempAdmission *admit_for_snapshot (const char *snapshot)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    const char *fixture_tree = NULL;
    if (g_strcmp0 (snapshot, GISTEMP_SNAPSHOT_INITIAL) == 0)
        fixture_tree = "ewd";
    else if (g_strcmp0 (snapshot, GISTEMP_SNAPSHOT_CURRENT) == 0)
        fixture_tree = "ewd-current";
    else
        g_assert_not_reached ();
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json", "science/data/registry.csv"
    };
    GBytes *sources[4];
    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (root, fixture_tree, paths[i], NULL);
        char *bytes; gsize length;
        g_assert_true (g_file_get_contents (path, &bytes, &length, NULL));
        sources[i] = g_bytes_new_take (bytes, length);
        g_free (path);
    }
    AtmGistempAdmission *a = NULL;
    g_assert_true (atm_gistemp_admission_new (
        "LaurentiuStaicu/empirical-world3-dynamics", snapshot, sources, &a, NULL));
    for (guint i = 0; i < 4; i++) g_bytes_unref (sources[i]);
    return a;
}

static AtmVerifiedSeries *build_for_snapshot (const char *snapshot)
{
    AtmGistempAdmission *a = admit_for_snapshot (snapshot);
    AtmVerifiedSeries *s = NULL;
    GError *error = NULL;
    g_assert_true (atm_verified_series_from_gistemp (a, &s, &error));
    g_assert_no_error (error);
    atm_gistemp_admission_free (a);
    return s;
}

static AtmChartSpec *specification_for_snapshot (AtmChartKind kind,
    const char *snapshot)
{
    AtmVerifiedSeries *s = build_for_snapshot (snapshot);
    const AtmVerifiedSeries *inputs[] = {s};
    AtmChartSpec *spec = NULL;
    g_assert_true (atm_chart_spec_new (kind, inputs, 1, &spec, NULL));
    atm_verified_series_free (s);
    return spec;
}

static AtmChartSpec *specification (AtmChartKind kind)
{
    return specification_for_snapshot (kind, GISTEMP_SNAPSHOT_INITIAL);
}

static AtmChartSpec *
ei_specification_for_renderer_test (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    const char *snapshot = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    const char *paths[] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[ATM_EI_SOURCE_COUNT] = {NULL};
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, "ewd-current", paths[i], NULL);
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
        "LaurentiuStaicu/empirical-world3-dynamics", snapshot,
        sources, &admission, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);
    g_assert_true (atm_energy_institute_verified_series_from_admission (
        admission, &series, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_energy_institute_v2 (
        ATM_CHART_LINE, series, &spec, &error));
    g_assert_no_error (error);
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
    return spec;
}

static AtmChartSpec *
bnr_specification_for_renderer_test (AtmChartKind kind)
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
    AtmChartSpec *spec = NULL;
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
    g_assert_true (atm_chart_spec_new_bnr_bls_c01_v3 (
        kind, series, &spec, &error));
    g_assert_no_error (error);
    atm_bnr_bls_verified_series_free (series);
    atm_bnr_bls_series_contract_free (contract);
    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
    return spec;
}

static gboolean
pixel_is_black (const AtmChartRender *rendered, int x, int y)
{
    cairo_surface_t *surface = atm_chart_render_surface (rendered);
    cairo_surface_flush (surface);
    const unsigned char *bytes = cairo_image_surface_get_data (surface);
    int stride = cairo_image_surface_get_stride (surface);
    return ((const uint32_t *) (bytes + y * stride))[x] == 0xff000000u;
}

static void
test_bnr_data_view_and_real_gap (void)
{
    AtmChartSpec *spec = bnr_specification_for_renderer_test (ATM_CHART_LINE);
    AtmChartRender *rendered = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_render_new (spec, 480, 348, 1, &rendered, &error));
    g_assert_no_error (error);
    g_assert_nonnull (rendered);
    g_assert_false (atm_chart_render_is_fallback (rendered));
    g_assert_cmpint (atm_chart_spec_version (atm_chart_render_spec (rendered)), ==,
                     ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3);

    const char *data = atm_chart_render_data (rendered);
    g_assert_nonnull (data);
    g_assert_nonnull (strstr (data, "# BNR BLS C01 credit standards net percentage"));
    g_assert_nonnull (strstr (data, "# unit\tpercent"));
    g_assert_nonnull (strstr (data, "# source_snapshot\te43eb4e899f123d3b1aa0f6e20b7c8ed35434170"));
    g_assert_nonnull (strstr (data, "# source_path\tdata/processed/bnr_bls_realised_rounds.csv"));
    g_assert_nonnull (strstr (data, "series\tquarter\tstatus\tsource_decimal"));
    g_assert_nonnull (strstr (data, "0\t2022-Q4\tNUMERIC\t11.89\t"));
    g_assert_nonnull (strstr (data, "0\t2025-Q1\tNUMERIC\t11.71\t"));
    g_assert_nonnull (strstr (data, "0\t2025-Q2\tMISSING\t\t\t0\tnone\t"));
    g_assert_nonnull (strstr (data, "\tsource_round_not_retained\n"));
    g_assert_nonnull (strstr (data, "0\t2025-Q3\tNUMERIC\t9.46\t"));
    g_assert_null (strstr (data, "0\t2025-Q2\tNUMERIC\t0"));

    char **lines = g_strsplit (data, "\n", -1);
    guint rows = 0u;
    for (guint i = 0; lines[i] != NULL; i++)
        if (g_str_has_prefix (lines[i], "0\t202"))
            rows++;
    g_assert_cmpuint (rows, ==, 12u);
    g_strfreev (lines);

    /* Q2 2025's X ordinal lies halfway between Q1 and Q3, but the line must
     * have no segment across that midpoint. It is neither zero-imputed nor a
     * missing-marker glyph, and it is not a CSV row. */
    const int missing_x = (int) (84.0 + (10.0 / 11.0) * 368.0 + 0.5);
    const int expected_bridge_y = (int) (108.0 +
        ((20.0 - (11.71 + 9.46) / 2.0) / 20.0) * 160.0 + 0.5);
    g_assert_false (pixel_is_black (rendered, missing_x, expected_bridge_y));

    AtmChartSpec *scatter_spec = bnr_specification_for_renderer_test (ATM_CHART_SCATTER);
    AtmChartRender *scatter = NULL;
    g_assert_true (atm_chart_render_new (scatter_spec, 480, 348, 1, &scatter, &error));
    g_assert_no_error (error);
    g_assert_false (atm_chart_render_is_fallback (scatter));
    g_assert_nonnull (strstr (atm_chart_render_data (scatter), "0\t2025-Q2\tMISSING"));
    g_assert_false (pixel_is_black (scatter, missing_x, expected_bridge_y));

    atm_chart_render_free (scatter);
    atm_chart_spec_free (scatter_spec);
    atm_chart_render_free (rendered);
    atm_chart_spec_free (spec);
}

static void
test_ei_projection_renders_exact_data_and_resizes (void)
{
    AtmChartSpec *spec = ei_specification_for_renderer_test ();
    AtmChartRender *rendered = NULL;
    GError *error = NULL;

    /* The GTK-embedded viewport is 480x348 logical pixels. It must fit the
     * complete EI chart, including its provenance footer, rather than silently
     * degrading to the compact "Chart needs more space" fallback. */
    g_assert_true (atm_chart_render_new (spec, 480, 348, 1, &rendered, &error));
    g_assert_no_error (error);
    g_assert_nonnull (rendered);
    g_assert_false (atm_chart_render_is_fallback (rendered));
    atm_chart_render_free (rendered);
    rendered = NULL;

    g_assert_true (atm_chart_render_new (spec, 860, 500, 1, &rendered, &error));
    g_assert_no_error (error);
    g_assert_nonnull (rendered);
    g_assert_nonnull (atm_chart_render_surface (rendered));

    const char *data = atm_chart_render_data (rendered);
    g_assert_nonnull (data);
    g_assert_nonnull (strstr (data, "# Energy Institute global total energy supply"));
    g_assert_nonnull (strstr (data, "# unit\texajoules/year"));
    g_assert_nonnull (strstr (data, "# source_snapshot\tdc710d8e604c0fff713a3acfdb18ab47a1a902bb"));
    g_assert_nonnull (strstr (data, "# source_path\tscience/data/processed/energy_institute_global_2026.csv"));
    g_assert_nonnull (strstr (data, "1965"));
    g_assert_nonnull (strstr (data, "2025"));
    g_assert_null (strstr (data, "GISTEMP global annual temperature anomaly"));

    char **lines = g_strsplit (data, "\n", -1);
    guint rows = 0;
    for (guint i = 0; lines[i] != NULL; i++) {
        if (lines[i][0] == '\0' || lines[i][0] == '#' ||
            g_str_has_prefix (lines[i], "series\t"))
            continue;
        rows++;
    }
    g_assert_cmpuint (rows, ==, 61u);
    g_strfreev (lines);

    char *before = g_strdup (data);
    g_assert_true (atm_chart_render_resize (rendered, 700, 400, 1, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_chart_render_data (rendered), ==, before);

    cairo_surface_t *valid_surface = atm_chart_render_surface (rendered);
    g_assert_false (atm_chart_render_resize (rendered, 120, 100, 1, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_true (atm_chart_render_surface (rendered) == valid_surface);
    g_assert_cmpstr (atm_chart_render_data (rendered), ==, before);

    g_free (before);
    atm_chart_render_free (rendered);
    atm_chart_spec_free (spec);
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
static void test_gistemp_data_view_uses_qualified_snapshot (void)
{
    const char *snapshots[] = {
        GISTEMP_SNAPSHOT_INITIAL,
        GISTEMP_SNAPSHOT_CURRENT
    };
    for (guint i = 0; i < G_N_ELEMENTS (snapshots); i++) {
        AtmChartSpec *spec = specification_for_snapshot (
            ATM_CHART_LINE, snapshots[i]);
        AtmChartRender *rendered = render (spec, 860, 500, 1);
        const AtmChartSpec *rendered_spec = atm_chart_render_spec (rendered);
        const char *data = atm_chart_render_data (rendered);
        g_assert_cmpstr (
            atm_chart_spec_series_snapshot_sha (rendered_spec, 0), ==,
            snapshots[i]);
        g_assert_cmpstr (
            atm_chart_spec_series_repository_id (rendered_spec, 0), ==, "ewd");
        g_assert_nonnull (strstr (data,
            "# source_repository\tLaurentiuStaicu/empirical-world3-dynamics\n"));
        char *snapshot_line = g_strdup_printf (
            "# source_snapshot\t%s\n", snapshots[i]);
        g_assert_nonnull (strstr (data, snapshot_line));
        g_free (snapshot_line);
        g_assert_nonnull (strstr (data,
            "# source_path\tscience/data/processed/nasa_gistemp_global_2026.csv\n"));
        atm_chart_render_free (rendered);
        atm_chart_spec_free (spec);
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

static AtmChartSpecV4 *
render_v4_spec_from_source (const char *series_id, AtmChartSpec *source)
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

static AtmChartRenderV4 *
render_v4_new_for_test (const AtmChartSpecV4 *spec,
                        guint width,
                        guint height,
                        guint scale)
{
    AtmChartRenderV4 *render = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_render_v4_new (
        spec, width, height, scale, &render, &error));
    g_assert_no_error (error);
    return render;
}

static void
test_v4_gistemp_data_view_uses_qualified_snapshot (void)
{
    static const char *const series_id =
        "climate.gistemp.global-temperature-anomaly";
    const char *snapshots[] = {
        GISTEMP_SNAPSHOT_INITIAL,
        GISTEMP_SNAPSHOT_CURRENT
    };

    for (guint i = 0; i < G_N_ELEMENTS (snapshots); i++) {
        AtmChartSpecV4 *spec = render_v4_spec_from_source (
            series_id, specification_for_snapshot (ATM_CHART_LINE, snapshots[i]));
        AtmChartRenderV4 *rendered = render_v4_new_for_test (
            spec, 860, 500, 1);
        const AtmChartSpec *source_spec = atm_chart_spec_v4_source_spec (
            atm_chart_render_v4_spec (rendered));
        const char *data = atm_chart_render_v4_data (rendered);

        g_assert_cmpstr (
            atm_chart_spec_series_snapshot_sha (source_spec, 0), ==,
            snapshots[i]);
        g_assert_cmpstr (
            atm_chart_spec_series_repository_id (source_spec, 0), ==, "ewd");
        g_assert_nonnull (strstr (data,
            "# source_repository\tLaurentiuStaicu/empirical-world3-dynamics\n"));
        char *snapshot_line = g_strdup_printf (
            "# source_snapshot\t%s\n", snapshots[i]);
        g_assert_nonnull (strstr (data, snapshot_line));
        g_free (snapshot_line);
        g_assert_nonnull (strstr (data,
            "# source_path\tscience/data/processed/nasa_gistemp_global_2026.csv\n"));

        atm_chart_render_v4_free (rendered);
        atm_chart_spec_v4_free (spec);
    }
}

static char *
v4_surface_digest (cairo_surface_t *surface)
{
    g_assert_nonnull (surface);
    g_assert_cmpint (cairo_surface_get_type (surface), ==,
                     CAIRO_SURFACE_TYPE_IMAGE);
    cairo_surface_flush (surface);
    g_assert_cmpint (cairo_surface_status (surface), ==, CAIRO_STATUS_SUCCESS);
    int width = cairo_image_surface_get_width (surface);
    int height = cairo_image_surface_get_height (surface);
    int stride = cairo_image_surface_get_stride (surface);
    const unsigned char *pixels = cairo_image_surface_get_data (surface);
    g_assert_cmpint (width, >, 0);
    g_assert_cmpint (height, >, 0);
    g_assert_cmpint (stride, >=, width * 4);
    return g_compute_checksum_for_data (
        G_CHECKSUM_SHA256, pixels, (gsize) stride * (gsize) height);
}

static void
assert_v4_surface_is_monochrome (cairo_surface_t *surface)
{
    g_assert_nonnull (surface);
    g_assert_cmpint (cairo_surface_get_type (surface), ==,
                     CAIRO_SURFACE_TYPE_IMAGE);
    cairo_surface_flush (surface);
    const unsigned char *bytes = cairo_image_surface_get_data (surface);
    int width = cairo_image_surface_get_width (surface);
    int height = cairo_image_surface_get_height (surface);
    int stride = cairo_image_surface_get_stride (surface);
    for (int y = 0; y < height; y++) {
        const uint32_t *row =
            (const uint32_t *) (bytes + (gsize) y * (gsize) stride);
        for (int x = 0; x < width; x++) {
            g_assert_true (row[x] == 0xff000000u || row[x] == 0xffffffffu);
        }
    }
}

static void
assert_render_v4_profile_is_generic_and_exact_data (
    const char *series_id,
    AtmChartSpec *source_spec)
{
    AtmChartSpecV4 *v4 = render_v4_spec_from_source (series_id, source_spec);
    AtmChartRenderV4 *rendered = render_v4_new_for_test (v4, 860, 500, 1);
    const AtmChartSpec *retained_source =
        atm_chart_spec_v4_source_spec (atm_chart_render_v4_spec (rendered));
    GError *error = NULL;
    char *expected_data =
        atm_chart_render_data_for_spec (retained_source, &error);
    g_assert_no_error (error);
    g_assert_nonnull (expected_data);

    AtmChartRender *legacy_data_reference = NULL;
    g_assert_true (atm_chart_render_new (
        retained_source, 860, 500, 1, &legacy_data_reference, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_render_v4_validate (rendered, &error));
    g_assert_no_error (error);
    g_assert_nonnull (atm_chart_render_v4_id (rendered));
    g_assert_cmpstr (atm_chart_render_v4_schema (rendered), ==,
                     ATM_CHART_RENDER_V4_SCHEMA);
    g_assert_cmpstr (atm_chart_spec_v4_id (
        atm_chart_render_v4_spec (rendered)), ==, atm_chart_spec_v4_id (v4));
    g_assert_false (atm_chart_render_v4_is_fallback (rendered));
    g_assert_nonnull (atm_chart_render_v4_projection (rendered));
    g_assert_cmpstr (atm_chart_render_v4_data (rendered), ==, expected_data);
    g_assert_cmpstr (atm_chart_render_v4_data (rendered), ==,
                     atm_chart_render_data (legacy_data_reference));
    assert_v4_surface_is_monochrome (atm_chart_render_v4_surface (rendered));

    char *first_digest =
        v4_surface_digest (atm_chart_render_v4_surface (rendered));
    AtmChartRenderV4 *repeat = render_v4_new_for_test (v4, 860, 500, 1);
    char *repeat_digest =
        v4_surface_digest (atm_chart_render_v4_surface (repeat));
    g_assert_cmpstr (first_digest, ==, repeat_digest);
    g_assert_true (atm_chart_render_v4_validate (repeat, &error));
    g_assert_no_error (error);

    g_free (first_digest);
    g_free (repeat_digest);
    g_free (expected_data);
    atm_chart_render_free (legacy_data_reference);
    atm_chart_render_v4_free (repeat);
    atm_chart_render_v4_free (rendered);
    atm_chart_spec_v4_free (v4);
}

static void
test_v4_generic_painter_profiles_and_exact_data (void)
{
    assert_render_v4_profile_is_generic_and_exact_data (
        "climate.gistemp.global-temperature-anomaly",
        specification (ATM_CHART_LINE));
    assert_render_v4_profile_is_generic_and_exact_data (
        "energy.energy-institute.global-total-energy-supply",
        ei_specification_for_renderer_test ());
    assert_render_v4_profile_is_generic_and_exact_data (
        "banking.bnr.bls-c01.credit-standards-net-percentage",
        bnr_specification_for_renderer_test (ATM_CHART_LINE));
}

static void
test_v4_render_fallback_keeps_exact_data (void)
{
    AtmChartSpecV4 *spec = render_v4_spec_from_source (
        "climate.gistemp.global-temperature-anomaly",
        specification (ATM_CHART_LINE));
    AtmChartRenderV4 *render = render_v4_new_for_test (spec, 320, 200, 1);
    const char *expected_line = g_strdup_printf (
        "# chart_spec_id\t%s\n",
        atm_chart_spec_id (atm_chart_spec_v4_source_spec (
            atm_chart_render_v4_spec (render))));
    g_assert_true (atm_chart_render_v4_is_fallback (render));
    g_assert_null (atm_chart_render_v4_projection (render));
    g_assert_nonnull (atm_chart_render_v4_surface (render));
    g_assert_nonnull (atm_chart_render_v4_data (render));
    g_assert_nonnull (strstr (atm_chart_render_v4_data (render), expected_line));
    g_free ((char *) expected_line);
    g_assert_true (atm_chart_render_v4_validate (render, NULL));
    atm_chart_render_v4_free (render);
    atm_chart_spec_v4_free (spec);
}

static void
test_v4_render_rejections_and_tampering (void)
{
    AtmChartSpecV4 *spec = render_v4_spec_from_source (
        "climate.gistemp.global-temperature-anomaly",
        specification (ATM_CHART_LINE));
    AtmChartRenderV4 *render = NULL;
    GError *error = NULL;

    g_assert_false (atm_chart_render_v4_new (NULL, 860, 500, 1, &render, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_null (render);

    g_assert_false (atm_chart_render_v4_new (spec, 220, 500, 1, &render, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_null (render);

    char *spec_id = (char *) atm_chart_spec_v4_id (spec);
    char saved_spec_char = spec_id[0];
    spec_id[0] = saved_spec_char == 'a' ? 'b' : 'a';
    g_assert_false (atm_chart_render_v4_new (spec, 860, 500, 1, &render, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_null (render);
    spec_id[0] = saved_spec_char;

    render = render_v4_new_for_test (spec, 860, 500, 1);
    AtmChartRenderV4 *saved_render = render;
    g_assert_false (atm_chart_render_v4_new (spec, 860, 500, 1, &render, &error));
    g_assert_error (error, ATM_CHART_RENDER_V4_ERROR,
                    ATM_CHART_RENDER_V4_ERROR_ARGUMENT);
    g_clear_error (&error);
    g_assert_true (render == saved_render);

    const AtmChartProjectionV4 *projection =
        atm_chart_render_v4_projection (render);
    AtmChartProjectedPoint *point = (AtmChartProjectedPoint *)
        atm_chart_projection_point (
            atm_chart_projection_v4_source_projection (projection), 0);
    guint saved_point_index = point->point_index;
    point->point_index++;
    g_assert_false (atm_chart_render_v4_validate (render, NULL));
    point->point_index = saved_point_index;
    g_assert_true (atm_chart_render_v4_validate (render, NULL));

    char *render_id = (char *) atm_chart_render_v4_id (render);
    char saved_render_char = render_id[0];
    render_id[0] = saved_render_char == 'a' ? 'b' : 'a';
    g_assert_false (atm_chart_render_v4_validate (render, NULL));
    render_id[0] = saved_render_char;
    g_assert_true (atm_chart_render_v4_validate (render, NULL));

    atm_chart_render_v4_free (render);
    atm_chart_spec_v4_free (spec);
    g_assert_false (atm_chart_render_v4_validate (NULL, NULL));
    g_assert_null (atm_chart_render_v4_spec (NULL));
    g_assert_null (atm_chart_render_v4_projection (NULL));
    g_assert_null (atm_chart_render_v4_surface (NULL));
    g_assert_null (atm_chart_render_v4_data (NULL));
    g_assert_null (atm_chart_render_v4_id (NULL));
    g_assert_false (atm_chart_render_v4_is_fallback (NULL));
    atm_chart_render_v4_free (NULL);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-render/line-data-ownership", test_line_data_ownership);
    g_test_add_func ("/chart-render/gistemp-data-qualified-snapshot", test_gistemp_data_view_uses_qualified_snapshot);
    g_test_add_func ("/chart-render/scatter-scale", test_scatter_scale);
    g_test_add_func ("/chart-render/resize-transactional", test_resize_transactional);
    g_test_add_func ("/chart-render/fallback-arguments", test_fallback_arguments);
    g_test_add_func ("/chart-render/bnr-data-view-and-real-gap", test_bnr_data_view_and_real_gap);
    g_test_add_func ("/chart-render/ei-exact-data-resize", test_ei_projection_renders_exact_data_and_resizes);
    g_test_add_func ("/chart-render-v4/generic-painter-and-exact-data", test_v4_generic_painter_profiles_and_exact_data);
    g_test_add_func ("/chart-render-v4/gistemp-data-qualified-snapshot", test_v4_gistemp_data_view_uses_qualified_snapshot);
    g_test_add_func ("/chart-render-v4/fallback-data", test_v4_render_fallback_keeps_exact_data);
    g_test_add_func ("/chart-render-v4/rejections-tampering", test_v4_render_rejections_and_tampering);
    int result = g_test_run ();
    /* Release process-wide Cairo/Fontconfig test caches after every object is
     * destroyed, so leak checking remains enabled without suppressions. */
    cairo_debug_reset_static_data (); FcFini ();
    return result;
}
