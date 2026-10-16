#include "chart_catalog.h"

#include <string.h>

static const AtmChartCatalogEntry *
entry (const char *id)
{
    return atm_chart_catalog_find_id (id);
}

static void
test_catalog_validates (void)
{
    GError *error = NULL;
    g_assert_true (atm_chart_catalog_validate (&error));
    g_assert_no_error (error);

    gsize count = 0;
    const AtmChartCatalogEntry *entries = atm_chart_catalog_entries (&count);
    g_assert_nonnull (entries);
    g_assert_cmpuint (count, ==, 3);
}

static void
test_stable_ids_and_profiles (void)
{
    const AtmChartCatalogEntry *gistemp =
        entry ("climate.gistemp.global-temperature-anomaly");
    const AtmChartCatalogEntry *ei =
        entry ("energy.energy-institute.global-total-energy-supply");
    const AtmChartCatalogEntry *bnr =
        entry ("banking.bnr.bls-c01.credit-standards-net-percentage");

    g_assert_nonnull (gistemp);
    g_assert_nonnull (ei);
    g_assert_nonnull (bnr);
    g_assert_null (entry ("population.over.time"));
    g_assert_cmpstr (gistemp->chart_spec_schema, ==, "atm-chart-spec/1");
    g_assert_cmpstr (ei->chart_spec_schema, ==, "atm-chart-spec/2");
    g_assert_cmpstr (bnr->chart_spec_schema, ==, "atm-chart-spec/3");
    g_assert_cmpstr (gistemp->series_profile_id, ==,
                     "atm-series/gistemp-complete-annual/1");
    g_assert_cmpstr (ei->series_profile_id, ==,
                     "atm-series/energy-institute-tes-complete-annual/1");
    g_assert_cmpstr (bnr->series_profile_id, ==,
                     "atm-series/bnr-bls-c01-calendar-quarter/1");
    g_assert_cmpstr (gistemp->history_schema_version, ==, "5");
    g_assert_cmpstr (ei->history_schema_version, ==, "5");
    g_assert_cmpstr (bnr->history_schema_version, ==, "5");
}

static void
assert_alias (const char *query, const char *expected_id)
{
    const AtmChartCatalogEntry *found = NULL;
    AtmChartCatalogLookupResult result =
        atm_chart_catalog_find_alias (query, &found);
    g_assert_cmpint (result, ==, ATM_CHART_CATALOG_LOOKUP_FOUND);
    g_assert_nonnull (found);
    g_assert_cmpstr (found->series_id, ==, expected_id);
}

static void
test_alias_normalization (void)
{
    assert_alias ("  GISTEMP  ",
                  "climate.gistemp.global-temperature-anomaly");
    assert_alias ("ANOMALIA TEMPERATURII GLOBALE",
                  "climate.gistemp.global-temperature-anomaly");
    assert_alias ("FURNIZAREA TOTALĂ GLOBALĂ DE ENERGIE",
                  "energy.energy-institute.global-total-energy-supply");
    assert_alias ("STANDARDele DE CREDIT",
                  "banking.bnr.bls-c01.credit-standards-net-percentage");

    const AtmChartCatalogEntry *found = NULL;
    g_assert_cmpint (atm_chart_catalog_find_alias ("unknown series", &found),
                     ==, ATM_CHART_CATALOG_LOOKUP_NOT_FOUND);
    g_assert_null (found);
    g_assert_cmpint (atm_chart_catalog_find_alias ("   ", &found),
                     ==, ATM_CHART_CATALOG_LOOKUP_INVALID);
    g_assert_null (found);
    g_assert_cmpint (atm_chart_catalog_find_alias ("gistemp", NULL),
                     ==, ATM_CHART_CATALOG_LOOKUP_INVALID);
}

