#include "bnr_bls_sra.h"

#include <json-glib/json-glib.h>
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
    "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3", "2023-Q4",
    "2024-Q1", "2024-Q2", "2024-Q3", "2024-Q4", "2025-Q1", "2025-Q3"
};

static const char *const expected_values[] = {
    "11.89", "16.34", "15.54", "0.0", "0.0", "0.0",
    "0.0", "12.54", "12.08", "11.71", "9.46"
};

static const char *const expected_digests[ATM_BNR_BLS_SOURCE_COUNT] = {
    "c5dad2c9577431e9765bc02b166e8d5a2b4c4b4c8e761fbb5bbce50f4e6498ff",
    "9fd12a73499c51d6be200c3e6fba467fc4f0e8dbff33b946fcca8a4bec5b2bf9",
    "2df50e5c1ab64032c449b6c1fbdc3c93c3e5622e6e221fd716f22fcf337aa380",
    "818e16c4ec0598a7c1410044b74c30bc3744cdb5533dba606a9904bb9420b07a",
    "679ef4f2e401d27ebb9eadadc20a603b4ce57e9296c9fd9bc695a0999b515b8c"
};

static void
load_sources (void)
{
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, fixture_names[i], NULL);
        GError *error = NULL;
        g_assert_true (g_file_get_contents (path, &buffers[i], &lengths[i], &error));
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
build_sra (void)
{
    AtmBnrBlsPanel *panel = build_panel ();
    AtmBnrBlsSra *sra = NULL;
    GError *error = NULL;
    g_assert_true (atm_bnr_bls_sra_new (panel, &sra, &error));
    g_assert_no_error (error);
    g_assert_nonnull (sra);
    /* SRA owns an independently reconstructed five-file admission. */
    atm_bnr_bls_panel_free (panel);
    g_assert_true (atm_bnr_bls_sra_validate (sra, &error));
    g_assert_no_error (error);
    return sra;
}

static const AtmSraEstablishedFact *
find_fact (const AtmSraResult *result, const char *fact_id)
{
    for (guint i = 0; i < result->established_facts->len; i++) {
        const AtmSraEstablishedFact *fact =
            g_ptr_array_index (result->established_facts, i);
        if (g_strcmp0 (fact->fact_id, fact_id) == 0)
            return fact;
    }
    return NULL;
}

