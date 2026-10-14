#include "chart_spec.h"
#include "energy_institute_verified_series.h"
#include "energy_institute_admission.h"
#include "bnr_bls_verified_series.h"
#include "bnr_bls_series_contract.h"
#include "series_contract.h"
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

static void
test_bnr_v3_source_bound_quarters_and_missing (void)
{
    static const char *const quarters[] = {
        "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3",
        "2023-Q4", "2024-Q1", "2024-Q2", "2024-Q3",
        "2024-Q4", "2025-Q1", "2025-Q2", "2025-Q3"
    };
    static const char *const values[] = {
        "11.89", "16.34", "15.54", "0.0", "0.0", "0.0",
        "0.0", "12.54", "12.08", "11.71", NULL, "9.46"
    };

    AtmBnrBlsVerifiedSeries *input = build_bnr_verified_series ();
    AtmChartSpec *line = NULL, *again = NULL, *scatter = NULL, *rebuilt = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_spec_new_bnr_bls_c01_v3 (
        ATM_CHART_LINE, input, &line, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_bnr_bls_c01_v3 (
        ATM_CHART_LINE, input, &again, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_bnr_bls_c01_v3 (
        ATM_CHART_SCATTER, input, &scatter, &error));
    g_assert_no_error (error);

    g_assert_cmpint (atm_chart_spec_version (line), ==,
                     ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3);
    g_assert_cmpstr (atm_chart_spec_schema (line), ==, ATM_CHART_SPEC_V3_SCHEMA);
    g_assert_cmpstr (atm_chart_spec_x_unit (line), ==, "calendar_quarter");
    g_assert_cmpstr (atm_chart_spec_x_dimension (line), ==, "time");
    g_assert_cmpstr (atm_chart_spec_y_unit (line), ==, "percent");
    g_assert_cmpstr (atm_chart_spec_y_dimension (line), ==, "survey_net_percentage");
    g_assert_cmpstr (atm_chart_spec_series_profile (line), ==,
                     ATM_BNR_BLS_VERIFIED_SERIES_PROFILE);
    g_assert_cmpstr (atm_chart_spec_series_repository_id (line, 0), ==, "rmd");
    g_assert_cmpstr (atm_chart_spec_series_snapshot_sha (line, 0), ==,
                     ATM_BNR_BLS_SNAPSHOT_SHA);
    g_assert_cmpstr (atm_chart_spec_series_source_path (line, 0), ==,
                     "data/processed/bnr_bls_realised_rounds.csv");
    g_assert_cmpstr (atm_chart_spec_id (line), ==, atm_chart_spec_id (again));
    g_assert_cmpstr (atm_chart_spec_id (line), !=, atm_chart_spec_id (scatter));
    g_assert_cmpuint (atm_chart_spec_x_min (line), ==, 0u);
    g_assert_cmpuint (atm_chart_spec_x_max (line), ==, 11u);
    g_assert_cmpuint (atm_chart_spec_point_count (line, 0), ==, 12u);
    g_assert_cmpstr (atm_chart_spec_y_min (line)->coefficient, ==, "0");
    g_assert_cmpstr (atm_chart_spec_y_max (line)->coefficient, ==, "1634");
    g_assert_cmpint (atm_chart_spec_y_max (line)->exponent, ==, -2);

    for (guint i = 0; i < G_N_ELEMENTS (quarters); i++) {
        AtmChartPointView point = {0};
        g_assert_true (atm_chart_spec_point (line, 0, i, &point));
        g_assert_cmpuint (point.order, ==, i);
        g_assert_cmpstr (point.calendar_quarter, ==, quarters[i]);
        g_assert_cmpuint (point.calendar_year, ==, 0u);
        g_assert_cmpuint (point.source_row, ==, 0u);
        g_assert_false (point.break_before);
        g_assert_nonnull (point.support);
        g_assert_cmpuint (point.support->len, >, 0u);
        if (i == 10u) {
            g_assert_cmpint (point.y_status, ==, ATM_CHART_POINT_MISSING);
            g_assert_null (point.source_decimal);
            g_assert_null (point.coefficient);
            g_assert_cmpint (point.exponent, ==, 0);
            g_assert_cmpuint (point.support->len, ==, ATM_BNR_BLS_SOURCE_COUNT);
        } else {
            g_assert_cmpint (point.y_status, ==, ATM_CHART_POINT_NUMERIC);
            g_assert_cmpstr (point.source_decimal, ==, values[i]);
            g_assert_nonnull (point.coefficient);
        }
    }

    g_assert_true (atm_chart_spec_rebuild (line, &rebuilt, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_chart_spec_id (line), ==, atm_chart_spec_id (rebuilt));
    g_assert_true (atm_chart_spec_validate (line, &error));
    g_assert_no_error (error);

    /* A forged absence reason is rejected at the source-specific constructor. */
    AtmSeriesContract *mutable_contract = (AtmSeriesContract *)
        atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (input));
    AtmSeriesContractPoint *missing =
        g_ptr_array_index (mutable_contract->points, 10u);
    char *saved_reason = missing->missing_reason;
    missing->missing_reason = "invented";
    AtmChartSpec *rejected = NULL;
    g_assert_false (atm_chart_spec_new_bnr_bls_c01_v3 (
        ATM_CHART_LINE, input, &rejected, NULL));
    g_assert_null (rejected);
    missing->missing_reason = saved_reason;
    g_assert_true (atm_chart_spec_validate (line, &error));
    g_assert_no_error (error);

    atm_chart_spec_free (rebuilt);
    atm_chart_spec_free (scatter);
    atm_chart_spec_free (again);
    atm_chart_spec_free (line);
    atm_bnr_bls_verified_series_free (input);
}

static void test_extents_ownership (void)
{
    AtmVerifiedSeries *source = build ();
    const AtmVerifiedSeries *inputs[] = {source};
    AtmChartSpec *line = NULL, *again = NULL, *scatter = NULL;
    g_assert_true (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &line, NULL));
    g_assert_true (atm_chart_spec_new (ATM_CHART_LINE, inputs, 1, &again, NULL));
    g_assert_cmpint (atm_chart_spec_version (line), ==, ATM_CHART_SPEC_VERSION_GISTEMP_V1);
    g_assert_cmpstr (atm_chart_spec_schema (line), ==, ATM_CHART_SPEC_SCHEMA);
    g_assert_cmpuint (atm_chart_spec_point_count (line, 0), ==, 146u);
    AtmChartPointView first_point = {0};
    g_assert_true (atm_chart_spec_point (line, 0, 0, &first_point));
    g_assert_cmpuint (first_point.calendar_year, ==, 1880u);
    g_assert_cmpint (first_point.y_status, ==, ATM_CHART_POINT_NUMERIC);
    g_assert_cmpstr (atm_chart_spec_x_unit (line), ==, "calendar_year");
    g_assert_cmpstr (atm_chart_spec_x_dimension (line), ==, "time");
    g_assert_cmpstr (atm_chart_spec_y_dimension (line), ==, "temperature_difference");
    g_assert_cmpstr (atm_chart_spec_series_profile (line), ==, ATM_VERIFIED_SERIES_PROFILE);
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

static AtmEnergyInstituteVerifiedSeries *
build_ei_series (const char *snapshot)
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
        gchar *bytes = NULL;
        gsize length = 0;
        GError *error = NULL;
        g_assert_true (g_file_get_contents (path, &bytes, &length, &error));
        g_assert_no_error (error);
        g_free (path);
        sources[i] = g_bytes_new_take (bytes, length);
    }

    AtmEnergyInstituteAdmission *admission = NULL;
    AtmEnergyInstituteVerifiedSeries *series = NULL;
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
    atm_energy_institute_admission_free (admission);
    return series;
}