static void
test_snapshot_allowlists (void)
{
    const AtmChartCatalogEntry *gistemp =
        entry ("climate.gistemp.global-temperature-anomaly");
    const AtmChartCatalogEntry *ei =
        entry ("energy.energy-institute.global-total-energy-supply");
    const AtmChartCatalogEntry *bnr =
        entry ("banking.bnr.bls-c01.credit-standards-net-percentage");
    const char *old_ewd = "d9e249339663015f6d1c05752338a955bf64ad0b";
    const char *new_ewd = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    const char *rmd = "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170";

    g_assert_true (atm_chart_catalog_snapshot_admitted (gistemp, old_ewd));
    g_assert_true (atm_chart_catalog_snapshot_admitted (gistemp, new_ewd));
    g_assert_true (atm_chart_catalog_snapshot_admitted (ei, old_ewd));
    g_assert_true (atm_chart_catalog_snapshot_admitted (ei, new_ewd));
    g_assert_true (atm_chart_catalog_snapshot_admitted (bnr, rmd));

    g_assert_false (atm_chart_catalog_snapshot_admitted (gistemp, rmd));
    g_assert_false (atm_chart_catalog_snapshot_admitted (ei, rmd));
    g_assert_false (atm_chart_catalog_snapshot_admitted (bnr, old_ewd));
    g_assert_false (atm_chart_catalog_snapshot_admitted (bnr, "D9E249339663015F6D1C05752338A955BF64AD0B"));
    g_assert_false (atm_chart_catalog_snapshot_admitted (bnr, "not-a-snapshot"));
    g_assert_false (atm_chart_catalog_snapshot_admitted (NULL, rmd));

    /* A caller-crafted descriptor must not expand the compiled-in allowlist. */
    AtmChartCatalogEntry forged = *gistemp;
    const char *forged_snapshot[] = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    };
    forged.snapshot_shas = forged_snapshot;
    forged.snapshot_count = G_N_ELEMENTS (forged_snapshot);
    g_assert_false (atm_chart_catalog_snapshot_admitted (&forged, forged_snapshot[0]));
}

static void
test_source_semantics_and_provenance (void)
{
    const AtmChartCatalogEntry *gistemp =
        entry ("climate.gistemp.global-temperature-anomaly");
    const AtmChartCatalogEntry *ei =
        entry ("energy.energy-institute.global-total-energy-supply");
    const AtmChartCatalogEntry *bnr =
        entry ("banking.bnr.bls-c01.credit-standards-net-percentage");

    g_assert_cmpstr (gistemp->subject, ==, "global_surface_temperature");
    g_assert_cmpstr (gistemp->attribute, ==, "annual_temperature_anomaly");
    g_assert_cmpstr (gistemp->reference_period, ==, "1951-1980");
    g_assert_cmpstr (gistemp->coverage_start, ==, "1880");
    g_assert_cmpstr (gistemp->coverage_end, ==, "2025");
    g_assert_cmpuint (gistemp->coordinate_count, ==, 146);
    g_assert_cmpuint (gistemp->numeric_observation_count, ==, 146);
    g_assert_cmpstr (gistemp->epistemic_status, ==, "empirical");
    g_assert_cmpstr (gistemp->y_unit, ==, "degree_Celsius_anomaly");
    g_assert_cmpstr (gistemp->publisher_name, ==, "NASA GISS / GISTEMP v4");
    g_assert_true (g_str_has_prefix (gistemp->publisher_url, "https://"));
    g_assert_nonnull (strstr (gistemp->pinned_artifact_description, "processed"));

    g_assert_cmpstr (ei->subject, ==, "global_total_energy_supply");
    g_assert_cmpstr (ei->x_coordinate_semantics, ==, "calendar_year");
    g_assert_cmpstr (ei->x_unit, ==, "calendar_year");
    g_assert_cmpstr (ei->x_dimension, ==, "time");
    g_assert_cmpstr (ei->attribute, ==, "total_energy_supply");
    g_assert_cmpstr (ei->y_unit, ==, "exajoules/year");
    g_assert_cmpstr (ei->y_dimension, ==, "energy_flow_rate");
    g_assert_cmpstr (ei->coverage_start, ==, "1965");
    g_assert_cmpstr (ei->coverage_end, ==, "2025");
    g_assert_cmpuint (ei->coordinate_count, ==, 61);
    g_assert_cmpuint (ei->numeric_observation_count, ==, 61);
    g_assert_cmpstr (ei->publisher_name, ==,
                     "Energy Institute, Statistical Review of World Energy 2026");
    g_assert_nonnull (strstr (ei->observation_meaning, "Physical Energy Content"));
    g_assert_nonnull (strstr (ei->reconstruction_scope, "not independently reproduce"));

    g_assert_cmpstr (bnr->subject, ==, "non_financial_corporations");
    g_assert_cmpstr (bnr->attribute, ==, "nfc_credit_standards");
    g_assert_cmpstr (bnr->y_unit, ==, "percent");
    g_assert_cmpstr (bnr->y_dimension, ==, "survey_net_percentage");
    g_assert_cmpint (bnr->missing_policy, ==,
                     ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE);
    g_assert_cmpstr (bnr->coverage_start, ==, "2022-Q4");
    g_assert_cmpstr (bnr->coverage_end, ==, "2025-Q3");
    g_assert_cmpuint (bnr->coordinate_count, ==, 12);
    g_assert_cmpuint (bnr->numeric_observation_count, ==, 11);
    g_assert_cmpstr (bnr->missing_coordinate, ==, "2025-Q2");
    g_assert_cmpstr (bnr->missing_reason, ==, "source_round_not_retained");
    g_assert_cmpstr (bnr->publisher_name, ==,
                     "National Bank of Romania, Bank Lending Survey (BLS)");
    g_assert_true (g_str_has_prefix (bnr->publisher_url, "https://"));
}