static gboolean
array_has_string (const GPtrArray *array, const char *value)
{
    if (array == NULL || value == NULL)
        return FALSE;
    for (guint i = 0; i < array->len; i++)
        if (g_strcmp0 (g_ptr_array_index ((GPtrArray *) array, i), value) == 0)
            return TRUE;
    return FALSE;
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

static void
assert_fact_support_is_qualified (
    const AtmSraResult *result,
    const AtmSraEstablishedFact *fact,
    guint expected_count
)
{
    g_assert_nonnull (fact);
    g_assert_nonnull (fact->support);
    g_assert_cmpuint (fact->support->len, ==, expected_count);
    for (guint i = 0; i < fact->support->len; i++) {
        const char *id = g_ptr_array_index (fact->support, i);
        g_assert_true (array_has_string (
            result->qualification->evidence_atom_ids, id));
        for (guint j = 0; j < i; j++)
            g_assert_cmpstr (id, !=, g_ptr_array_index (fact->support, j));
    }
}

static void
test_sra_reconstructs_eleven_values_and_one_authorized_gap (void)
{
    AtmBnrBlsSra *sra = build_sra ();
    const AtmSraResult *result = atm_bnr_bls_sra_result (sra);
    g_assert_nonnull (result);
    g_assert_true (result->finalized);
    g_assert_cmpint (result->answerability, ==, ATM_SRA_PARTIAL);
    g_assert_cmpuint (result->established_facts->len, ==, 12u);
    g_assert_cmpuint (result->derived_facts->len, ==, 0u);
    g_assert_cmpuint (result->qualification->evidence_atom_ids->len, ==, 16u);
    g_assert_cmpuint (result->qualification->control_evidence_ids->len, ==, 0u);
    g_assert_cmpuint (result->qualification->operations->len, ==, 0u);
    g_assert_cmpstr (result->qualification->numeric_profile, ==,
                     "exact_decimal_coefficient_exponent/1");
    g_assert_cmpuint (result->limitations->len, ==, 6u);

    const AtmSraEstablishedFact *missing =
        find_fact (result, "rmd:bnr-bls-c01:2025-Q2");
    g_assert_nonnull (missing);
    g_assert_cmpstr (missing->fact_type, ==,
                     "rmd.bnr_bls_c01_missing_observation");
    g_assert_cmpstr (missing->value, ==, "MISSING");
    g_assert_cmpstr (missing->unit, ==, "percent");
    g_assert_cmpstr (missing->dimension, ==, "survey_net_percentage");
    assert_fact_support_is_qualified (result, missing, 5u);

    JsonParser *missing_parser = NULL;
    JsonObject *missing_json = parse_object (
        missing->qualifiers_json, &missing_parser);
    g_assert_cmpstr (json_object_get_string_member (
        missing_json, "calendar_quarter"), ==, "2025-Q2");
    g_assert_cmpstr (json_object_get_string_member (
        missing_json, "y_status"), ==, "MISSING");
    g_assert_cmpstr (json_object_get_string_member (
        missing_json, "missing_reason"), ==, "source_round_not_retained");
    g_assert_cmpstr (json_object_get_string_member (
        missing_json, "schema"), ==, "atm-bnr-bls-c01-missing-observation/1");
    g_assert_false (json_object_has_member (missing_json, "source_row"));

    JsonArray *paths = json_object_get_array_member (
        missing_json, "authority_source_paths");
    JsonArray *digests = json_object_get_array_member (
        missing_json, "authority_source_sha256");
    g_assert_cmpuint (json_array_get_length (paths), ==, ATM_BNR_BLS_SOURCE_COUNT);
    g_assert_cmpuint (json_array_get_length (digests), ==, ATM_BNR_BLS_SOURCE_COUNT);
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++)
        g_assert_cmpstr (json_array_get_string_element (digests, i), ==,
                         expected_digests[i]);
    g_object_unref (missing_parser);

    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        char *fact_id = g_strdup_printf (
            "rmd:bnr-bls-c01:%s", expected_quarters[i]);
        const AtmSraEstablishedFact *fact = find_fact (result, fact_id);
        g_assert_nonnull (fact);
        g_assert_cmpstr (fact->fact_type, ==, "rmd.bnr_bls_c01_observation");
        g_assert_cmpstr (fact->unit, ==, "percent");
        g_assert_cmpstr (fact->dimension, ==, "survey_net_percentage");
        g_assert_true (g_str_has_suffix (fact->value, "e0") ||
                       strstr (fact->value, "e-") != NULL ||
                       strstr (fact->value, "e+") != NULL);
        assert_fact_support_is_qualified (result, fact, 6u);

        JsonParser *parser = NULL;
        JsonObject *payload = parse_object (fact->qualifiers_json, &parser);
        g_assert_cmpstr (json_object_get_string_member (
            payload, "schema"), ==, "atm-bnr-bls-c01-observation/1");
        g_assert_cmpstr (json_object_get_string_member (
            payload, "calendar_quarter"), ==, expected_quarters[i]);
        g_assert_cmpstr (json_object_get_string_member (
            payload, "source_decimal"), ==, expected_values[i]);
        g_assert_cmpstr (json_object_get_string_member (
            payload, "y_status"), ==, "NUMERIC");
        char *expected_row = g_strdup_printf ("%u", i + 2u);
        g_assert_cmpstr (json_object_get_string_member (
            payload, "source_row"), ==, expected_row);
        g_free (expected_row);

        JsonNode *reference_date = json_object_get_member (
            payload, "reference_date");
        if (i == 2u || i == 6u) {
            g_assert_true (JSON_NODE_HOLDS_NULL (reference_date));
        } else {
            g_assert_true (JSON_NODE_HOLDS_VALUE (reference_date));
            g_assert_cmpstr (json_node_get_string (reference_date), !=, "");
        }
        g_object_unref (parser);
        g_free (fact_id);
    }

    g_assert_true (atm_sra_result_validate (result, NULL));
    atm_bnr_bls_sra_free (sra);
}

