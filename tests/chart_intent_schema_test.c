#include "chart_intent_schema.h"

#include <string.h>

static gchar *
make_json (
    const gchar *schema,
    const gchar *intent,
    const gchar *series_json,
    const gchar *kind_json,
    const gchar *response_mode,
    const gchar *language,
    const gchar *extra_member
)
{
    return g_strdup_printf (
        "{\"schema\":\"%s\",\"intent\":\"%s\",\"series_id\":%s,"
        "\"chart_kind\":%s,\"response_mode\":\"%s\",\"language\":\"%s\"%s}",
        schema, intent, series_json, kind_json, response_mode, language,
        extra_member != NULL ? extra_member : "");
}

static void
assert_valid (
    const gchar *json,
    AtmChartIntentKind expected_intent,
    const gchar *expected_series,
    gboolean expected_has_kind,
    AtmChartIntentResponseMode expected_mode,
    AtmChartIntentLanguage expected_language
)
{
    AtmChartIntentProposal proposal = { 0 };
    GError *error = NULL;

    g_assert_true (atm_chart_intent_proposal_parse (
        json, strlen (json), &proposal, &error));
    g_assert_no_error (error);
    g_assert_cmpint (proposal.intent, ==, expected_intent);
    g_assert_cmpstr (proposal.series_id, ==, expected_series);
    g_assert_cmpint (proposal.has_chart_kind, ==, expected_has_kind);
    if (expected_has_kind)
        g_assert_true (proposal.chart_kind_line);
    else
        g_assert_false (proposal.chart_kind_line);
    g_assert_cmpint (proposal.response_mode, ==, expected_mode);
    g_assert_cmpint (proposal.language, ==, expected_language);

    atm_chart_intent_proposal_clear (&proposal);
    g_assert_null (proposal.series_id);
}

static void
assert_invalid (
    const gchar *json,
    gsize length,
    AtmChartIntentSchemaError expected_error
)
{
    AtmChartIntentProposal proposal = { 0 };
    GError *error = NULL;

    g_assert_false (atm_chart_intent_proposal_parse (
        json, length, &proposal, &error));
    g_assert_error (error, ATM_CHART_INTENT_SCHEMA_ERROR, expected_error);
    g_clear_error (&error);
    g_assert_null (proposal.series_id);
    g_assert_false (proposal.has_chart_kind);
}

static void
test_valid_registered_chart_profiles (void)
{
    gchar *gistemp = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"climate.gistemp.global-temperature-anomaly\"", "\"line\"",
        "chart_only", "en", NULL);
    assert_valid (gistemp, ATM_CHART_INTENT_CHART,
        "climate.gistemp.global-temperature-anomaly", TRUE,
        ATM_CHART_INTENT_RESPONSE_CHART_ONLY, ATM_CHART_INTENT_LANGUAGE_EN);
    g_free (gistemp);

    gchar *energy = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"energy.energy-institute.global-total-energy-supply\"", "\"line\"",
        "chart_with_interpretation", "en", NULL);
    assert_valid (energy, ATM_CHART_INTENT_CHART,
        "energy.energy-institute.global-total-energy-supply", TRUE,
        ATM_CHART_INTENT_RESPONSE_CHART_WITH_INTERPRETATION,
        ATM_CHART_INTENT_LANGUAGE_EN);
    g_free (energy);

    gchar *bnr = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"banking.bnr.bls-c01.credit-standards-net-percentage\"", "\"line\"",
        "chart_only", "ro", NULL);
    assert_valid (bnr, ATM_CHART_INTENT_CHART,
        "banking.bnr.bls-c01.credit-standards-net-percentage", TRUE,
        ATM_CHART_INTENT_RESPONSE_CHART_ONLY, ATM_CHART_INTENT_LANGUAGE_RO);
    g_free (bnr);
}

