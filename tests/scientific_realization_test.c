#include <glib.h>

#include "scientific_artifact.h"
#include "scientific_realization.h"
#include "scientific_result.h"

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

static AtmScientificArtifact *
new_evidence_artifact (
    const char *repository_id,
    const char *native_id,
    const char *payload
)
{
    AtmScientificEvidenceAtom evidence = { 0 };
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;
    char *logical_source_id = g_strdup_printf (
        "%s:entity:variable:%s",
        repository_id,
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
        g_strcmp0 (repository_id, "cbd") == 0
            ? "atm-profile/cbd/v1"
            : "atm-profile/ewd/v1";
    evidence.profile_version = "1";
    evidence.repository_id = (char *) repository_id;
    evidence.repository_version = "0.1.0";
    evidence.snapshot_sha = (char *) TEST_SHA;
    evidence.logical_source_id = logical_source_id;
    evidence.source_path = "model/fixture.json";
    evidence.locator = locator;
    evidence.evidence_kind = "entity";
    evidence.semantic_type =
        g_strcmp0 (repository_id, "cbd") == 0
            ? "cbd.variable_record"
            : "ewd.variable_record";
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
    return artifact;
}

static void
qualify_evidence (
    AtmSraResult *result,
    const char *repository_id,
    AtmScientificArtifact **artifacts,
    gsize artifact_count
)
{
    GError *error = NULL;
    char *snapshot = g_strdup_printf (
        "%s@0.1.0#%s",
        repository_id,
        TEST_SHA
    );
    char *profile = g_strdup_printf (
        "atm-profile/%s/v1@1",
        repository_id
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
            profile,
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

    g_free (profile);
    g_free (snapshot);
}

static AtmSraEstablishedFact *
numeric_fact (
    const char *fact_id,
    const char *subject,
    const char *attribute,
    const char *value,
    const char *unit,
    const char *support
)
{
    AtmSraEstablishedFact *fact =
        atm_sra_established_fact_new (
            fact_id,
            "numeric",
            subject,
            attribute,
            value,
            unit,
            "fixture-dimension",
            NULL
        );
    GError *error = NULL;

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

static AtmSraResult *
new_answered_result (
    AtmScientificArtifact **out_support
)
{
    AtmScientificArtifact *support =
        new_evidence_artifact (
            "ewd",
            "food_per_capita",
            "{\"id\":\"food_per_capita\",\"value\":7.005760432822278}"
        );
    AtmScientificArtifact *artifacts[] = {
        support
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify_evidence (
        result,
        "ewd",
        artifacts,
        G_N_ELEMENTS (artifacts)
    );

    AtmSraEstablishedFact *fact =
        numeric_fact (
            "food-mape",
            "ewd:variable:food_per_capita",
            "historical_mape_pct",
            "7.005760432822278",
            "percent",
            support->qualified_artifact_id
        );

    g_assert_true (
        atm_sra_result_add_fact (
            result,
            fact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_limitation (
            result,
            "line1\nline2\t\\tail",
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

    *out_support = support;
    return result;
}

static AtmSraResult *
new_blocked_result (
    AtmScientificArtifact **out_control
)
{
    AtmScientificArtifact *control =
        new_control_artifact ();
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_BLOCKED);
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
            control,
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

    *out_control = control;
    return result;
}

static AtmSraResult *
new_bayes_result (
    AtmScientificArtifact **out_prior,
    AtmScientificArtifact **out_lr
)
{
    AtmScientificArtifact *prior =
        new_evidence_artifact (
            "cbd",
            "prior",
            "{\"id\":\"prior\",\"value\":\"0.01\"}"
        );
    AtmScientificArtifact *lr =
        new_evidence_artifact (
            "cbd",
            "lr",
            "{\"id\":\"lr\",\"value\":\"8\"}"
        );
    AtmScientificArtifact *artifacts[] = {
        prior,
        lr
    };
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_ANSWERED);
    GError *error = NULL;

    qualify_evidence (
        result,
        "cbd",
        artifacts,
        G_N_ELEMENTS (artifacts)
    );

    g_assert_true (
        atm_sra_result_add_fact (
            result,
            numeric_fact (
                "prior",
                "fixture.claim",
                "prior_probability",
                "0.01",
                "1",
                prior->qualified_artifact_id
            ),
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_sra_result_add_fact (
            result,
            numeric_fact (
                "lr",
                "fixture.claim",
                "likelihood_ratio",
                "8",
                "1",
                lr->qualified_artifact_id
            ),
            &error
        )
    );
    g_assert_no_error (error);

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
    g_assert_true (
        atm_sra_result_finalize (
            result,
            &error
        )
    );
    g_assert_no_error (error);

    *out_prior = prior;
    *out_lr = lr;
    return result;
}

static void
test_requires_finalized_valid_sra (void)
{
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_NOT_ANSWERABLE);
    GError *error = NULL;

    g_assert_null (
        atm_scientific_realization_view_new (
            result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_REALIZATION_ERROR,
        ATM_SCIENTIFIC_REALIZATION_ERROR_ARGUMENT
    );
    g_clear_error (&error);

    atm_sra_result_free (result);
}

static void
test_answered_direct_render_is_complete_and_deterministic (void)
{
    AtmScientificArtifact *support = NULL;
    AtmSraResult *result =
        new_answered_result (&support);
    GError *error = NULL;

    AtmScientificRealizationView *view =
        atm_scientific_realization_view_new (
            result,
            &error
        );
    g_assert_no_error (error);
    g_assert_nonnull (view);
    g_assert_cmpstr (
        atm_scientific_realization_view_schema (view),
        ==,
        ATM_SCIENTIFIC_REALIZATION_VIEW_SCHEMA
    );
    g_assert_cmpuint (
        atm_scientific_realization_view_block_count (view),
        ==,
        3
    );
    g_assert_cmpuint (
        atm_scientific_realization_view_mandatory_count (view),
        ==,
        3
    );

    AtmScientificDirectRender *first = NULL;
    AtmScientificDirectRender *second = NULL;

    g_assert_true (
        atm_scientific_realization_render_direct (
            view,
            ATM_SCIENTIFIC_RENDER_LANGUAGE_RO,
            &first,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_realization_render_direct (
            view,
            ATM_SCIENTIFIC_RENDER_LANGUAGE_RO,
            &second,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        atm_scientific_direct_render_text (first),
        ==,
        atm_scientific_direct_render_text (second)
    );
    g_assert_cmpstr (
        atm_scientific_direct_render_content_id (first),
        ==,
        result->qualification->scientific_content_id
    );
    g_assert_cmpstr (
        atm_scientific_direct_render_qualified_id (first),
        ==,
        result->qualification->qualified_artifact_id
    );
    g_assert_cmpuint (
        atm_scientific_direct_render_block_count (first),
        ==,
        3
    );
    g_assert_cmpuint (
        atm_scientific_direct_render_support_count (first),
        ==,
        1
    );

    const char *text =
        atm_scientific_direct_render_text (first);
    g_assert_nonnull (
        strstr (text, "Stare: ANSWERED")
    );
    g_assert_nonnull (
        strstr (
            text,
            "value=7.005760432822278; unit=percent"
        )
    );
    g_assert_nonnull (
        strstr (
            text,
            "line1\\nline2\\t\\\\tail"
        )
    );
    g_assert_null (strstr (text, "line1\nline2"));

    atm_scientific_direct_render_free (second);
    atm_scientific_direct_render_free (first);
    atm_scientific_realization_view_free (view);
    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

static void
test_blocked_deny_is_mandatory_and_visible (void)
{
    AtmScientificArtifact *control = NULL;
    AtmSraResult *result =
        new_blocked_result (&control);
    GError *error = NULL;
    AtmScientificRealizationView *view =
        atm_scientific_realization_view_new (
            result,
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (view);
    g_assert_cmpuint (
        atm_scientific_realization_view_block_count (view),
        ==,
        2
    );
    g_assert_cmpstr (
        atm_scientific_realization_view_mandatory_at (
            view,
            1
        ),
        ==,
        "constraint:rmd.behavioural_closure_required"
    );

    AtmScientificDirectRender *render = NULL;

    g_assert_true (
        atm_scientific_realization_render_direct (
            view,
            ATM_SCIENTIFIC_RENDER_LANGUAGE_EN,
            &render,
            &error
        )
    );
    g_assert_no_error (error);

    const char *text =
        atm_scientific_direct_render_text (render);
    g_assert_nonnull (
        strstr (text, "Status: BLOCKED")
    );
    g_assert_nonnull (
        strstr (text, "status=DENY")
    );
    g_assert_nonnull (
        strstr (
            text,
            "reason=CONTROL_CONSTRAINT_DENIED"
        )
    );

    atm_scientific_direct_render_free (render);
    atm_scientific_realization_view_free (view);
    atm_sra_result_free (result);
    atm_scientific_artifact_free (control);
}

static void
test_derived_operation_is_rendered_verbatim (void)
{
    AtmScientificArtifact *prior = NULL;
    AtmScientificArtifact *lr = NULL;
    AtmSraResult *result =
        new_bayes_result (&prior, &lr);
    GError *error = NULL;
    AtmScientificRealizationView *view =
        atm_scientific_realization_view_new (
            result,
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (view);

    AtmScientificDirectRender *render = NULL;

    g_assert_true (
        atm_scientific_realization_render_direct (
            view,
            ATM_SCIENTIFIC_RENDER_LANGUAGE_EN,
            &render,
            &error
        )
    );
    g_assert_no_error (error);

    const char *text =
        atm_scientific_direct_render_text (render);
    g_assert_nonnull (
        strstr (
            text,
            "value=0.07476635514018691"
        )
    );
    g_assert_nonnull (
        strstr (
            text,
            "operation=CBD_BAYES_LR_UPDATE@1"
        )
    );
    g_assert_nonnull (
        strstr (
            text,
            "numeric_profile=atm-numeric/binary64-basic-v1"
        )
    );
    g_assert_cmpuint (
        atm_scientific_direct_render_support_count (render),
        ==,
        2
    );

    atm_scientific_direct_render_free (render);
    atm_scientific_realization_view_free (view);
    atm_sra_result_free (result);
    atm_scientific_artifact_free (lr);
    atm_scientific_artifact_free (prior);
}

static void
test_language_changes_labels_not_scientific_values (void)
{
    AtmScientificArtifact *support = NULL;
    AtmSraResult *result =
        new_answered_result (&support);
    GError *error = NULL;
    AtmScientificRealizationView *view =
        atm_scientific_realization_view_new (
            result,
            &error
        );
    AtmScientificDirectRender *ro = NULL;
    AtmScientificDirectRender *en = NULL;

    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_realization_render_direct (
            view,
            ATM_SCIENTIFIC_RENDER_LANGUAGE_RO,
            &ro,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_realization_render_direct (
            view,
            ATM_SCIENTIFIC_RENDER_LANGUAGE_EN,
            &en,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        atm_scientific_direct_render_text (ro),
        !=,
        atm_scientific_direct_render_text (en)
    );
    g_assert_nonnull (
        strstr (
            atm_scientific_direct_render_text (ro),
            "7.005760432822278"
        )
    );
    g_assert_nonnull (
        strstr (
            atm_scientific_direct_render_text (en),
            "7.005760432822278"
        )
    );
    g_assert_cmpstr (
        atm_scientific_direct_render_content_id (ro),
        ==,
        atm_scientific_direct_render_content_id (en)
    );
    g_assert_cmpstr (
        atm_scientific_direct_render_qualified_id (ro),
        ==,
        atm_scientific_direct_render_qualified_id (en)
    );

    atm_scientific_direct_render_free (en);
    atm_scientific_direct_render_free (ro);
    atm_scientific_realization_view_free (view);
    atm_sra_result_free (result);
    atm_scientific_artifact_free (support);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-realization/requires-finalized",
        test_requires_finalized_valid_sra
    );
    g_test_add_func (
        "/scientific-realization/answered-deterministic",
        test_answered_direct_render_is_complete_and_deterministic
    );
    g_test_add_func (
        "/scientific-realization/blocked-deny",
        test_blocked_deny_is_mandatory_and_visible
    );
    g_test_add_func (
        "/scientific-realization/derived-operation",
        test_derived_operation_is_rendered_verbatim
    );
    g_test_add_func (
        "/scientific-realization/language-stability",
        test_language_changes_labels_not_scientific_values
    );

    return g_test_run ();
}
