#include <glib.h>
#include "series_contract.h"
#include "scientific_artifact.h"

static const char *SHA = "0123456789abcdef0123456789abcdef01234567";

static AtmScientificArtifact *support_artifact (void)
{
    AtmScientificEvidenceAtom e = {0};
    AtmScientificArtifact *a = NULL;
    GError *error = NULL;
    e.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    e.reason_code = "typed_validated";
    e.profile_id = "atm-profile/ewd/v1";
    e.profile_version = "1";
    e.repository_id = "ewd";
    e.repository_version = "0.1.0";
    e.snapshot_sha = (char *) SHA;
    e.logical_source_id = "ewd:entity:variable:fixture";
    e.source_path = "science/configs/fixture.json";
    e.locator = "json:/fixture/fixture";
    e.evidence_kind = "entity";
    e.semantic_type = "ewd.variable_record";
    e.entity_type = "variable";
    e.native_id = "fixture";
    e.raw_payload = "{\"id\":\"fixture\",\"value\":1}";
    g_assert_true (atm_scientific_artifact_from_evidence (&e, &a, &error));
    g_assert_no_error (error);
    return a;
}

static AtmSraResult *qualified_with_artifact_mode (AtmScientificArtifact *a,
                                                       gboolean qualify_break)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    AtmSraEstablishedFact *f1 = atm_sra_established_fact_new (
        "fact-1", "observation", "subject", "attribute", "1e0", "unit", "dimension",
        "{\"calendar_year\":\"1880\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\"}");
    AtmSraEstablishedFact *f2 = atm_sra_established_fact_new (
        "fact-2", "observation", "subject", "attribute", "0e0", "unit", "dimension",
        "{\"calendar_year\":\"1882\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\"}");
    const char *break_fields = qualify_break
        ? ",\"break_before\":true,\"break_reason\":\"measurement_gap\"" : "";
    char *missing_qualifiers = g_strdup_printf (
        "{\"calendar_year\":\"1881\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\","
        "\"y_status\":\"MISSING\",\"missing_reason\":\"source_missing\"%s}",
        break_fields);
    AtmSraEstablishedFact *missing = atm_sra_established_fact_new (
        "fact-missing-1881", "observation", "subject", "attribute",
        "MISSING", "unit", "dimension", missing_qualifiers);
    g_free (missing_qualifiers);
    g_assert_true (atm_sra_fact_add_support (f1, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_fact_add_support (f2, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_fact_add_support (missing, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, f1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, f2, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, missing, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_canonical_obligation (q, "atm-sra/1", &error));
    g_assert_no_error (error);
    char *snapshot = g_strdup_printf ("ewd@0.1.0#%s", SHA);
    g_assert_true (atm_sra_qualification_add_repository_snapshot (q, snapshot, &error));
    g_free (snapshot);
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_evidence_artifact (q, a, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_semantic_profile (
        q, "atm-profile/ewd/v1@1", &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_set_numeric_profile (
        q, ATM_SERIES_CONTRACT_NUMERIC_PROFILE, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_finalize (q, &error));
    g_assert_no_error (error);
    return q;
}

static AtmSraResult *qualified_with_artifact (AtmScientificArtifact *a)
{
    return qualified_with_artifact_mode (a, FALSE);
}

static AtmSraResult *
qualified_with_single_coordinate_fact (AtmScientificArtifact *a,
                                      const char *coordinate_key,
                                      const char *coordinate_value)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    char *qualifiers = g_strdup_printf (
        "{\"%s\":\"%s\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\"}",
        coordinate_key, coordinate_value);
    AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
        "coordinate-fact", "observation", "subject", "attribute",
        "1e0", "unit", "dimension", qualifiers);
    g_free (qualifiers);
    g_assert_nonnull (fact);
    g_assert_true (atm_sra_fact_add_support (fact, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, fact, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_canonical_obligation (q, "atm-sra/1", &error));
    g_assert_no_error (error);
    char *snapshot = g_strdup_printf ("ewd@0.1.0#%s", SHA);
    g_assert_true (atm_sra_qualification_add_repository_snapshot (q, snapshot, &error));
    g_free (snapshot);
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_evidence_artifact (q, a, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_semantic_profile (
        q, "atm-profile/ewd/v1@1", &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_set_numeric_profile (
        q, ATM_SERIES_CONTRACT_NUMERIC_PROFILE, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_finalize (q, &error));
    g_assert_no_error (error);
    return q;
}

static AtmSraResult *
qualified_with_missing_facts (AtmScientificArtifact *a)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    AtmSraEstablishedFact *f0 = atm_sra_established_fact_new (
        "fact-missing-1880", "observation", "subject", "attribute",
        "MISSING", "unit", "dimension",
        "{\"calendar_year\":\"1880\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\","
        "\"y_status\":\"MISSING\",\"missing_reason\":\"source_missing\"}");
    AtmSraEstablishedFact *f1 = atm_sra_established_fact_new (
        "fact-missing-1881", "observation", "subject", "attribute",
        "MISSING", "unit", "dimension",
        "{\"calendar_year\":\"1881\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\","
        "\"y_status\":\"MISSING\",\"missing_reason\":\"source_missing\"}");

    g_assert_true (atm_sra_fact_add_support (f0, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_fact_add_support (f1, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, f0, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, f1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_canonical_obligation (q, "atm-sra/1", &error));
    g_assert_no_error (error);
    char *snapshot = g_strdup_printf ("ewd@0.1.0#%s", SHA);
    g_assert_true (atm_sra_qualification_add_repository_snapshot (q, snapshot, &error));
    g_free (snapshot);
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_evidence_artifact (q, a, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_semantic_profile (
        q, "atm-profile/ewd/v1@1", &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_set_numeric_profile (
        q, ATM_SERIES_CONTRACT_NUMERIC_PROFILE, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_finalize (q, &error));
    g_assert_no_error (error);
    return q;
}

static AtmScientificArtifact *
support_artifact_other (void)
{
    AtmScientificEvidenceAtom e = {0};
    AtmScientificArtifact *a = NULL;
    GError *error = NULL;
    e.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    e.reason_code = "typed_validated";
    e.profile_id = "atm-profile/ewd/v1";
    e.profile_version = "1";
    e.repository_id = "ewd";
    e.repository_version = "0.1.0";
    e.snapshot_sha = (char *) SHA;
    e.logical_source_id = "ewd:entity:variable:fixture-other";
    e.source_path = "science/configs/fixture-other.json";
    e.locator = "json:/fixture/fixture-other";
    e.evidence_kind = "entity";
    e.semantic_type = "ewd.variable_record";
    e.entity_type = "variable";
    e.native_id = "fixture-other";
    e.raw_payload = "{\"id\":\"fixture-other\",\"value\":2}";
    g_assert_true (atm_scientific_artifact_from_evidence (&e, &a, &error));
    g_assert_no_error (error);
    return a;
}

static AtmSraResult *
qualified_with_two_artifacts (AtmScientificArtifact *a, AtmScientificArtifact *b)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
        "fact-two-supports", "observation", "subject", "attribute",
        "1e0", "unit", "dimension",
        "{\"calendar_year\":\"1880\",\"scenario\":\"baseline\",\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\"}");

    g_assert_true (atm_sra_fact_add_support (fact, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_fact_add_support (fact, b->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, fact, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_canonical_obligation (q, "atm-sra/1", &error));
    g_assert_no_error (error);
    char *snapshot = g_strdup_printf ("ewd@0.1.0#%s", SHA);
    g_assert_true (atm_sra_qualification_add_repository_snapshot (q, snapshot, &error));
    g_free (snapshot);
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_evidence_artifact (q, a, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_evidence_artifact (q, b, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_semantic_profile (
        q, "atm-profile/ewd/v1@1", &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_set_numeric_profile (
        q, ATM_SERIES_CONTRACT_NUMERIC_PROFILE, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_finalize (q, &error));
    g_assert_no_error (error);
    return q;
}

static AtmSeriesContract *base (void)
{
    GError *error = NULL;
    AtmSeriesContract *s = atm_series_contract_new (
        ATM_SERIES_X_CALENDAR_YEAR, "subject", "attribute",
        "calendar_year", "time", "unit", "dimension",
        "empirical_observations", "baseline", "1880-2025", "observed",
        "ewd", "0.1.0", SHA, "data/series.csv", "fixture-series", "1", &error);
    g_assert_no_error (error);
    g_assert_nonnull (s);
    return s;
}

static void test_missing_and_break_are_explicit (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        TRUE, "source_missing", "measurement_gap", support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s, "1882", ATM_SERIES_Y_NUMERIC, "-0.0000", "0", 0, TRUE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    gboolean valid = atm_series_contract_validate (s, q, &error);
    if (!valid && error != NULL)
        g_test_message ("contract validation error: %s", error->message);
    g_assert_true (valid);
    g_assert_no_error (error);
    g_assert_nonnull (atm_series_contract_scientific_id (s));
    g_assert_nonnull (atm_series_contract_qualified_id (s));

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_signed_zero_flag_matches_source (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    AtmSeriesContract *s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "-0.0000", "0", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR, ATM_SERIES_CONTRACT_ERROR_NUMERIC);
    g_clear_error (&error);
    atm_series_contract_free (s);

    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "0.0000", "0", 0, TRUE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR, ATM_SERIES_CONTRACT_ERROR_NUMERIC);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_duplicate_x_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "2.0000", "2", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_ORDER);
    g_clear_error (&error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_numeric_point_must_match_sra_fact (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "2.0000", "2", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR, ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);

    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1881", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR, ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_numeric_point_support_must_match_as_unique_set (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmScientificArtifact *b = support_artifact_other ();
    AtmSraResult *q = qualified_with_two_artifacts (a, b);
    GError *error = NULL;

    const char *duplicate_support[] = {
        a->qualified_artifact_id, a->qualified_artifact_id
    };
    AtmSeriesContract *s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, duplicate_support, G_N_ELEMENTS (duplicate_support), &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR, ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);

    /* Reordering is valid: finalized SRA support is a canonical sorted set,
     * not a positional list. The same two unique IDs must be accepted. */
    const char *reordered_support[] = {
        b->qualified_artifact_id, a->qualified_artifact_id
    };
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, reordered_support, G_N_ELEMENTS (reordered_support), &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (s, q, &error));
    g_assert_no_error (error);
    g_assert_nonnull (atm_series_contract_scientific_id (s));
    atm_series_contract_free (s);

    atm_sra_result_free (q);
    atm_scientific_artifact_free (b);
    atm_scientific_artifact_free (a);
}

static void test_epistemic_status_must_match_sra_fact (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    g_free (s->epistemic_status);
    s->epistemic_status = g_strdup ("simulated");
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_calendar_year_dimension_must_be_time (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    g_free (s->x_dimension);
    s->x_dimension = g_strdup ("mass");
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_direct_scenario_must_match_sra_fact (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_free (s->scenario);
    s->scenario = g_strdup ("not_applicable");
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_direct_time_scope_must_enclose_points (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_free (s->time_scope);
    s->time_scope = g_strdup ("1881-2025");
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_missing_point_must_match_sra_fact (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    /* A numeric fact for the same year must not authorize a MISSING point. */
    AtmSeriesContract *s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);

    /* The year and MISSING status match, but the claimed reason does not. */
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        FALSE, "not_reported", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);

    /* Only the explicit matching missing fact authorizes this point. */
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (s, q, &error));
    g_assert_no_error (error);
    atm_series_contract_free (s);

    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_unqualified_support_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *forged[] = { "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" };
    GError *error = NULL;
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        FALSE, "not_observed", NULL, forged, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SUPPORT);
    g_clear_error (&error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_break_reason_requires_break (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    /* A reason without break_before is contradictory metadata, not a dormant
     * discontinuity declaration. */
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, "measurement_gap", support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_assert_nonnull (g_strstr_len (error->message, -1,
                                    "Break reason requires break_before"));
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_first_break_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        TRUE, "not_observed", "start_gap", support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_ORDER);
    g_clear_error (&error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_caller_identity_not_authoritative (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    s->scientific_content_id = g_strdup (SHA);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_IDENTITY);
    g_clear_error (&error);
    g_clear_pointer (&s->scientific_content_id, g_free);
    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void
test_contract_snapshot_must_be_qualified (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);

    /* Point facts/support still match, but a changed source version must not
     * be minted into a qualified contract against the old SRA envelope. */
    g_free (s->repository_version);
    s->repository_version = g_strdup ("unqualified-version");
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_IDENTITY);
    g_assert_nonnull (g_strstr_len (error->message, -1,
        "repository/version/snapshot is not present in finalized qualification"));
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static AtmSeriesContract *base_kind (AtmSeriesXKind kind);

static void test_last_break_and_all_missing_are_supported (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    AtmSeriesContract *broken = base_kind (ATM_SERIES_X_CALENDAR_YEAR);
    g_assert_true (atm_series_contract_add_point (
        broken, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0,
        FALSE, FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        broken, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, TRUE, "source_missing", "measurement_gap", support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (broken, q, &error));
    g_assert_no_error (error);
    atm_series_contract_free (broken);

    AtmSraResult *q_missing = qualified_with_missing_facts (a);
    AtmSeriesContract *missing = base_kind (ATM_SERIES_X_CALENDAR_YEAR);
    g_assert_true (atm_series_contract_add_point (
        missing, "1880", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        missing, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (missing, q_missing, &error));
    g_assert_no_error (error);
    atm_series_contract_free (missing);
    atm_sra_result_free (q_missing);

    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static AtmSeriesContract *base_kind (AtmSeriesXKind kind)
{
    GError *error = NULL;
    AtmSeriesContract *s = atm_series_contract_new (
        kind, "subject", "attribute",
        kind == ATM_SERIES_X_UTC_INSTANT ? "UTC" :
            kind == ATM_SERIES_X_EVENT_ORDINAL ? "event" : "calendar_year",
        "time", "unit", "dimension",
        "empirical_observations", "baseline",
        kind == ATM_SERIES_X_CALENDAR_YEAR ? "1880-2025" : "scope", "observed",
        "ewd", "0.1.0", SHA, "data/series.csv", "fixture-series", "1", &error);
    g_assert_no_error (error);
    g_assert_nonnull (s);
    return s;
}

static void test_non_calendar_coordinates_bind_to_matching_sra_qualifier (void)
{
    const struct {
        AtmSeriesXKind kind;
        const char *qualifier;
        const char *coordinate;
    } cases[] = {
        { ATM_SERIES_X_UTC_INSTANT, "utc_instant", "2026-01-01T00:00:00Z" },
        { ATM_SERIES_X_EVENT_ORDINAL, "event_ordinal", "42" },
    };

    for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
        AtmScientificArtifact *a = support_artifact ();
        AtmSraResult *q = qualified_with_single_coordinate_fact (
            a, cases[i].qualifier, cases[i].coordinate);
        AtmSeriesContract *s = base_kind (cases[i].kind);
        const char *support[] = { a->qualified_artifact_id };
        GError *error = NULL;

        g_assert_true (atm_series_contract_add_point (
            s, cases[i].coordinate, ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0,
            FALSE, FALSE, NULL, NULL, support, 1, &error));
        g_assert_no_error (error);
        g_assert_true (atm_series_contract_validate (s, q, &error));
        g_assert_no_error (error);
        g_assert_nonnull (atm_series_contract_scientific_id (s));

        atm_series_contract_free (s);
        atm_sra_result_free (q);
        atm_scientific_artifact_free (a);
    }
}

static void test_invalid_utc_calendar_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base_kind (ATM_SERIES_X_UTC_INSTANT);
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_false (atm_series_contract_add_point (
        s, "2026-02-30T12:00:00Z", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_ARGUMENT);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_event_ordinal_overflow_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base_kind (ATM_SERIES_X_EVENT_ORDINAL);
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    g_assert_true (atm_series_contract_add_point (
        s, "18446744073709551615", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_add_point (
        s, "18446744073709551616", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_ARGUMENT);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/series-contract/missing-break", test_missing_and_break_are_explicit);
    g_test_add_func ("/series-contract/signed-zero-flag", test_signed_zero_flag_matches_source);
    g_test_add_func ("/series-contract/duplicate-x", test_duplicate_x_rejected);
    g_test_add_func ("/series-contract/support", test_unqualified_support_rejected);
    g_test_add_func ("/series-contract/fact-binding", test_numeric_point_must_match_sra_fact);
    g_test_add_func ("/series-contract/missing-point-fact-binding", test_missing_point_must_match_sra_fact);
    g_test_add_func ("/series-contract/direct-epistemic-status-binding", test_epistemic_status_must_match_sra_fact);
    g_test_add_func ("/series-contract/calendar-year-dimension", test_calendar_year_dimension_must_be_time);
    g_test_add_func ("/series-contract/direct-scenario-binding", test_direct_scenario_must_match_sra_fact);
    g_test_add_func ("/series-contract/direct-time-scope", test_direct_time_scope_must_enclose_points);
    g_test_add_func ("/series-contract/support-unique-set", test_numeric_point_support_must_match_as_unique_set);
    g_test_add_func ("/series-contract/break-reason-requires-break", test_break_reason_requires_break);
    g_test_add_func ("/series-contract/first-break", test_first_break_rejected);
    g_test_add_func ("/series-contract/identity", test_caller_identity_not_authoritative);
    g_test_add_func ("/series-contract/snapshot-binding", test_contract_snapshot_must_be_qualified);
    g_test_add_func ("/series-contract/non-calendar-coordinate-binding", test_non_calendar_coordinates_bind_to_matching_sra_qualifier);
    g_test_add_func ("/series-contract/utc-calendar", test_invalid_utc_calendar_rejected);
    g_test_add_func ("/series-contract/event-overflow", test_event_ordinal_overflow_rejected);
    g_test_add_func ("/series-contract/missing-boundaries", test_last_break_and_all_missing_are_supported);
    return g_test_run ();
}