static void
test_valid_non_chart_modes (void)
{
    gchar *interpretation = make_json (ATM_CHART_INTENT_SCHEMA_ID, "interpretation",
        "\"climate.gistemp.global-temperature-anomaly\"", "null",
        "none", "ro", NULL);
    assert_valid (interpretation, ATM_CHART_INTENT_INTERPRETATION,
        "climate.gistemp.global-temperature-anomaly", FALSE,
        ATM_CHART_INTENT_RESPONSE_NONE, ATM_CHART_INTENT_LANGUAGE_RO);
    g_free (interpretation);

    gchar *chart_and_interpretation = make_json (ATM_CHART_INTENT_SCHEMA_ID,
        "interpretation", "\"banking.bnr.bls-c01.credit-standards-net-percentage\"",
        "\"line\"", "chart_with_interpretation", "en", NULL);
    assert_valid (chart_and_interpretation, ATM_CHART_INTENT_INTERPRETATION,
        "banking.bnr.bls-c01.credit-standards-net-percentage", TRUE,
        ATM_CHART_INTENT_RESPONSE_CHART_WITH_INTERPRETATION,
        ATM_CHART_INTENT_LANGUAGE_EN);
    g_free (chart_and_interpretation);

    gchar *data = make_json (ATM_CHART_INTENT_SCHEMA_ID, "data_extract",
        "\"energy.energy-institute.global-total-energy-supply\"", "null",
        "data_extract", "other", NULL);
    assert_valid (data, ATM_CHART_INTENT_DATA_EXTRACT,
        "energy.energy-institute.global-total-energy-supply", FALSE,
        ATM_CHART_INTENT_RESPONSE_DATA_EXTRACT, ATM_CHART_INTENT_LANGUAGE_OTHER);
    g_free (data);

    gchar *not_chart = make_json (ATM_CHART_INTENT_SCHEMA_ID, "not_chart",
        "null", "null", "none", "en", NULL);
    assert_valid (not_chart, ATM_CHART_INTENT_NOT_CHART, NULL, FALSE,
        ATM_CHART_INTENT_RESPONSE_NONE, ATM_CHART_INTENT_LANGUAGE_EN);
    g_free (not_chart);

    gchar *ambiguous = make_json (ATM_CHART_INTENT_SCHEMA_ID, "ambiguous",
        "null", "null", "none", "ro", NULL);
    assert_valid (ambiguous, ATM_CHART_INTENT_AMBIGUOUS, NULL, FALSE,
        ATM_CHART_INTENT_RESPONSE_NONE, ATM_CHART_INTENT_LANGUAGE_RO);
    g_free (ambiguous);
}

static void
test_invalid_json_and_root (void)
{
    const gchar *invalid = "{not-json";
    assert_invalid (invalid, strlen (invalid),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_JSON);

    const gchar *array = "[]";
    assert_invalid (array, strlen (array),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE);

    const gchar *trailing = "{\"schema\":\"atm-chart-intent/1\"} garbage";
    assert_invalid (trailing, strlen (trailing),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_JSON);
}

static void
test_required_and_additional_fields (void)
{
    const gchar *missing =
        "{\"schema\":\"atm-chart-intent/1\",\"intent\":\"not_chart\","
        "\"series_id\":null,\"chart_kind\":null,\"response_mode\":\"none\"}";
    assert_invalid (missing, strlen (missing),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE);

    gchar *extra = make_json (ATM_CHART_INTENT_SCHEMA_ID, "not_chart",
        "null", "null", "none", "en", ",\"extra\":true");
    assert_invalid (extra, strlen (extra),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE);
    g_free (extra);
}

