#include "bnr_bls_series_contract.h"

#include <string.h>

static char *buffers[ATM_BNR_BLS_SOURCE_COUNT];
static gsize lengths[ATM_BNR_BLS_SOURCE_COUNT];
static GBytes *inputs[ATM_BNR_BLS_SOURCE_COUNT];

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

static void
load_sources (void)
{
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, fixture_names[i], NULL);
        GError *error = NULL;
        g_assert_true (g_file_get_contents (
            path, &buffers[i], &lengths[i], &error));
        g_assert_no_error (error);
        g_assert_nonnull (buffers[i]);
        inputs[i] = g_bytes_new_static (buffers[i], lengths[i]);
        g_free (path);
    }
}

static void
free_sources (void)
{
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        g_clear_pointer (&inputs[i], g_bytes_unref);
        g_clear_pointer (&buffers[i], g_free);
        lengths[i] = 0;
    }
}

static AtmBnrBlsPanel *
build_panel (void)
{
    AtmBnrBlsPanel *panel = NULL;
    GError *error = NULL;
    g_assert_true (atm_bnr_bls_panel_new (
        ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        ATM_BNR_BLS_SNAPSHOT_SHA,
        inputs,
        &panel,
        &error
    ));
    g_assert_no_error (error);
    g_assert_nonnull (panel);
    return panel;
}

static AtmBnrBlsSra *
build_sra (const AtmBnrBlsPanel *panel)
{
    AtmBnrBlsSra *sra = NULL;
    GError *error = NULL;
    g_assert_true (atm_bnr_bls_sra_new (panel, &sra, &error));
    g_assert_no_error (error);
    g_assert_nonnull (sra);
    return sra;
}

static const AtmSraEstablishedFact *
find_fact (const AtmSraResult *result, const char *fact_id)
{
    if (result == NULL || result->established_facts == NULL)
        return NULL;

    for (guint i = 0; i < result->established_facts->len; i++) {
        const AtmSraEstablishedFact *fact =
            g_ptr_array_index (result->established_facts, i);
        if (fact != NULL && g_strcmp0 (fact->fact_id, fact_id) == 0)
            return fact;
    }

    return NULL;
}

static gboolean
array_has (const GPtrArray *array, const char *value)
{
    if (array == NULL || value == NULL)
        return FALSE;

    for (guint i = 0; i < array->len; i++)
        if (g_strcmp0 (g_ptr_array_index ((GPtrArray *) array, i), value) == 0)
            return TRUE;

    return FALSE;
}

static void
assert_support_set_matches (
    const GPtrArray *actual,
    const GPtrArray *expected
)
{
    g_assert_nonnull (actual);
    g_assert_nonnull (expected);
    g_assert_cmpuint (actual->len, ==, expected->len);

    for (guint i = 0; i < actual->len; i++) {
        const char *id = g_ptr_array_index ((GPtrArray *) actual, i);
        g_assert_true (array_has (expected, id));
        for (guint j = 0; j < i; j++)
            g_assert_cmpstr (id, !=,
                g_ptr_array_index ((GPtrArray *) actual, j));
    }
}

static JsonObject *
parse_object (const char *json, JsonParser **out_parser)
{
    JsonParser *parser = json_parser_new ();
    GError *error = NULL;
    g_assert_true (json_parser_load_from_data (parser, json, -1, &error));
    g_assert_no_error (error);
    JsonNode *root = json_parser_get_root (parser);
    g_assert_true (JSON_NODE_HOLDS_OBJECT (root));
    *out_parser = parser;
    return json_node_get_object (root);
}

static gboolean
valid_sha256 (const char *text)
{
    if (text == NULL || strlen (text) != 64)
        return FALSE;

    for (guint i = 0; i < 64; i++)
        if (!(text[i] >= '0' && text[i] <= '9') &&
            !(text[i] >= 'a' && text[i] <= 'f'))
            return FALSE;

    return TRUE;
}

