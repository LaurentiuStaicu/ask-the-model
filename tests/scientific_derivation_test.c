#include <glib.h>

#include "scientific_artifact.h"
#include "scientific_canonical.h"
#include "scientific_result.h"

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

static const char *
profile_id_for_repo (const char *repo)
{
    if (g_strcmp0 (repo, "cbd") == 0) {
        return "atm-profile/cbd/v1";
    }

    if (g_strcmp0 (repo, "rmd") == 0) {
        return "atm-profile/rmd/v1";
    }

    g_assert_not_reached ();
}

static const char *
profile_qualification_for_repo (const char *repo)
{
    if (g_strcmp0 (repo, "cbd") == 0) {
        return "atm-profile/cbd/v1@1";
    }

    if (g_strcmp0 (repo, "rmd") == 0) {
        return "atm-profile/rmd/v1@1";
    }

    g_assert_not_reached ();
}

static const char *
semantic_type_for_repo (const char *repo)
{
    if (g_strcmp0 (repo, "cbd") == 0) {
        return "cbd.variable_record";
    }

    if (g_strcmp0 (repo, "rmd") == 0) {
        return "rmd.stock_record";
    }

    g_assert_not_reached ();
}

static const char *
entity_type_for_repo (const char *repo)
{
    if (g_strcmp0 (repo, "cbd") == 0) {
        return "variable";
    }

    if (g_strcmp0 (repo, "rmd") == 0) {
        return "stock";
    }

    g_assert_not_reached ();
}

static AtmScientificArtifact *
new_artifact (
    const char *repo,
    const char *native_id,
    const char *payload
)
{
    AtmScientificEvidenceAtom evidence = { 0 };
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;
    char *logical_source_id = g_strdup_printf (
        "%s:entity:%s:%s",
        repo,
        entity_type_for_repo (repo),
        native_id
    );
    char *locator = g_strdup_printf (
        "json:/fixture/%s",
        native_id
    );

    evidence.status =
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    evidence.reason_code = "typed_validated";
    evidence.profile_id =
        (char *) profile_id_for_repo (repo);
    evidence.profile_version = "1";
    evidence.repository_id = (char *) repo;
    evidence.repository_version = "0.1.0";
    evidence.snapshot_sha = (char *) TEST_SHA;
    evidence.logical_source_id = logical_source_id;
    evidence.source_path = "model/fixture.json";
    evidence.locator = locator;
    evidence.evidence_kind = "entity";
    evidence.semantic_type =
        (char *) semantic_type_for_repo (repo);
    evidence.entity_type =
        (char *) entity_type_for_repo (repo);
    evidence.native_id = (char *) native_id;
    evidence.raw_payload = (char *) payload;

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (artifact);

    g_free (locator);
    g_free (logical_source_id);
    return artifact;
}