static void
test_schema_and_enum_validation (void)
{
    gchar *wrong_schema = make_json ("atm-chart-intent/99", "not_chart",
        "null", "null", "none", "en", NULL);
    assert_invalid (wrong_schema, strlen (wrong_schema),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (wrong_schema);

    gchar *wrong_intent = make_json (ATM_CHART_INTENT_SCHEMA_ID, "plot_anything",
        "null", "null", "none", "en", NULL);
    assert_invalid (wrong_intent, strlen (wrong_intent),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (wrong_intent);

    gchar *wrong_response = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"climate.gistemp.global-temperature-anomaly\"", "\"line\"",
        "render_anything", "en", NULL);
    assert_invalid (wrong_response, strlen (wrong_response),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (wrong_response);

    gchar *wrong_language = make_json (ATM_CHART_INTENT_SCHEMA_ID, "not_chart",
        "null", "null", "none", "english", NULL);
    assert_invalid (wrong_language, strlen (wrong_language),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (wrong_language);

    gchar *wrong_kind = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"climate.gistemp.global-temperature-anomaly\"", "\"scatter\"",
        "chart_only", "en", NULL);
    assert_invalid (wrong_kind, strlen (wrong_kind),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (wrong_kind);
}

static void
test_series_allowlist_and_types (void)
{
    gchar *unknown = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"population.anywhere\"", "\"line\"", "chart_only", "en", NULL);
    assert_invalid (unknown, strlen (unknown),
        ATM_CHART_INTENT_SCHEMA_ERROR_UNKNOWN_SERIES);
    g_free (unknown);

    gchar *number = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "3", "\"line\"", "chart_only", "en", NULL);
    assert_invalid (number, strlen (number),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (number);

    gchar *empty = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"\"", "\"line\"", "chart_only", "en", NULL);
    assert_invalid (empty, strlen (empty),
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD);
    g_free (empty);
}

static void
test_inconsistent_combinations (void)
{
    gchar *chart_without_series = make_json (ATM_CHART_INTENT_SCHEMA_ID,
        "chart", "null", "\"line\"", "chart_only", "en", NULL);
    assert_invalid (chart_without_series, strlen (chart_without_series),
        ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT);
    g_free (chart_without_series);

    gchar *chart_wrong_mode = make_json (ATM_CHART_INTENT_SCHEMA_ID, "chart",
        "\"climate.gistemp.global-temperature-anomaly\"", "\"line\"",
        "data_extract", "en", NULL);
    assert_invalid (chart_wrong_mode, strlen (chart_wrong_mode),
        ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT);
    g_free (chart_wrong_mode);

    gchar *not_chart_with_series = make_json (ATM_CHART_INTENT_SCHEMA_ID,
        "not_chart", "\"climate.gistemp.global-temperature-anomaly\"",
        "null", "none", "en", NULL);
    assert_invalid (not_chart_with_series, strlen (not_chart_with_series),
        ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT);
    g_free (not_chart_with_series);

    gchar *interpretation_without_kind = make_json (ATM_CHART_INTENT_SCHEMA_ID,
        "interpretation", "\"climate.gistemp.global-temperature-anomaly\"",
        "null", "chart_with_interpretation", "en", NULL);
    assert_invalid (interpretation_without_kind, strlen (interpretation_without_kind),
        ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT);
    g_free (interpretation_without_kind);

    gchar *data_with_line = make_json (ATM_CHART_INTENT_SCHEMA_ID, "data_extract",
        "\"energy.energy-institute.global-total-energy-supply\"", "\"line\"",
        "data_extract", "en", NULL);
    assert_invalid (data_with_line, strlen (data_with_line),
        ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT);
    g_free (data_with_line);
}

static void
test_size_bound_and_arguments (void)
{
    gchar *large = g_malloc0 (ATM_CHART_INTENT_JSON_MAX_BYTES + 2);
    memset (large, ' ', ATM_CHART_INTENT_JSON_MAX_BYTES + 1);
    large[ATM_CHART_INTENT_JSON_MAX_BYTES + 1] = '\0';
    assert_invalid (large, ATM_CHART_INTENT_JSON_MAX_BYTES + 1,
        ATM_CHART_INTENT_SCHEMA_ERROR_TOO_LARGE);
    g_free (large);

    AtmChartIntentProposal proposal = { 0 };
    GError *error = NULL;
    g_assert_false (atm_chart_intent_proposal_parse (
        NULL, 0, &proposal, &error));
    g_assert_error (error, ATM_CHART_INTENT_SCHEMA_ERROR,
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_chart_intent_proposal_parse (
        "{}", 2, NULL, &error));
    g_assert_error (error, ATM_CHART_INTENT_SCHEMA_ERROR,
        ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/chart-intent-schema/registered-chart-profiles",
        test_valid_registered_chart_profiles);
    g_test_add_func ("/chart-intent-schema/non-chart-modes",
        test_valid_non_chart_modes);
    g_test_add_func ("/chart-intent-schema/invalid-json-root",
        test_invalid_json_and_root);
    g_test_add_func ("/chart-intent-schema/required-additional-fields",
        test_required_and_additional_fields);
    g_test_add_func ("/chart-intent-schema/schema-enums",
        test_schema_and_enum_validation);
    g_test_add_func ("/chart-intent-schema/series-allowlist-types",
        test_series_allowlist_and_types);
    g_test_add_func ("/chart-intent-schema/inconsistent-combinations",
        test_inconsistent_combinations);
    g_test_add_func ("/chart-intent-schema/size-bound-arguments",
        test_size_bound_and_arguments);

    return g_test_run ();
}
