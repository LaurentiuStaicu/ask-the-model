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

static AtmSraResult *
qualified_with_artifact_mode_full (AtmScientificArtifact *a,
                                   gboolean qualify_break,
                                   const char *canonical_obligation,
                                   const char *numeric_profile)
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
    g_assert_true (atm_sra_qualification_add_canonical_obligation (
        q, canonical_obligation, &error));
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
        q, numeric_profile, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_finalize (q, &error));
    g_assert_no_error (error);
    return q;
}

static AtmSraResult *qualified_with_artifact_mode (AtmScientificArtifact *a,
                                                    gboolean qualify_break)
{
    return qualified_with_artifact_mode_full (
        a, qualify_break, "atm-sra/1", ATM_SERIES_CONTRACT_NUMERIC_PROFILE);
}

static AtmSraResult *qualified_with_artifact_mode_profile (
    AtmScientificArtifact *a, gboolean qualify_break, const char *numeric_profile)
{
    return qualified_with_artifact_mode_full (
        a, qualify_break, "atm-sra/1", numeric_profile);
}

static AtmSraResult *qualified_with_artifact_mode_obligation (
    AtmScientificArtifact *a, gboolean qualify_break, const char *canonical_obligation)
{
    return qualified_with_artifact_mode_full (
        a, qualify_break, canonical_obligation, ATM_SERIES_CONTRACT_NUMERIC_PROFILE);
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
qualified_with_single_source_decimal_fact (
    AtmScientificArtifact *a,
    const char *coordinate_key,
    const char *coordinate_value,
    const char *source_decimal
)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    char *qualifiers = source_decimal == NULL
        ? g_strdup_printf (
            "{\"%s\":\"%s\",\"scenario\":\"baseline\","
            "\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\"}",
            coordinate_key, coordinate_value)
        : g_strdup_printf (
            "{\"%s\":\"%s\",\"scenario\":\"baseline\","
            "\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\","
            "\"source_decimal\":\"%s\"}",
            coordinate_key, coordinate_value, source_decimal);
    AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
        "source-decimal-fact", "observation", "subject", "attribute",
        "0e0", "unit", "dimension", qualifiers);
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
qualified_with_single_numeric_payload_fact (
    AtmScientificArtifact *a,
    const char *coefficient,
    const char *exponent,
    const char *numeric_profile,
    gboolean negative_zero
)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    char *qualifiers = g_strdup_printf (
        "{\"calendar_year\":\"1880\",\"scenario\":\"baseline\","
        "\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\","
        "\"coefficient\":\"%s\",\"exponent\":\"%s\","
        "\"numeric_profile\":\"%s\",\"negative_zero\":%s}",
        coefficient, exponent, numeric_profile,
        negative_zero ? "true" : "false");
    AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
        "numeric-payload-fact", "observation", "subject", "attribute",
        "1e0", "unit", "dimension", qualifiers);
    g_free (qualifiers);
    g_assert_nonnull (fact);
    g_assert_true (atm_sra_fact_add_support (fact, a->qualified_artifact_id, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_add_fact (q, fact, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_canonical_obligation (
        q, "atm-sra/1", &error));
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
qualified_with_coordinate_facts (
    AtmScientificArtifact *a,
    const char *coordinate_key,
    const char *const *coordinates,
    const char *const *values,
    guint count
)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    g_assert_nonnull (coordinates);
    g_assert_nonnull (values);
    g_assert_cmpuint (count, >, 0);

    for (guint i = 0; i < count; i++) {
        char *qualifiers = g_strdup_printf (
            "{\"%s\":\"%s\",\"scenario\":\"baseline\","
            "\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\"}",
            coordinate_key, coordinates[i]);
        char *id = g_strdup_printf ("coordinate-fact-%u", i);
        AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
            id, "observation", "subject", "attribute", values[i],
            "unit", "dimension", qualifiers);
        g_free (id);
        g_free (qualifiers);
        g_assert_nonnull (fact);
        g_assert_true (atm_sra_fact_add_support (
            fact, a->qualified_artifact_id, &error));
        g_assert_no_error (error);
        g_assert_true (atm_sra_result_add_fact (q, fact, &error));
        g_assert_no_error (error);
    }

    g_assert_true (atm_sra_qualification_add_canonical_obligation (
        q, "atm-sra/1", &error));
    g_assert_no_error (error);
    char *snapshot = g_strdup_printf ("ewd@0.1.0#%s", SHA);
    g_assert_true (atm_sra_qualification_add_repository_snapshot (
        q, snapshot, &error));
    g_free (snapshot);
    g_assert_no_error (error);
    g_assert_true (atm_sra_qualification_add_evidence_artifact (
        q, a, &error));
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

static AtmSraResult *
qualified_with_numeric_fact_missing_reason (AtmScientificArtifact *a,
                                            gboolean malformed_reason_type)
{
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    const char *qualifiers = malformed_reason_type
        ? "{\"calendar_year\":\"1880\",\"scenario\":\"baseline\","
          "\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\","
          "\"missing_reason\":42}"
        : "{\"calendar_year\":\"1880\",\"scenario\":\"baseline\","
          "\"epistemic_status\":\"observed\",\"y_status\":\"NUMERIC\","
          "\"missing_reason\":\"source_missing\"}";
    AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
        "fact-numeric-with-missing-reason", "observation", "subject",
        "attribute", "1e0", "unit", "dimension", qualifiers);
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
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    /* A missing observation alone does not qualify a discontinuity label. */
    AtmSraResult *q = qualified_with_artifact (a);
    AtmSeriesContract *s = base ();
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
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);

    /* The same series is accepted when its missing-point SRA fact explicitly
     * carries the matching break_before and break_reason qualifiers. */
    q = qualified_with_artifact_mode (a, TRUE);
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 0, FALSE,
        TRUE, "source_missing", "measurement_gap", support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s, "1882", ATM_SERIES_Y_NUMERIC, "0.0000", "0", 0, FALSE,
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

