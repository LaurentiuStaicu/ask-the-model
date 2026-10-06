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

static AtmSraResult *qualified_with_artifact (AtmScientificArtifact *a)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    AtmSraEstablishedFact *f1 = atm_sra_established_fact_new (
        "fact-1", "observation", "subject", "attribute", "1", "u", "d", NULL);
    AtmSraEstablishedFact *f2 = atm_sra_established_fact_new (
        "fact-2", "observation", "subject", "attribute", "2", "u", "d", NULL);
    g_assert_true (atm_sra_fact_add_support (f1, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_fact_add_support (f2, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, f1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, f2, &error));
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

static AtmSeriesContract *base (void)
{
    GError *error = NULL;
    AtmSeriesContract *s = atm_series_contract_new (
        ATM_SERIES_X_CALENDAR_YEAR, "subject", "attribute",
        "calendar_year", "time", "unit", "dimension",
        "empirical_observations", "baseline", "1880-2025", "observed",
        "fixture", "1", SHA, "data/series.csv", "fixture-series", "1", &error);
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
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.2500", "125", -2, FALSE,
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
    g_assert_true (atm_series_contract_validate (s, q, &error));
    g_assert_no_error (error);
    g_assert_nonnull (atm_series_contract_scientific_id (s));
    g_assert_nonnull (atm_series_contract_qualified_id (s));

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

static void test_unqualified_support_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
    const char *forged[] = { SHA };
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
    s->scientific_content_id = NULL;
    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/series-contract/missing-break", test_missing_and_break_are_explicit);
    g_test_add_func ("/series-contract/duplicate-x", test_duplicate_x_rejected);
    g_test_add_func ("/series-contract/support", test_unqualified_support_rejected);
    g_test_add_func ("/series-contract/first-break", test_first_break_rejected);
    g_test_add_func ("/series-contract/identity", test_caller_identity_not_authoritative);
    return g_test_run ();
}
