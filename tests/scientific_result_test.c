#include <glib.h>

#include "scientific_artifact.h"
#include "scientific_result.h"

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

typedef struct {
    AtmScientificArtifact *a;
    AtmScientificArtifact *b;
} ArtifactPair;

static AtmScientificArtifact *
new_evidence_artifact (
    const char *native_id,
    const char *payload
)
{
    AtmScientificEvidenceAtom evidence = { 0 };
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;
    char *logical_source_id = g_strdup_printf (
        "ewd:entity:variable:%s",
        native_id
    );
    char *locator = g_strdup_printf (
        "json:/fixture/%s",
        native_id
    );

    evidence.status =
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    evidence.reason_code = "typed_validated";
    evidence.profile_id = "atm-profile/ewd/v1";
    evidence.profile_version = "1";
    evidence.repository_id = "ewd";
    evidence.repository_version = "0.1.0";
    evidence.snapshot_sha = (char *) TEST_SHA;
    evidence.logical_source_id = logical_source_id;
    evidence.source_path = "science/configs/fixture.json";
    evidence.locator = locator;
    evidence.evidence_kind = "entity";
    evidence.semantic_type = "ewd.variable_record";
    evidence.entity_type = "variable";
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

static ArtifactPair
new_artifact_pair (void)
{
    ArtifactPair pair = {
        .a = new_evidence_artifact (
            "fixture-a",
            "{\"id\":\"fixture-a\",\"value\":1}"
        ),
        .b = new_evidence_artifact (
            "fixture-b",
            "{\"id\":\"fixture-b\",\"value\":2}"
        )
    };

    return pair;
}

static void
artifact_pair_clear (ArtifactPair *pair)
{
    if (pair == NULL) {
        return;
    }

    atm_scientific_artifact_free (pair->a);
    atm_scientific_artifact_free (pair->b);
    pair->a = NULL;
    pair->b = NULL;
}

static AtmScientificArtifact *
new_control_artifact (void)
{
    AtmScientificControlValue control = { 0 };
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    control.control_id =
        ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE;
    control.repository_id = "rmd";
    control.snapshot_sha = (char *) TEST_SHA;
    control.source_path =
        "model/dynamics/core_contract.json";
    control.json_pointer =
        "/behavioural_closure/active";
    control.value_type =
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN;
    control.boolean_value = FALSE;

    g_assert_true (
        atm_scientific_artifact_from_control (
            &control,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (artifact);
    return artifact;
}

static void
qualify_base (
    AtmSraResult *result,
    const AtmScientificArtifact *support_a,
    const AtmScientificArtifact *support_b
)
{
    GError *error = NULL;

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
            "ewd@0.1.0#0123456789abcdef0123456789abcdef01234567",
            &error
        )
    );
    g_assert_no_error (error);

    if (support_a != NULL) {
        g_assert_true (
            atm_sra_qualification_add_evidence_artifact (
                result,
                support_a,
                &error
            )
        );
        g_assert_no_error (error);
    }

    if (support_b != NULL) {
        g_assert_true (
            atm_sra_qualification_add_evidence_artifact (
                result,
                support_b,
                &error
            )
        );
        g_assert_no_error (error);
    }

    g_assert_true (
        atm_sra_qualification_add_semantic_profile (
            result,
            "atm-profile/ewd/v1@1",
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_qualification_set_numeric_profile (
            result,
            "atm-numeric/exact-decimal+binary64-v1",
            &error
        )
    );
    g_assert_no_error (error);
}

static void
qualify_control (
    AtmSraResult *result,
    const AtmScientificArtifact *artifact
)
{
    GError *error = NULL;

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
            "rmd@0.3.0#0123456789abcdef0123456789abcdef01234567",
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_qualification_add_control_artifact (
            result,
            artifact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_qualification_add_semantic_profile (
            result,
            "atm-profile/rmd/v1@1",
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_qualification_set_numeric_profile (
            result,
            "atm-numeric/exact-decimal+binary64-v1",
            &error
        )
    );
    g_assert_no_error (error);
}

static AtmSraEstablishedFact *
new_fact (
    const char *fact_id,
    const char *value,
    const char *support
)
{
    GError *error = NULL;
    AtmSraEstablishedFact *fact =
        atm_sra_established_fact_new (
            fact_id,
            "categorical",
            "subject",
            "attribute",
            value,
            NULL,
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
    return fact;
}

static void
test_retrieval_order_invariance (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *left =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    AtmSraResult *right =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (left, pair.a, pair.b);
    qualify_base (right, pair.b, pair.a);

    AtmSraEstablishedFact *l2 =
        new_fact ("fact-2", "two", pair.b->qualified_artifact_id);
    AtmSraEstablishedFact *l1 =
        new_fact ("fact-1", "one", pair.a->qualified_artifact_id);
    AtmSraEstablishedFact *r1 =
        new_fact ("fact-1", "one", pair.a->qualified_artifact_id);
    AtmSraEstablishedFact *r2 =
        new_fact ("fact-2", "two", pair.b->qualified_artifact_id);

    g_assert_true (
        atm_sra_result_add_fact (
            left,
            l2,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            left,
            l1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_add_fact (
            right,
            r1,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            right,
            r2,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_add_limitation (
            left,
            "zeta",
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_limitation (
            left,
            "alpha",
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_add_limitation (
            right,
            "alpha",
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_limitation (
            right,
            "zeta",
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_finalize (
            left,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_finalize (
            right,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        left->qualification->scientific_content_id,
        ==,
        right->qualification->scientific_content_id
    );
    g_assert_cmpstr (
        left->qualification->qualified_artifact_id,
        ==,
        right->qualification->qualified_artifact_id
    );

    atm_sra_result_free (right);
    atm_sra_result_free (left);
    artifact_pair_clear (&pair);
}

static void
test_support_changes_only_qualification (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *left =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    AtmSraResult *right =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (left, pair.a, NULL);
    qualify_base (right, pair.b, NULL);

    g_assert_true (
        atm_sra_result_add_fact (
            left,
            new_fact ("fact", "same", pair.a->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            right,
            new_fact ("fact", "same", pair.b->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_finalize (
            left,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_finalize (
            right,
            &error
        )
    );
    g_assert_no_error (error);

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
    artifact_pair_clear (&pair);
}

static void
test_support_assignment_changes_qualification (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *left =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    AtmSraResult *right =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (left, pair.a, pair.b);
    qualify_base (right, pair.a, pair.b);

    g_assert_true (
        atm_sra_result_add_fact (
            left,
            new_fact ("fact-1", "one", pair.a->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            left,
            new_fact ("fact-2", "two", pair.b->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_add_fact (
            right,
            new_fact ("fact-1", "one", pair.b->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            right,
            new_fact ("fact-2", "two", pair.a->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_result_finalize (
            left,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_finalize (
            right,
            &error
        )
    );
    g_assert_no_error (error);

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
    artifact_pair_clear (&pair);
}

static void
test_unqualified_support_fails_closed (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (result, pair.a, NULL);

    g_assert_true (
        atm_sra_result_add_fact (
            result,
            new_fact ("fact", "value", pair.b->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_SUPPORT
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    artifact_pair_clear (&pair);
}

static void
test_derived_facts_forbidden_before_sci06 (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (result, pair.a, NULL);

    g_assert_true (
        atm_sra_result_add_fact (
            result,
            new_fact ("fact", "value", pair.a->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);

    g_ptr_array_add (
        result->derived_facts,
        g_strdup ("forbidden-derived-fact")
    );

    g_assert_false (
        atm_sra_result_finalize (
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
    artifact_pair_clear (&pair);
}

static void
test_operations_forbidden_before_sci06 (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (result, pair.a, NULL);
    g_assert_true (
        atm_sra_result_add_fact (
            result,
            new_fact ("fact", "value", pair.a->qualified_artifact_id),
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_sra_qualification_add_operation (
            result,
            "forbidden.operation@1",
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_sra_result_finalize (
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
    artifact_pair_clear (&pair);
}

static void
test_duplicate_conflict_ids_fail_closed (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_NOT_ANSWERABLE);

    qualify_base (result, pair.a, NULL);

    for (guint i = 0; i < 2; i++) {
        AtmSraConflict *conflict =
            atm_sra_conflict_new (
                "same-conflict",
                i == 0 ? "LEFT" : "RIGHT"
            );

        g_assert_nonnull (conflict);
        g_assert_true (
            atm_sra_conflict_add_support (
                conflict,
                pair.a->qualified_artifact_id,
                &error
            )
        );
        g_assert_no_error (error);
        g_assert_true (
            atm_sra_result_add_conflict (
                result,
                conflict,
                &error
            )
        );
        g_assert_no_error (error);
    }

    g_assert_false (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_SHAPE
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    artifact_pair_clear (&pair);
}

static void
test_blocked_constraint_without_fact_is_valid (void)
{
    GError *error = NULL;
    AtmScientificArtifact *control =
        new_control_artifact ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_BLOCKED);

    qualify_control (result, control);

    AtmSraConstraintResult *constraint =
        atm_sra_constraint_new (
            "rmd.behavioural_closure_required",
            ATM_SRA_CONSTRAINT_DENY,
            "CONTROL_CONSTRAINT_DENIED"
        );

    g_assert_nonnull (constraint);
    g_assert_true (
        atm_sra_constraint_add_support (
            constraint,
            control->qualified_artifact_id,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_constraint (
            result,
            constraint,
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
    g_assert_true (
        atm_sra_result_validate (
            result,
            &error
        )
    );
    g_assert_no_error (error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (control);
}

static void
test_artifact_kind_admission_fails_closed (void)
{
    GError *error = NULL;
    AtmScientificArtifact *control =
        new_control_artifact ();
    AtmScientificArtifact *evidence =
        new_evidence_artifact (
            "kind-check",
            "{\"id\":\"kind-check\",\"value\":1}"
        );
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_NOT_ANSWERABLE);

    g_assert_false (
        atm_sra_qualification_add_evidence_artifact (
            result,
            control,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_SUPPORT
    );
    g_clear_error (&error);

    g_assert_false (
        atm_sra_qualification_add_control_artifact (
            result,
            evidence,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_SUPPORT
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (evidence);
    atm_scientific_artifact_free (control);
}

static void
test_tampered_artifact_admission_fails_closed (void)
{
    GError *error = NULL;
    AtmScientificArtifact *artifact =
        new_evidence_artifact (
            "tamper",
            "{\"id\":\"tamper\",\"value\":1}"
        );
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_NOT_ANSWERABLE);

    g_free (artifact->payload);
    artifact->payload =
        g_strdup ("{\"id\":\"tampered\",\"value\":2}");

    g_assert_false (
        atm_sra_qualification_add_evidence_artifact (
            result,
            artifact,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_SUPPORT
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    atm_scientific_artifact_free (artifact);
}

static void
test_post_finalize_mutation_breaks_validation (void)
{
    GError *error = NULL;
    ArtifactPair pair = new_artifact_pair ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);

    qualify_base (result, pair.a, NULL);
    g_assert_true (
        atm_sra_result_add_fact (
            result,
            new_fact ("fact", "one", pair.a->qualified_artifact_id),
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

    AtmSraEstablishedFact *fact =
        g_ptr_array_index (
            result->established_facts,
            0
        );
    g_free (fact->value);
    fact->value = g_strdup ("tampered");

    g_assert_false (
        atm_sra_result_validate (
            result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_IDENTITY
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
    artifact_pair_clear (&pair);
}


int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-result/retrieval-order-invariance",
        test_retrieval_order_invariance
    );
    g_test_add_func (
        "/scientific-result/support-only-qualification",
        test_support_changes_only_qualification
    );
    g_test_add_func (
        "/scientific-result/support-assignment-qualification",
        test_support_assignment_changes_qualification
    );
    g_test_add_func (
        "/scientific-result/unqualified-support",
        test_unqualified_support_fails_closed
    );
    g_test_add_func (
        "/scientific-result/no-derived-before-sci06",
        test_derived_facts_forbidden_before_sci06
    );
    g_test_add_func (
        "/scientific-result/no-operations-before-sci06",
        test_operations_forbidden_before_sci06
    );
    g_test_add_func (
        "/scientific-result/duplicate-conflict-id",
        test_duplicate_conflict_ids_fail_closed
    );
    g_test_add_func (
        "/scientific-result/blocked-constraint",
        test_blocked_constraint_without_fact_is_valid
    );
    g_test_add_func (
        "/scientific-result/artifact-kind-admission",
        test_artifact_kind_admission_fails_closed
    );
    g_test_add_func (
        "/scientific-result/tampered-artifact-admission",
        test_tampered_artifact_admission_fails_closed
    );
    g_test_add_func (
        "/scientific-result/mutation-validation",
        test_post_finalize_mutation_breaks_validation
    );

    return g_test_run ();
}