static void
test_catalog_does_not_promote_arbitrary_columns (void)
{
    g_assert_null (entry ("ewd.population"));
    g_assert_null (entry ("population"));
    g_assert_null (entry ("empirical_model_inputs_2026-08-28.population"));
    g_assert_null (entry ("food_production_index"));
}


static void
test_request_alias_phrase_boundaries (void)
{
    gboolean ambiguous = FALSE;
    char *series_id = atm_chart_catalog_match_request_series_id (
        "Generate a chart of global temperature anomalies over time.",
        &ambiguous);
    g_assert_false (ambiguous);
    g_assert_cmpstr (series_id, ==,
                     "climate.gistemp.global-temperature-anomaly");
    g_free (series_id);

    series_id = atm_chart_catalog_match_request_series_id (
        "Show a chart of total energy supply (TES).", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_cmpstr (series_id, ==,
                     "energy.energy-institute.global-total-energy-supply");
    g_free (series_id);

    series_id = atm_chart_catalog_match_request_series_id (
        "Generate a chart of credit standards over time.", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_cmpstr (series_id, ==,
                     "banking.bnr.bls-c01.credit-standards-net-percentage");
    g_free (series_id);

    /* The short alias TES is a whole token, never a substring of another word. */
    series_id = atm_chart_catalog_match_request_series_id (
        "Generate a chart about the latest Tesla production data.", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_null (series_id);

    series_id = atm_chart_catalog_match_request_series_id (
        "Show a chart of population over time.", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_null (series_id);

    series_id = atm_chart_catalog_match_request_series_id (
        "Compare GISTEMP global temperature with Energy Institute total energy supply.",
        &ambiguous);
    g_assert_true (ambiguous);
    g_assert_null (series_id);

    series_id = atm_chart_catalog_match_request_series_id ("   ", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_null (series_id);
    g_assert_null (atm_chart_catalog_match_request_series_id ("gistemp", NULL));
}


static void
test_exact_chart_subject_alias_lookup (void)
{
    gboolean ambiguous = FALSE;
    char *series_id = atm_chart_catalog_match_exact_alias_series_id (
        "global temperature anomalies", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_cmpstr (series_id, ==,
                     "climate.gistemp.global-temperature-anomaly");
    g_free (series_id);

    series_id = atm_chart_catalog_match_exact_alias_series_id (
        "global tes", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_cmpstr (series_id, ==,
                     "energy.energy-institute.global-total-energy-supply");
    g_free (series_id);

    series_id = atm_chart_catalog_match_exact_alias_series_id (
        "diabetes prevalence", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_null (series_id);

    series_id = atm_chart_catalog_match_exact_alias_series_id (
        "global temperature anomalies using GISTEMP", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_null (series_id);

    series_id = atm_chart_catalog_match_exact_alias_series_id (
        "procentului net privind standardele de credit", &ambiguous);
    g_assert_false (ambiguous);
    g_assert_cmpstr (series_id, ==,
                     "banking.bnr.bls-c01.credit-standards-net-percentage");
    g_free (series_id);

    g_assert_null (
        atm_chart_catalog_match_exact_alias_series_id ("gistemp", NULL)
    );
}


static void
test_catalog_response_prose_uses_registered_metadata (void)
{
    static const char *series_ids[] = {
        "climate.gistemp.global-temperature-anomaly",
        "energy.energy-institute.global-total-energy-supply",
        "banking.bnr.bls-c01.credit-standards-net-percentage"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (series_ids); i++) {
        const AtmChartCatalogEntry *entry =
            atm_chart_catalog_find_id (series_ids[i]);
        g_assert_nonnull (entry);

        char *guidance =
            atm_chart_catalog_response_guidance (series_ids[i]);
        char *english =
            atm_chart_catalog_chart_only_response (series_ids[i], FALSE);
        char *romanian =
            atm_chart_catalog_chart_only_response (series_ids[i], TRUE);
        g_assert_nonnull (guidance);
        g_assert_nonnull (english);
        g_assert_nonnull (romanian);
        g_assert_nonnull (strstr (guidance, entry->display_name));
        g_assert_nonnull (strstr (guidance, entry->description));
        g_assert_nonnull (strstr (guidance, entry->repository_full_name));
        g_assert_nonnull (strstr (guidance, entry->y_unit));
        g_assert_nonnull (strstr (guidance, entry->publisher_name));
        g_assert_nonnull (strstr (guidance, entry->reconstruction_scope));
        g_assert_nonnull (strstr (guidance, "native Chart/Data widget"));
        g_assert_nonnull (strstr (guidance, "pseudo-table"));
        g_assert_nonnull (strstr (english, entry->display_name));
        g_assert_nonnull (strstr (english, "qualified Chart/Data view"));
        g_assert_nonnull (strstr (english, "If chart construction fails"));
        g_assert_nonnull (strstr (romanian, entry->display_name));
        g_assert_nonnull (strstr (romanian, "Cerere de grafic"));
        g_free (guidance);
        g_free (english);
        g_free (romanian);
    }

    char *bnr_guidance = atm_chart_catalog_response_guidance (
        "banking.bnr.bls-c01.credit-standards-net-percentage");
    char *bnr_english = atm_chart_catalog_chart_only_response (
        "banking.bnr.bls-c01.credit-standards-net-percentage", FALSE);
    char *bnr_romanian = atm_chart_catalog_chart_only_response (
        "banking.bnr.bls-c01.credit-standards-net-percentage", TRUE);
    g_assert_nonnull (strstr (bnr_guidance, "2025-Q2"));
    g_assert_nonnull (strstr (bnr_guidance, "MISSING"));
    g_assert_nonnull (strstr (bnr_guidance, "not zero"));
    g_assert_nonnull (strstr (bnr_english, "2025-Q2 observation is missing"));
    g_assert_nonnull (strstr (bnr_romanian, "2025-Q2 lipsește din sursă"));
    g_free (bnr_guidance);
    g_free (bnr_english);
    g_free (bnr_romanian);

    g_assert_null (atm_chart_catalog_response_guidance ("population"));
    g_assert_null (atm_chart_catalog_chart_only_response ("population", FALSE));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-catalog/validates", test_catalog_validates);
    g_test_add_func ("/chart-catalog/stable-ids", test_stable_ids_and_profiles);
    g_test_add_func ("/chart-catalog/aliases", test_alias_normalization);
    g_test_add_func ("/chart-catalog/request-phrase-boundaries", test_request_alias_phrase_boundaries);
    g_test_add_func ("/chart-catalog/exact-subject-alias", test_exact_chart_subject_alias_lookup);
    g_test_add_func ("/chart-catalog/response-prose-metadata", test_catalog_response_prose_uses_registered_metadata);
    g_test_add_func ("/chart-catalog/snapshot-allowlists", test_snapshot_allowlists);
    g_test_add_func ("/chart-catalog/source-semantics", test_source_semantics_and_provenance);
    g_test_add_func ("/chart-catalog/no-arbitrary-columns", test_catalog_does_not_promote_arbitrary_columns);
    return g_test_run ();
}