static void
test_ei_v2_source_bound_identity_and_generic_accessors (void)
{
    const char *older_snapshot = "d9e249339663015f6d1c05752338a955bf64ad0b";
    const char *current_snapshot = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    AtmEnergyInstituteVerifiedSeries *current = build_ei_series (current_snapshot);
    AtmEnergyInstituteVerifiedSeries *current_again = build_ei_series (current_snapshot);
    AtmEnergyInstituteVerifiedSeries *older = build_ei_series (older_snapshot);
    AtmChartSpec *line = NULL, *again = NULL, *scatter = NULL, *old_line = NULL, *rebuilt = NULL;
    GError *error = NULL;

    g_assert_true (atm_chart_spec_new_energy_institute_v2 (ATM_CHART_LINE,
        current, &line, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_energy_institute_v2 (ATM_CHART_LINE,
        current_again, &again, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_energy_institute_v2 (ATM_CHART_SCATTER,
        current, &scatter, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_energy_institute_v2 (ATM_CHART_LINE,
        older, &old_line, &error));
    g_assert_no_error (error);

    g_assert_cmpint (atm_chart_spec_version (line), ==,
                     ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2);
    g_assert_cmpstr (atm_chart_spec_schema (line), ==, ATM_CHART_SPEC_V2_SCHEMA);
    g_assert_cmpstr (atm_chart_spec_id (line), ==, atm_chart_spec_id (again));
    g_assert_cmpstr (atm_chart_spec_id (line), !=, atm_chart_spec_id (scatter));
    g_assert_cmpstr (atm_chart_spec_id (line), !=, atm_chart_spec_id (old_line));
    /* AtmSeriesContract includes point-support IDs in its scientific
     * serialization. The two snapshots have equivalent SRA fact content,
     * but snapshot-bound support makes these full series identities distinct. */
    g_assert_cmpstr (atm_chart_spec_series_scientific_id (line, 0), !=,
                     atm_chart_spec_series_scientific_id (old_line, 0));
    g_assert_cmpstr (atm_chart_spec_series_qualified_id (line, 0), !=,
                     atm_chart_spec_series_qualified_id (old_line, 0));

    g_assert_cmpuint (atm_chart_spec_count (line), ==, 1u);
    g_assert_cmpuint (atm_chart_spec_point_count (line, 0), ==, 61u);
    g_assert_cmpuint (atm_chart_spec_point_count (line, 1), ==, 0u);
    g_assert_cmpuint (atm_chart_spec_x_min (line), ==, 1965u);
    g_assert_cmpuint (atm_chart_spec_x_max (line), ==, 2025u);
    g_assert_cmpstr (atm_chart_spec_y_min (line)->coefficient, ==, "1496960972");
    g_assert_cmpint (atm_chart_spec_y_min (line)->exponent, ==, -7);
    g_assert_cmpstr (atm_chart_spec_y_max (line)->coefficient, ==, "6003129128");
    g_assert_cmpint (atm_chart_spec_y_max (line)->exponent, ==, -7);
    g_assert_cmpstr (atm_chart_spec_x_unit (line), ==, "calendar_year");
    g_assert_cmpstr (atm_chart_spec_x_dimension (line), ==, "time");
    g_assert_cmpstr (atm_chart_spec_y_unit (line), ==, "exajoules/year");
    g_assert_cmpstr (atm_chart_spec_y_dimension (line), ==, "energy_flow_rate");
    g_assert_cmpstr (atm_chart_spec_series_profile (line), ==,
                     ATM_EI_VERIFIED_SERIES_PROFILE);
    g_assert_null (atm_chart_spec_series (line, 0));

    AtmChartPointView point = {0};
    g_assert_true (atm_chart_spec_point (line, 0, 0, &point));
    g_assert_cmpuint (point.order, ==, 0u);
    g_assert_cmpuint (point.calendar_year, ==, 1965u);
    g_assert_cmpint (point.y_status, ==, ATM_CHART_POINT_NUMERIC);
    g_assert_cmpstr (point.source_decimal, ==, "149.6960972");
    g_assert_cmpstr (point.coefficient, ==, "1496960972");
    g_assert_cmpint (point.exponent, ==, -7);
    g_assert_false (point.break_before);
    g_assert_cmpuint (point.support->len, ==, 5u);

    g_assert_true (atm_chart_spec_point (line, 0, 60, &point));
    g_assert_cmpuint (point.calendar_year, ==, 2025u);
    g_assert_cmpstr (point.source_decimal, ==, "600.3129128");
    g_assert_cmpstr (point.coefficient, ==, "6003129128");
    g_assert_cmpuint (point.support->len, ==, 5u);
    g_assert_false (atm_chart_spec_point (line, 0, 61, &point));
    g_assert_false (atm_chart_spec_point (line, 1, 0, &point));
    g_assert_false (atm_chart_spec_point (NULL, 0, 0, &point));
    g_assert_false (atm_chart_spec_point (line, 0, 0, NULL));

    g_assert_true (atm_chart_spec_validate (line, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_rebuild (line, &rebuilt, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_chart_spec_id (line), ==, atm_chart_spec_id (rebuilt));
    g_assert_true (atm_chart_spec_validate (old_line, &error));
    g_assert_no_error (error);

    atm_chart_spec_free (rebuilt);
    atm_chart_spec_free (old_line);
    atm_chart_spec_free (scatter);
    atm_chart_spec_free (again);
    atm_chart_spec_free (line);
    atm_energy_institute_verified_series_free (older);
    atm_energy_institute_verified_series_free (current_again);
    atm_energy_institute_verified_series_free (current);
}

static void
test_ei_v2_rejections_and_tampering (void)
{
    AtmEnergyInstituteVerifiedSeries *source =
        build_ei_series ("dc710d8e604c0fff713a3acfdb18ab47a1a902bb");
    AtmChartSpec *spec = NULL;
    AtmChartKind invalid_kinds[] = {0, ATM_CHART_STEP, ATM_CHART_BAR, 99};
    for (guint i = 0; i < G_N_ELEMENTS (invalid_kinds); i++)
        g_assert_false (atm_chart_spec_new_energy_institute_v2 (
            invalid_kinds[i], source, &spec, NULL));
    g_assert_false (atm_chart_spec_new_energy_institute_v2 (ATM_CHART_LINE,
        NULL, &spec, NULL));
    g_assert_false (atm_chart_spec_new_energy_institute_v2 (ATM_CHART_LINE,
        source, NULL, NULL));
    g_assert_null (spec);

    /* A series must pass its source/SRA revalidation before a ChartSpec is made. */
    AtmSeriesContract *mutable_contract = (AtmSeriesContract *)
        atm_energy_institute_verified_series_contract (source);
    AtmSeriesContractPoint *first = g_ptr_array_index (mutable_contract->points, 0);
    char saved_first = first->coefficient[0];
    first->coefficient[0] = saved_first == '1' ? '2' : '1';
    g_assert_false (atm_chart_spec_new_energy_institute_v2 (
        ATM_CHART_LINE, source, &spec, NULL));
    g_assert_null (spec);
    first->coefficient[0] = saved_first;

    g_assert_true (atm_chart_spec_new_energy_institute_v2 (
        ATM_CHART_LINE, source, &spec, NULL));
    AtmChartSpec *saved_spec = spec;
    g_assert_false (atm_chart_spec_new_energy_institute_v2 (
        ATM_CHART_LINE, source, &spec, NULL));
    g_assert_true (spec == saved_spec);

    AtmScientificDecimal *minimum =
        (AtmScientificDecimal *) atm_chart_spec_y_min (spec);
    minimum->exponent++;
    g_assert_false (atm_chart_spec_validate (spec, NULL));
    minimum->exponent--;

    char *id = (char *) atm_chart_spec_id (spec);
    char saved_id = id[0];
    id[0] = saved_id == 'a' ? 'b' : 'a';
    g_assert_false (atm_chart_spec_validate (spec, NULL));
    id[0] = saved_id;
    g_assert_true (atm_chart_spec_validate (spec, NULL));

    g_assert_null (atm_chart_spec_id (NULL));
    g_assert_null (atm_chart_spec_schema (NULL));
    g_assert_cmpint (atm_chart_spec_version (NULL), ==, 0);
    g_assert_cmpuint (atm_chart_spec_point_count (NULL, 0), ==, 0u);
    g_assert_null (atm_chart_spec_x_unit (NULL));
    g_assert_null (atm_chart_spec_y_unit (NULL));
    atm_chart_spec_free (spec);
    atm_energy_institute_verified_series_free (source);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-spec/extents-ownership", test_extents_ownership);
    g_test_add_func ("/chart-spec/bnr-v3-source-bound-quarter", test_bnr_v3_source_bound_quarters_and_missing);
    g_test_add_func ("/chart-spec/rejections", test_rejections);
    g_test_add_func ("/chart-spec/tampering", test_tampering);
    g_test_add_func ("/chart-spec/ei-v2-source-bound", test_ei_v2_source_bound_identity_and_generic_accessors);
    g_test_add_func ("/chart-spec/ei-v2-rejections", test_ei_v2_rejections_and_tampering);
    return g_test_run ();
}