static void
test_reconstruction_identity_is_deterministic (void)
{
    AtmBnrBlsSra *first = build_sra ();
    AtmBnrBlsSra *second = build_sra ();
    const AtmSraResult *a = atm_bnr_bls_sra_result (first);
    const AtmSraResult *b = atm_bnr_bls_sra_result (second);
    g_assert_cmpstr (a->qualification->scientific_content_id, ==,
                     b->qualification->scientific_content_id);
    g_assert_cmpstr (a->qualification->qualified_artifact_id, ==,
                     b->qualification->qualified_artifact_id);
    atm_bnr_bls_sra_free (first);
    atm_bnr_bls_sra_free (second);
}

static void
test_refinalized_fact_forgery_is_rejected (void)
{
    AtmBnrBlsSra *sra = build_sra ();
    AtmSraResult *result = (AtmSraResult *) atm_bnr_bls_sra_result (sra);
    AtmSraEstablishedFact *missing = (AtmSraEstablishedFact *)
        find_fact (result, "rmd:bnr-bls-c01:2025-Q2");
    g_assert_nonnull (missing);

    g_free (missing->value);
    missing->value = g_strdup ("0");
    result->finalized = FALSE;
    g_clear_pointer (&result->qualification->scientific_content_id, g_free);
    g_clear_pointer (&result->qualification->qualified_artifact_id, g_free);
    GError *error = NULL;
    g_assert_true (atm_sra_result_finalize (result, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_validate (result, &error));
    g_assert_no_error (error);

    g_assert_false (atm_bnr_bls_sra_validate (sra, &error));
    g_assert_error (error, ATM_BNR_BLS_SRA_ERROR, ATM_BNR_BLS_SRA_ERROR_IDENTITY);
    g_clear_error (&error);
    atm_bnr_bls_sra_free (sra);
}

static void
test_invalid_arguments_fail_closed (void)
{
    AtmBnrBlsSra *sra = NULL;
    GError *error = NULL;
    g_assert_false (atm_bnr_bls_sra_new (NULL, &sra, &error));
    g_assert_error (error, ATM_BNR_BLS_SRA_ERROR, ATM_BNR_BLS_SRA_ERROR_ARGUMENT);
    g_assert_null (sra);
    g_clear_error (&error);

    AtmBnrBlsPanel *panel = build_panel ();
    g_assert_false (atm_bnr_bls_sra_new (panel, NULL, &error));
    g_assert_error (error, ATM_BNR_BLS_SRA_ERROR, ATM_BNR_BLS_SRA_ERROR_ARGUMENT);
    g_clear_error (&error);
    atm_bnr_bls_panel_free (panel);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    load_sources ();

    g_test_add_func (
        "/bnr-bls-sra/eleven-numeric-and-one-authorized-missing",
        test_sra_reconstructs_eleven_values_and_one_authorized_gap);
    g_test_add_func (
        "/bnr-bls-sra/deterministic-identities",
        test_reconstruction_identity_is_deterministic);
    g_test_add_func (
        "/bnr-bls-sra/refinalized-fact-forgery",
        test_refinalized_fact_forgery_is_rejected);
    g_test_add_func (
        "/bnr-bls-sra/invalid-arguments",
        test_invalid_arguments_fail_closed);

    int result = g_test_run ();
    free_sources ();
    return result;
}