static void test_numeric_source_decimal_must_match_qualified_fact (void)
{
    AtmScientificArtifact *a = support_artifact ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    /* The normalized SRA value is 0e0 in both cases. The qualified original
     * token still distinguishes positive from negative zero. */
    AtmSraResult *q = qualified_with_single_source_decimal_fact (
        a, "calendar_year", "1882", "0.0000");
    AtmSeriesContract *s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1882", ATM_SERIES_Y_NUMERIC, "-0.0000", "0", 0, TRUE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);

    /* A normalized zero without a qualified original token cannot prove
     * the distinctive negative-zero claim. */
    q = qualified_with_single_source_decimal_fact (
        a, "calendar_year", "1882", NULL);
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1882", ATM_SERIES_Y_NUMERIC, "-0.0000", "0", 0, TRUE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_clear_error (&error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);

    /* Generic facts with normalized ordinary zero remain compatible when
     * source-specific lexical metadata is not part of their schema. */
    q = qualified_with_single_source_decimal_fact (
        a, "calendar_year", "1882", NULL);
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1882", ATM_SERIES_Y_NUMERIC, "0.0000", "0", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (s, q, &error));
    g_assert_no_error (error);
    atm_series_contract_free (s);
    atm_sra_result_free (q);

    q = qualified_with_single_source_decimal_fact (
        a, "calendar_year", "1882", "-0.0000");
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1882", ATM_SERIES_Y_NUMERIC, "-0.0000", "0", 0, TRUE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (s, q, &error));
    g_assert_no_error (error);
    g_assert_nonnull (atm_series_contract_scientific_id (s));

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_numeric_payload_qualifiers_must_match_point (void)
{
    AtmScientificArtifact *a = support_artifact ();
    const char *support[] = { a->qualified_artifact_id };
    const struct {
        const char *coefficient;
        const char *exponent;
        const char *numeric_profile;
        gboolean negative_zero;
        gboolean valid;
    } cases[] = {
        { "2", "0", ATM_SERIES_CONTRACT_NUMERIC_PROFILE, FALSE, FALSE },
        { "1", "1", ATM_SERIES_CONTRACT_NUMERIC_PROFILE, FALSE, FALSE },
        { "1", "0", "different-numeric-profile/1", FALSE, FALSE },
        { "1", "0", ATM_SERIES_CONTRACT_NUMERIC_PROFILE, TRUE, FALSE },
        { "1", "0", ATM_SERIES_CONTRACT_NUMERIC_PROFILE, FALSE, TRUE }
    };

    for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
        AtmSraResult *q = qualified_with_single_numeric_payload_fact (
            a, cases[i].coefficient, cases[i].exponent,
            cases[i].numeric_profile, cases[i].negative_zero);
        AtmSeriesContract *s = base ();
        GError *error = NULL;
        g_assert_true (atm_series_contract_add_point (
            s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
            FALSE, NULL, NULL, support, 1, &error));
        g_assert_no_error (error);

        if (cases[i].valid) {
            g_assert_true (atm_series_contract_validate (s, q, &error));
            g_assert_no_error (error);
            g_assert_nonnull (atm_series_contract_scientific_id (s));
        } else {
            g_assert_false (atm_series_contract_validate (s, q, &error));
            g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                            ATM_SERIES_CONTRACT_ERROR_SHAPE);
            g_clear_error (&error);
        }

        atm_series_contract_free (s);
        atm_sra_result_free (q);
    }
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