static void
test_source_bound_quarter_contract_and_rebuild (void)
{
    AtmBnrBlsPanel *panel = build_panel ();
    AtmBnrBlsSra *sra = build_sra (panel);
    AtmBnrBlsSeriesContract *series = NULL;
    GError *error = NULL;

    g_assert_true (atm_bnr_bls_series_contract_from_sra (
        panel, sra, &series, &error));
    g_assert_no_error (error);
    g_assert_nonnull (series);
    g_assert_true (atm_bnr_bls_series_contract_validate (series, &error));
    g_assert_no_error (error);

    const AtmSeriesContract *contract =
        atm_bnr_bls_series_contract_value (series);
    const AtmSraResult *result = atm_bnr_bls_sra_result (sra);
    const AtmSraEstablishedFact *missing =
        find_fact (result, "rmd:bnr-bls-c01:2025-Q2");

    g_assert_nonnull (contract);
    g_assert_nonnull (result);
    g_assert_nonnull (missing);
    g_assert_cmpuint (atm_bnr_bls_series_contract_count (series), ==, 12u);
    g_assert_cmpint (contract->x_kind, ==, ATM_SERIES_X_CALENDAR_QUARTER);
    g_assert_cmpstr (contract->subject, ==, "non_financial_corporations");
    g_assert_cmpstr (contract->attribute, ==, "nfc_credit_standards");
    g_assert_cmpstr (contract->x_unit, ==, "calendar_quarter");
    g_assert_cmpstr (contract->x_dimension, ==, "time");
    g_assert_cmpstr (contract->y_unit, ==, "percent");
    g_assert_cmpstr (contract->y_dimension, ==, "survey_net_percentage");
    g_assert_cmpstr (contract->series_semantics, ==,
                     "empirical_quarterly_observations");
    g_assert_cmpstr (contract->scenario, ==, "not_applicable");
    g_assert_cmpstr (contract->time_scope, ==, "2022-Q4..2025-Q3");
    g_assert_cmpstr (contract->epistemic_status, ==, "empirical");
    g_assert_cmpstr (contract->repository_id, ==, "rmd");
    g_assert_cmpstr (contract->repository_version, ==,
                     "snapshot:" ATM_BNR_BLS_SNAPSHOT_SHA);
    g_assert_cmpstr (contract->snapshot_sha, ==, ATM_BNR_BLS_SNAPSHOT_SHA);
    g_assert_cmpstr (contract->source_path, ==,
                     "data/processed/bnr_bls_realised_rounds.csv");
    g_assert_cmpstr (contract->profile_id, ==,
                     ATM_BNR_BLS_SERIES_PROFILE);
    g_assert_cmpstr (contract->profile_version, ==, "1");
    g_assert_true (valid_sha256 (
        atm_bnr_bls_series_contract_scientific_id (series)));
    g_assert_true (valid_sha256 (
        atm_bnr_bls_series_contract_qualified_id (series)));

    guint observation_index = 0;
    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        const AtmSeriesContractPoint *point =
            atm_bnr_bls_series_contract_point (series, i);
        g_assert_nonnull (point);
        g_assert_cmpuint (point->order, ==, i);
        g_assert_cmpstr (point->x, ==, expected_quarters[i]);
        g_assert_false (point->break_before);
        g_assert_null (point->break_reason);

        if (i == 10u) {
            g_assert_cmpint (point->y_status, ==, ATM_SERIES_Y_MISSING);
            g_assert_null (point->source_decimal);
            g_assert_null (point->coefficient);
            g_assert_cmpint (point->exponent, ==, 0);
            g_assert_false (point->negative_zero);
            g_assert_cmpstr (point->missing_reason, ==,
                             "source_round_not_retained");
            assert_support_set_matches (point->support, missing->support);
            continue;
        }

        g_assert_cmpint (point->y_status, ==, ATM_SERIES_Y_NUMERIC);
        g_assert_cmpstr (point->source_decimal, ==, expected_values[i]);
        g_assert_nonnull (point->coefficient);
        g_assert_null (point->missing_reason);
        g_assert_cmpint (point->exponent, <=, 0);

        const AtmBnrBlsObservation *unused = NULL;
        (void) unused;

        char *fact_id = g_strdup_printf (
            "rmd:bnr-bls-c01:%s",
            expected_quarters[i]
        );
        const AtmSraEstablishedFact *fact =
            find_fact (result, fact_id);
        g_free (fact_id);
        g_assert_nonnull (fact);
        g_assert_cmpuint (fact->support->len, ==, 6u);
        assert_support_set_matches (point->support, fact->support);
        for (guint j = 0; j < missing->support->len; j++)
            g_assert_true (array_has (
                point->support,
                g_ptr_array_index (missing->support, j)));

        JsonParser *parser = NULL;
        JsonObject *qualifiers = parse_object (fact->qualifiers_json, &parser);
        g_assert_cmpstr (json_object_get_string_member (
            qualifiers, "source_decimal"), ==, expected_values[i]);
        g_assert_cmpstr (json_object_get_string_member (
            qualifiers, "calendar_quarter"), ==, expected_quarters[i]);
        if (i == 2u || i == 6u) {
            JsonNode *date = json_object_get_member (qualifiers, "reference_date");
            g_assert_true (JSON_NODE_HOLDS_NULL (date));
        }
        g_object_unref (parser);
        observation_index++;
    }

    g_assert_cmpuint (observation_index, ==, 11u);

    AtmBnrBlsSeriesContract *rebuilt = NULL;
    g_assert_true (atm_bnr_bls_series_contract_rebuild (
        series, &rebuilt, &error));
    g_assert_no_error (error);
    g_assert_nonnull (rebuilt);
    g_assert_cmpstr (atm_bnr_bls_series_contract_scientific_id (rebuilt), ==,
                     atm_bnr_bls_series_contract_scientific_id (series));
    g_assert_cmpstr (atm_bnr_bls_series_contract_qualified_id (rebuilt), ==,
                     atm_bnr_bls_series_contract_qualified_id (series));
    g_assert_true (atm_bnr_bls_series_contract_validate (rebuilt, &error));
    g_assert_no_error (error);

    atm_bnr_bls_series_contract_free (rebuilt);
    atm_bnr_bls_series_contract_free (series);
    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
}

