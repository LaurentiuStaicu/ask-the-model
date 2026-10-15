#include "bnr_bls_verified_series.h"

static const char *const fixture_names[ATM_BNR_BLS_SOURCE_COUNT] = {
    "bnr_bls_realised_rounds.csv",
    "processed_data_inventory_registry.json",
    "bnr_bls_canonical_panel_promotion_review.json",
    "bnr_bls_cross_round_mapping_audit.json",
    "bnr_bls_missing_round_semantic_mapping_audit.json"
};

static const char *const expected_quarters[] = {
    "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3",
    "2023-Q4", "2024-Q1", "2024-Q2", "2024-Q3",
    "2024-Q4", "2025-Q1", "2025-Q2", "2025-Q3"
};

static const char *const expected_values[] = {
    "11.89", "16.34", "15.54", "0.0", "0.0", "0.0",
    "0.0", "12.54", "12.08", "11.71", NULL, "9.46"
};

static AtmBnrBlsSeriesContract *
build_contract (void)
{
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);
    GBytes *sources[ATM_BNR_BLS_SOURCE_COUNT] = {NULL};

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, fixture_names[i], NULL);
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
    GError *error = NULL;

    g_assert_true (atm_bnr_bls_panel_new (
        ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        ATM_BNR_BLS_SNAPSHOT_SHA,
        sources, &panel, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);

    g_assert_true (atm_bnr_bls_sra_new (panel, &sra, &error));
    g_assert_no_error (error);
    g_assert_true (atm_bnr_bls_series_contract_from_sra (
        panel, sra, &contract, &error));
    g_assert_no_error (error);

    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
    return contract;
}

static void
test_source_qualified_quarterly_series (void)
{
    AtmBnrBlsSeriesContract *contract = build_contract ();
    AtmBnrBlsVerifiedSeries *series = NULL;
    GError *error = NULL;

    g_assert_true (atm_bnr_bls_verified_series_from_contract (
        contract, &series, &error));
    g_assert_no_error (error);
    g_assert_nonnull (series);
    g_assert_true (atm_bnr_bls_verified_series_validate (series, &error));
    g_assert_no_error (error);

    const AtmSeriesContract *qualified =
        atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (series));
    g_assert_nonnull (qualified);
    g_assert_cmpstr (qualified->profile_id, ==,
                     ATM_BNR_BLS_VERIFIED_SERIES_PROFILE);
    g_assert_cmpstr (qualified->snapshot_sha, ==,
                     ATM_BNR_BLS_SNAPSHOT_SHA);
    g_assert_cmpint (qualified->x_kind, ==, ATM_SERIES_X_CALENDAR_QUARTER);
    g_assert_cmpuint (atm_bnr_bls_verified_series_count (series), ==, 12u);

    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        const AtmSeriesContractPoint *point =
            atm_bnr_bls_verified_series_point (series, i);
        g_assert_nonnull (point);
        g_assert_cmpuint (point->order, ==, i);
        g_assert_cmpstr (point->x, ==, expected_quarters[i]);
        if (i == 10u) {
            g_assert_cmpint (point->y_status, ==, ATM_SERIES_Y_MISSING);
            g_assert_null (point->source_decimal);
            g_assert_null (point->coefficient);
            g_assert_cmpstr (point->missing_reason, ==,
                             ATM_BNR_BLS_MISSING_REASON);
            g_assert_cmpuint (point->support->len, ==,
                              ATM_BNR_BLS_SOURCE_COUNT);
        } else {
            g_assert_cmpint (point->y_status, ==, ATM_SERIES_Y_NUMERIC);
            g_assert_cmpstr (point->source_decimal, ==, expected_values[i]);
            g_assert_nonnull (point->coefficient);
            g_assert_null (point->missing_reason);
        }
    }

    /* The verified object owns an independent reconstruction, not the input. */
    atm_bnr_bls_series_contract_free (contract);
    g_assert_true (atm_bnr_bls_verified_series_validate (series, &error));
    g_assert_no_error (error);

    AtmBnrBlsVerifiedSeries *rebuilt = NULL;
    g_assert_true (atm_bnr_bls_verified_series_rebuild (
        series, &rebuilt, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_bnr_bls_verified_series_scientific_id (series), ==,
                     atm_bnr_bls_verified_series_scientific_id (rebuilt));
    g_assert_cmpstr (atm_bnr_bls_verified_series_qualified_id (series), ==,
                     atm_bnr_bls_verified_series_qualified_id (rebuilt));

    atm_bnr_bls_verified_series_free (rebuilt);
    atm_bnr_bls_verified_series_free (series);
}

static void
test_missing_payload_tampering_fails_closed (void)
{
    AtmBnrBlsSeriesContract *contract = build_contract ();
    AtmBnrBlsVerifiedSeries *series = NULL;
    GError *error = NULL;

    g_assert_true (atm_bnr_bls_verified_series_from_contract (
        contract, &series, &error));
    g_assert_no_error (error);
    atm_bnr_bls_series_contract_free (contract);

    AtmSeriesContract *mutable_contract = (AtmSeriesContract *)
        atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (series));
    AtmSeriesContractPoint *missing =
        g_ptr_array_index (mutable_contract->points, 10u);
    char *saved_reason = missing->missing_reason;
    missing->missing_reason = "invented";
    g_assert_false (atm_bnr_bls_verified_series_validate (series, NULL));
    missing->missing_reason = saved_reason;
    g_assert_true (atm_bnr_bls_verified_series_validate (series, &error));
    g_assert_no_error (error);

    AtmBnrBlsVerifiedSeries *untouched = NULL;
    g_assert_false (atm_bnr_bls_verified_series_from_contract (
        NULL, &untouched, NULL));
    g_assert_null (untouched);

    atm_bnr_bls_verified_series_free (series);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func (
        "/bnr-bls-verified-series/source-qualified-quarterly",
        test_source_qualified_quarterly_series
    );
    g_test_add_func (
        "/bnr-bls-verified-series/missing-tampering",
        test_missing_payload_tampering_fails_closed
    );
    return g_test_run ();
}