static void
qualify (
    AtmSraResult *result,
    const char *repo,
    AtmScientificArtifact **artifacts,
    gsize artifact_count,
    const char *numeric_profile
)
{
    GError *error = NULL;
    char *snapshot = g_strdup_printf (
        "%s@0.1.0#%s",
        repo,
        TEST_SHA
    );

    g_assert_true (
        atm_sra_qualification_add_canonical_obligation (
            result,
            "atm-sra/1",
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_qualification_add_repository_snapshot (
            result,
            snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_free (snapshot);

    for (gsize i = 0; i < artifact_count; i++) {
        g_assert_true (
            atm_sra_qualification_add_evidence_artifact (
                result,
                artifacts[i],
                &error
            )
        );
        g_assert_no_error (error);
    }

    g_assert_true (
        atm_sra_qualification_add_semantic_profile (
            result,
            profile_qualification_for_repo (repo),
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_qualification_set_numeric_profile (
            result,
            numeric_profile,
            &error
        )
    );
    g_assert_no_error (error);
}

static AtmSraEstablishedFact *
add_numeric_fact (
    AtmSraResult *result,
    const char *fact_id,
    const char *value,
    const char *unit,
    const char *support
)
{
    GError *error = NULL;
    AtmSraEstablishedFact *fact =
        atm_sra_established_fact_new (
            fact_id,
            "numeric",
            "fixture.subject",
            fact_id,
            value,
            unit,
            NULL,
            NULL
        );

    g_assert_nonnull (fact);
    g_assert_true (
        atm_sra_fact_add_support (
            fact,
            support,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            result,
            fact,
            &error
        )
    );
    g_assert_no_error (error);
    return fact;
}

static AtmSraDerivedFact *
only_derived (AtmSraResult *result)
{
    g_assert_cmpuint (
        result->derived_facts->len,
        ==,
        1
    );

    return g_ptr_array_index (
        result->derived_facts,
        0
    );
}

static void
test_bayes_derivation (void)
{
    AtmScientificArtifact *prior_support =
        new_artifact (
            "cbd",
            "prior",
            "{\"id\":\"prior\",\"value\":\"0.01\"}"
        );
    AtmScientificArtifact *lr_support =
        new_artifact (
            "cbd",
            "lr",
            "{\"id\":\"lr\",\"value\":\"8\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        prior_support,
        lr_support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "cbd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal+binary64-v1"
    );

    add_numeric_fact (
        result,
        "prior",
        "0.01",
        "1",
        prior_support->qualified_artifact_id
    );
    add_numeric_fact (
        result,
        "lr",
        "8",
        "1",
        lr_support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "prior_probability", "prior" },
        { "likelihood_ratio", "lr" }
    };

    g_assert_true (
        atm_sra_result_derive_fact (
            result,
            "CBD_BAYES_LR_UPDATE@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "posterior",
            "probability",
            "fixture.claim",
            "posterior_probability",
            "probability",
            &error
        )
    );
    g_assert_no_error (error);

    AtmSraDerivedFact *derived =
        only_derived (result);

    g_assert_cmpint (
        derived->outcome,
        ==,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC
    );
    g_assert_cmpstr (
        derived->value,
        ==,
        "0.07476635514018691"
    );
    g_assert_cmpstr (derived->unit, ==, "1");
    g_assert_true (derived->has_binary64_bits);
    g_assert_cmpstr (
        derived->operation,
        ==,
        "CBD_BAYES_LR_UPDATE@1"
    );
    g_assert_cmpstr (
        derived->operation_numeric_profile,
        ==,
        "atm-numeric/binary64-basic-v1"
    );
    g_assert_cmpuint (derived->support->len, ==, 2);
    g_assert_cmpuint (
        result->qualification->operations->len,
        ==,
        1
    );

    g_assert_true (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_validate (
            result,
            &error
        )
    );
    g_assert_no_error (error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (lr_support);
    atm_scientific_artifact_free (prior_support);
}

static void
test_rmd_stock_close_derivation (void)
{
    AtmScientificArtifact *support =
        new_artifact (
            "rmd",
            "stock-close",
            "{\"id\":\"stock-close\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "rmd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal+binary64-v1"
    );

    add_numeric_fact (
        result, "opening", "100",
        "million_RON",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "transactions", "12",
        "million_RON",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "revaluations", "-3",
        "million_RON",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "other", "1",
        "million_RON",
        support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "opening", "opening" },
        { "transactions", "transactions" },
        { "revaluations", "revaluations" },
        { "other_changes", "other" }
    };

    g_assert_true (
        atm_sra_result_derive_fact (
            result,
            "RMD_FINANCIAL_STOCK_CLOSE@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "closing",
            "stock",
            "fixture.balance_sheet",
            "closing",
            "currency",
            &error
        )
    );
    g_assert_no_error (error);

    AtmSraDerivedFact *derived =
        only_derived (result);
    g_assert_cmpstr (derived->value, ==, "110");
    g_assert_cmpstr (
        derived->unit,
        ==,
        "million_RON"
    );

    g_assert_true (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_validate (
            result,
            &error
        )
    );
    g_assert_no_error (error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

static void
test_safe_ratio_zero_is_explicit_missing (void)
{
    AtmScientificArtifact *support =
        new_artifact (
            "rmd",
            "ratio",
            "{\"id\":\"ratio\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "rmd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal+binary64-v1"
    );

    add_numeric_fact (
        result, "numerator", "10",
        "million_RON",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "denominator", "0",
        "million_RON",
        support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "numerator", "numerator" },
        { "denominator", "denominator" }
    };

    g_assert_true (
        atm_sra_result_derive_fact (
            result,
            "RMD_SAFE_RATIO@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "ratio",
            "ratio",
            "fixture.ratio",
            "value",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    AtmSraDerivedFact *derived =
        only_derived (result);
    g_assert_cmpint (
        derived->outcome,
        ==,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING
    );
    g_assert_cmpstr (
        derived->value,
        ==,
        "MISSING"
    );
    g_assert_cmpstr (
        derived->reason_code,
        ==,
        "ZERO_DENOMINATOR"
    );
    g_assert_false (derived->has_binary64_bits);

    g_assert_true (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_validate (
            result,
            &error
        )
    );
    g_assert_no_error (error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

static void
test_check_operation_is_not_sra_derivation (void)
{
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_NOT_ANSWERABLE);
    GError *error = NULL;

    g_assert_false (
        atm_sra_result_derive_fact (
            result,
            "RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1",
            NULL,
            0,
            "forbidden",
            "boolean",
            "fixture",
            "check",
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_DERIVATION
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
}

static void
test_binding_role_mismatch_fails (void)
{
    AtmScientificArtifact *support =
        new_artifact (
            "cbd",
            "binding",
            "{\"id\":\"binding\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "cbd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal+binary64-v1"
    );
    add_numeric_fact (
        result, "prior", "0.1", "1",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "lr", "2", "1",
        support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "likelihood_ratio", "prior" },
        { "prior_probability", "lr" }
    };

    g_assert_false (
        atm_sra_result_derive_fact (
            result,
            "CBD_BAYES_LR_UPDATE@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "posterior",
            "probability",
            "fixture",
            "posterior",
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_DERIVATION
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

static void
test_numeric_profile_must_authorize_operation (void)
{
    AtmScientificArtifact *support =
        new_artifact (
            "cbd",
            "numeric-profile",
            "{\"id\":\"numeric-profile\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "cbd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal-v1"
    );
    add_numeric_fact (
        result, "prior", "0.1", "1",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "lr", "2", "1",
        support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "prior_probability", "prior" },
        { "likelihood_ratio", "lr" }
    };

    g_assert_false (
        atm_sra_result_derive_fact (
            result,
            "CBD_BAYES_LR_UPDATE@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "posterior",
            "probability",
            "fixture",
            "posterior",
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_DERIVATION
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

static AtmSraResult *
new_binding_identity_result (
    AtmScientificArtifact *support,
    const char *selected_prior
)
{
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "cbd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal+binary64-v1"
    );

    add_numeric_fact (
        result, "prior-a", "0.01", "1",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "prior-b", "0.01", "1",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "lr", "8", "1",
        support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "prior_probability", (char *) selected_prior },
        { "likelihood_ratio", "lr" }
    };

    g_assert_true (
        atm_sra_result_derive_fact (
            result,
            "CBD_BAYES_LR_UPDATE@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "posterior",
            "probability",
            "fixture",
            "posterior",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_no_error (error);
    return result;
}

static void
test_binding_changes_only_qualification (void)
{
    AtmScientificArtifact *support =
        new_artifact (
            "cbd",
            "binding-identity",
            "{\"id\":\"binding-identity\"}"
        );

    AtmSraResult *left =
        new_binding_identity_result (
            support,
            "prior-a"
        );
    AtmSraResult *right =
        new_binding_identity_result (
            support,
            "prior-b"
        );

    g_assert_cmpstr (
        left->qualification->scientific_content_id,
        ==,
        right->qualification->scientific_content_id
    );
    g_assert_cmpstr (
        left->qualification->qualified_artifact_id,
        !=,
        right->qualification->qualified_artifact_id
    );

    atm_sra_result_free (right);
    atm_sra_result_free (left);
    atm_scientific_artifact_free (support);
}

static void
test_derived_mutation_is_detected (void)
{
    AtmScientificArtifact *support =
        new_artifact (
            "cbd",
            "tamper",
            "{\"id\":\"tamper\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify (
        result,
        "cbd",
        artifacts,
        G_N_ELEMENTS (artifacts),
        "atm-numeric/exact-decimal+binary64-v1"
    );
    add_numeric_fact (
        result, "prior", "0.1", "1",
        support->qualified_artifact_id
    );
    add_numeric_fact (
        result, "lr", "2", "1",
        support->qualified_artifact_id
    );

    AtmSraOperationBinding bindings[] = {
        { "prior_probability", "prior" },
        { "likelihood_ratio", "lr" }
    };

    g_assert_true (
        atm_sra_result_derive_fact (
            result,
            "CBD_BAYES_LR_UPDATE@1",
            bindings,
            G_N_ELEMENTS (bindings),
            "posterior",
            "probability",
            "fixture",
            "posterior",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_no_error (error);

    AtmSraDerivedFact *derived =
        only_derived (result);
    derived->binary64_bits ^= 1u;

    g_assert_false (
        atm_sra_result_validate (
            result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_DERIVATION
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-derivation/bayes",
        test_bayes_derivation
    );
    g_test_add_func (
        "/scientific-derivation/rmd-stock-close",
        test_rmd_stock_close_derivation
    );
    g_test_add_func (
        "/scientific-derivation/safe-ratio-missing",
        test_safe_ratio_zero_is_explicit_missing
    );
    g_test_add_func (
        "/scientific-derivation/check-not-admitted",
        test_check_operation_is_not_sra_derivation
    );
    g_test_add_func (
        "/scientific-derivation/role-mismatch",
        test_binding_role_mismatch_fails
    );
    g_test_add_func (
        "/scientific-derivation/numeric-profile",
        test_numeric_profile_must_authorize_operation
    );
    g_test_add_func (
        "/scientific-derivation/binding-qualification",
        test_binding_changes_only_qualification
    );
    g_test_add_func (
        "/scientific-derivation/mutation-detected",
        test_derived_mutation_is_detected
    );

    return g_test_run ();
}