static void
test_mutated_series_point_is_rejected (void)
{
    AtmBnrBlsPanel *panel = build_panel ();
    AtmBnrBlsSra *sra = build_sra (panel);
    AtmBnrBlsSeriesContract *series = NULL;
    GError *error = NULL;

    g_assert_true (atm_bnr_bls_series_contract_from_sra (
        panel, sra, &series, &error));
    g_assert_no_error (error);

    AtmSeriesContract *mutable_contract = (AtmSeriesContract *)
        atm_bnr_bls_series_contract_value (series);
    AtmSeriesContractPoint *gap = g_ptr_array_index (
        mutable_contract->points, 10u);
    g_free (gap->missing_reason);
    gap->missing_reason = g_strdup ("caller-invented-reason");

    g_assert_false (atm_bnr_bls_series_contract_validate (series, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    AtmBnrBlsSeriesContract *rebuilt = NULL;
    g_assert_false (atm_bnr_bls_series_contract_rebuild (
        series, &rebuilt, &error));
    g_assert_nonnull (error);
    g_assert_null (rebuilt);
    g_clear_error (&error);

    atm_bnr_bls_series_contract_free (series);
    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
}

static void
test_sra_from_a_different_or_mutated_authority_is_rejected (void)
{
    AtmBnrBlsPanel *panel = build_panel ();
    AtmBnrBlsSra *sra = build_sra (panel);
    AtmBnrBlsSeriesContract *series = NULL;
    GError *error = NULL;

    AtmSraResult *result = (AtmSraResult *)
        atm_bnr_bls_sra_result (sra);
    AtmSraEstablishedFact *missing = (AtmSraEstablishedFact *)
        find_fact (result, "rmd:bnr-bls-c01:2025-Q2");
    g_assert_nonnull (missing);
    g_free (missing->value);
    missing->value = g_strdup ("0");

    g_assert_false (atm_bnr_bls_series_contract_from_sra (
        panel, sra, &series, &error));
    g_assert_nonnull (error);
    g_assert_null (series);
    g_clear_error (&error);

    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
}

static void
test_invalid_arguments_fail_closed (void)
{
    AtmBnrBlsSeriesContract *series = NULL;
    GError *error = NULL;

    g_assert_false (atm_bnr_bls_series_contract_from_sra (
        NULL, NULL, &series, &error));
    g_assert_error (error, ATM_BNR_BLS_SERIES_CONTRACT_ERROR,
                    (gint) ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT);
    g_assert_null (series);
    g_clear_error (&error);

    AtmBnrBlsPanel *panel = build_panel ();
    AtmBnrBlsSra *sra = build_sra (panel);
    g_assert_false (atm_bnr_bls_series_contract_from_sra (
        panel, sra, NULL, &error));
    g_assert_error (error, ATM_BNR_BLS_SERIES_CONTRACT_ERROR,
                    (gint) ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT);
    g_clear_error (&error);

    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    load_sources ();

    g_test_add_func (
        "/bnr-bls-series-contract/source-bound-quarter-contract",
        test_source_bound_quarter_contract_and_rebuild);
    g_test_add_func (
        "/bnr-bls-series-contract/mutated-point-is-rejected",
        test_mutated_series_point_is_rejected);
    g_test_add_func (
        "/bnr-bls-series-contract/mutated-authority-is-rejected",
        test_sra_from_a_different_or_mutated_authority_is_rejected);
    g_test_add_func (
        "/bnr-bls-series-contract/invalid-arguments",
        test_invalid_arguments_fail_closed);

    int result = g_test_run ();
    free_sources ();
    return result;
}