static void
test_sra_numeric_profile_must_match_contract (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_artifact_mode_profile (
        a, FALSE, "binary64_roundtrip/1");
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;
    g_assert_true (atm_sra_result_validate (q, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_NUMERIC);
    g_assert_nonnull (g_strstr_len (error->message, -1,
        "SRA numeric profile does not match series contract numeric profile"));
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

static void
test_numeric_fact_rejects_missing_reason (void)
{
    AtmScientificArtifact *a = support_artifact ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;
    for (guint malformed = 0; malformed < 2; malformed++) {
        AtmSraResult *q = qualified_with_numeric_fact_missing_reason (a,
                                                                       malformed != 0);
        AtmSeriesContract *s = base ();
        g_assert_true (atm_series_contract_add_point (
            s, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
            FALSE, NULL, NULL, support, 1, &error));
        g_assert_no_error (error);
        g_assert_false (atm_series_contract_validate (s, q, &error));
        g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                        ATM_SERIES_CONTRACT_ERROR_SHAPE);
        g_assert_nonnull (g_strstr_len (error->message, -1,
            "Numeric point does not match an established SRA fact"));
        g_clear_error (&error);
        atm_series_contract_free (s);
        atm_sra_result_free (q);
    }
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

    /* MISSING cannot retain a nonzero numeric exponent after its value and
     * coefficient are cleared. */
    s = base ();
    g_assert_true (atm_series_contract_add_point (
        s, "1881", ATM_SERIES_Y_MISSING, NULL, NULL, 42, FALSE,
        FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_NUMERIC);
    g_assert_nonnull (g_strstr_len (error->message, -1,
        "MISSING cannot carry numeric value or exponent"));
    g_clear_error (&error);
    atm_series_contract_free (s);

    /* Only the explicit matching missing fact authorizes the canonical state,
     * with no numeric representation (including exponent zero). */
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

static void
test_qualified_series_identity_binds_sra_qualification (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q1 = qualified_with_artifact (a);
    AtmSraResult *q2 = qualified_with_artifact_mode_obligation (
        a, FALSE, "atm-sra/alternate-obligation/1");
    GError *error = NULL;
    g_assert_true (atm_sra_result_validate (q1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_sra_result_validate (q2, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (q1->qualification->qualified_artifact_id, !=,
                     q2->qualification->qualified_artifact_id);
    const char *support[] = { a->qualified_artifact_id };
    AtmSeriesContract *s1 = base ();
    AtmSeriesContract *s2 = base ();
    g_assert_true (atm_series_contract_add_point (
        s1, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_add_point (
        s2, "1880", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0, FALSE,
        FALSE, NULL, NULL, support, 1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (s1, q1, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_contract_validate (s2, q2, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_series_contract_scientific_id (s1), ==,
                     atm_series_contract_scientific_id (s2));
    g_assert_cmpstr (atm_series_contract_qualified_id (s1), !=,
                     atm_series_contract_qualified_id (s2));
    atm_series_contract_free (s1);
    atm_series_contract_free (s2);
    atm_sra_result_free (q1);
    atm_sra_result_free (q2);
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

static void test_sra_artifact_snapshot_must_be_qualified (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;
    const char *other_sha =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    /* The envelope cannot merely claim a repository snapshot that differs
     * from the provenance bound into the artifact's qualified identity. */
    char *snapshot = g_strdup_printf ("ewd@0.1.0#%s", other_sha);
    g_assert_true (atm_sra_qualification_add_repository_snapshot (
        q, snapshot, &error));
    g_assert_no_error (error);
    g_free (snapshot);

    g_assert_false (atm_sra_qualification_add_evidence_artifact (
        q, a, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_SUPPORT);
    g_assert_nonnull (g_strstr_len (error->message, -1,
        "repository/snapshot is absent or ambiguous in qualification"));
    g_clear_error (&error);

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
    AtmSraResult *q = qualified_with_artifact_mode (a, TRUE);
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
            kind == ATM_SERIES_X_EVENT_ORDINAL ? "event" :
            kind == ATM_SERIES_X_CALENDAR_QUARTER ? "calendar_quarter" :
            "calendar_year",
        "time", "unit", "dimension",
        "empirical_observations", "baseline",
        kind == ATM_SERIES_X_CALENDAR_YEAR ? "1880-2025" :
            kind == ATM_SERIES_X_CALENDAR_QUARTER ? "2025-Q1..2025-Q4" :
            "scope", "observed",
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

static void test_calendar_quarter_coordinates_are_ordered_and_identity_bound (void)
{
    const char *coordinates[] = {
        "2024-Q4",
        "2025-Q1",
        "2025-Q3"
    };
    const char *values[] = { "1e0", "2e0", "3e0" };
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_coordinate_facts (
        a, "calendar_quarter", coordinates, values, G_N_ELEMENTS (coordinates));
    AtmSeriesContract *s = base_kind (ATM_SERIES_X_CALENDAR_QUARTER);
    g_free (s->time_scope);
    s->time_scope = g_strdup ("2024-Q4..2025-Q3");
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    for (guint i = 0; i < G_N_ELEMENTS (coordinates); i++) {
        char *decimal = g_strdup_printf ("%u.0000", i + 1);
        char *coefficient = g_strdup_printf ("%u", i + 1);
        g_assert_true (atm_series_contract_add_point (
            s, coordinates[i], ATM_SERIES_Y_NUMERIC, decimal, coefficient, 0,
            FALSE, FALSE, NULL, NULL, support, 1, &error));
        g_assert_no_error (error);
        g_free (decimal);
        g_free (coefficient);
    }

    g_assert_true (atm_series_contract_validate (s, q, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (s->points->len, ==, 3);
    const AtmSeriesContractPoint *last =
        g_ptr_array_index (s->points, 2);
    g_assert_cmpstr (last->x, ==, "2025-Q3");
    /* A gap between Q1 and Q3 remains an unfilled gap; ordering must not
     * manufacture a Q2 point or convert the source observation into a break. */
    g_assert_false (last->break_before);
    g_assert_nonnull (atm_series_contract_scientific_id (s));
    g_assert_nonnull (atm_series_contract_qualified_id (s));

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_calendar_quarter_malformed_coordinates_rejected (void)
{
    const char *invalid[] = {
        "2025-Q0", "2025-Q5", "2025-QX", "2025-q1", "2025Q1",
        "2025-Q01", "025-Q1", "0000-Q1", "2025-Q1x",
        "2025-Q 1", "000A-Q1", "2025-QQ", "2025-Q4 "
    };
    AtmScientificArtifact *a = support_artifact ();
    const char *support[] = { a->qualified_artifact_id };

    for (guint i = 0; i < G_N_ELEMENTS (invalid); i++) {
        AtmSeriesContract *s = base_kind (ATM_SERIES_X_CALENDAR_QUARTER);
        GError *error = NULL;
        g_assert_false (atm_series_contract_add_point (
            s, invalid[i], ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0,
            FALSE, FALSE, NULL, NULL, support, 1, &error));
        g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                        ATM_SERIES_CONTRACT_ERROR_ARGUMENT);
        g_clear_error (&error);
        atm_series_contract_free (s);
    }

    atm_scientific_artifact_free (a);
}

static void test_calendar_quarter_duplicate_and_reverse_rejected (void)
{
    const char *coordinates[] = { "2025-Q1", "2025-Q3" };
    const char *values[] = { "1e0", "2e0" };
    const char *cases[][2] = {
        { "2025-Q1", "2025-Q1" },
        { "2025-Q3", "2025-Q1" }
    };
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_coordinate_facts (
        a, "calendar_quarter", coordinates, values, G_N_ELEMENTS (coordinates));
    const char *support[] = { a->qualified_artifact_id };

    for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
        AtmSeriesContract *s = base_kind (ATM_SERIES_X_CALENDAR_QUARTER);
        GError *error = NULL;
        for (guint point = 0; point < 2; point++) {
            gboolean is_q3 = g_strcmp0 (cases[i][point], "2025-Q3") == 0;
            g_assert_true (atm_series_contract_add_point (
                s, cases[i][point], ATM_SERIES_Y_NUMERIC,
                is_q3 ? "2.0000" : "1.0000",
                is_q3 ? "2" : "1", 0,
                FALSE, FALSE, NULL, NULL, support, 1, &error));
            g_assert_no_error (error);
        }
        g_assert_false (atm_series_contract_validate (s, q, &error));
        g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                        ATM_SERIES_CONTRACT_ERROR_ORDER);
        g_clear_error (&error);
        atm_series_contract_free (s);
    }

    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_calendar_quarter_requires_temporal_unit_dimension_and_scope (void)
{
    const char *coordinates[] = { "2025-Q1", "2025-Q3" };
    const char *values[] = { "1e0", "2e0" };
    AtmScientificArtifact *a = support_artifact ();
    AtmSraResult *q = qualified_with_coordinate_facts (
        a, "calendar_quarter", coordinates, values, G_N_ELEMENTS (coordinates));
    const char *support[] = { a->qualified_artifact_id };

    for (guint mode = 0; mode < 3; mode++) {
        AtmSeriesContract *s = base_kind (ATM_SERIES_X_CALENDAR_QUARTER);
        GError *error = NULL;
        if (mode == 0) {
            g_free (s->x_unit);
            s->x_unit = g_strdup ("calendar_year");
        } else if (mode == 1) {
            g_free (s->x_dimension);
            s->x_dimension = g_strdup ("mass");
        } else {
            g_free (s->time_scope);
            s->time_scope = g_strdup ("2025-Q2..2025-Q4");
        }
        g_assert_true (atm_series_contract_add_point (
            s, "2025-Q1", ATM_SERIES_Y_NUMERIC, "1.0000", "1", 0,
            FALSE, FALSE, NULL, NULL, support, 1, &error));
        g_assert_no_error (error);
        g_assert_false (atm_series_contract_validate (s, q, &error));
        g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                        ATM_SERIES_CONTRACT_ERROR_SHAPE);
        g_clear_error (&error);
        atm_series_contract_free (s);
    }

    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_metadata_bounds_rechecked_after_mutation (void)
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

    /* Construction bounded this field, but the public struct can be mutated
     * before validation. It must not reach canonical identity serialization. */
    g_free (s->series_semantics);
    s->series_semantics = g_strnfill (ATM_SERIES_CONTRACT_MAX_TEXT + 1u, 'x');

    g_assert_false (atm_series_contract_validate (s, q, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_assert_nonnull (g_strstr_len (error->message, -1,
                                    "Invalid or oversized series metadata"));
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_sra_result_free (q);
    atm_scientific_artifact_free (a);
}

static void test_calendar_year_negative_zero_rejected (void)
{
    AtmScientificArtifact *a = support_artifact ();
    AtmSeriesContract *s = base ();
    const char *support[] = { a->qualified_artifact_id };
    GError *error = NULL;

    /* Calendar X has one canonical spelling for numeric zero. */
    g_assert_false (atm_series_contract_add_point (
        s, "-0", ATM_SERIES_Y_MISSING, NULL, NULL, 0,
        FALSE, FALSE, "source_missing", NULL, support, 1, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_ARGUMENT);
    g_clear_error (&error);

    atm_series_contract_free (s);
    atm_scientific_artifact_free (a);
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
    g_test_add_func ("/series-contract/source-decimal-binding",
                     test_numeric_source_decimal_must_match_qualified_fact);
    g_test_add_func ("/series-contract/numeric-payload-binding",
                     test_numeric_payload_qualifiers_must_match_point);
    g_test_add_func ("/series-contract/duplicate-x", test_duplicate_x_rejected);
    g_test_add_func ("/series-contract/support", test_unqualified_support_rejected);
    g_test_add_func ("/series-contract/fact-binding", test_numeric_point_must_match_sra_fact);
    g_test_add_func ("/series-contract/sra-numeric-profile",
                     test_sra_numeric_profile_must_match_contract);
    g_test_add_func ("/series-contract/missing-point-fact-binding", test_missing_point_must_match_sra_fact);
    g_test_add_func ("/series-contract/numeric-fact-no-missing-reason",
                     test_numeric_fact_rejects_missing_reason);
    g_test_add_func ("/series-contract/direct-epistemic-status-binding", test_epistemic_status_must_match_sra_fact);
    g_test_add_func ("/series-contract/calendar-year-dimension", test_calendar_year_dimension_must_be_time);
    g_test_add_func ("/series-contract/direct-scenario-binding", test_direct_scenario_must_match_sra_fact);
    g_test_add_func ("/series-contract/direct-time-scope", test_direct_time_scope_must_enclose_points);
    g_test_add_func ("/series-contract/support-unique-set", test_numeric_point_support_must_match_as_unique_set);
    g_test_add_func ("/series-contract/break-reason-requires-break", test_break_reason_requires_break);
    g_test_add_func ("/series-contract/first-break", test_first_break_rejected);
    g_test_add_func ("/series-contract/qualified-identity-binds-sra",
                     test_qualified_series_identity_binds_sra_qualification);
    g_test_add_func ("/series-contract/identity", test_caller_identity_not_authoritative);
    g_test_add_func ("/series-contract/artifact-snapshot-binding", test_sra_artifact_snapshot_must_be_qualified);
    g_test_add_func ("/series-contract/snapshot-binding", test_contract_snapshot_must_be_qualified);
    g_test_add_func ("/series-contract/non-calendar-coordinate-binding", test_non_calendar_coordinates_bind_to_matching_sra_qualifier);
    g_test_add_func ("/series-contract/calendar-quarter-order-and-identity",
                     test_calendar_quarter_coordinates_are_ordered_and_identity_bound);
    g_test_add_func ("/series-contract/calendar-quarter-malformed",
                     test_calendar_quarter_malformed_coordinates_rejected);
    g_test_add_func ("/series-contract/calendar-quarter-order-rejection",
                     test_calendar_quarter_duplicate_and_reverse_rejected);
    g_test_add_func ("/series-contract/calendar-quarter-scope",
                     test_calendar_quarter_requires_temporal_unit_dimension_and_scope);
    g_test_add_func ("/series-contract/metadata-bounds-after-mutation", test_metadata_bounds_rechecked_after_mutation);
    g_test_add_func ("/series-contract/calendar-year-negative-zero", test_calendar_year_negative_zero_rejected);
    g_test_add_func ("/series-contract/utc-calendar", test_invalid_utc_calendar_rejected);
    g_test_add_func ("/series-contract/event-overflow", test_event_ordinal_overflow_rejected);
    g_test_add_func ("/series-contract/missing-boundaries", test_last_break_and_all_missing_are_supported);
    return g_test_run ();
}
