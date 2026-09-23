#include <glib.h>

#include "scientific_artifact.h"

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";
static const char *OTHER_SHA =
    "1123456789abcdef0123456789abcdef01234567";

static AtmScientificEvidenceAtom
valid_evidence (void)
{
    AtmScientificEvidenceAtom evidence = { 0 };

    evidence.status =
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    evidence.reason_code = "typed_validated";
    evidence.profile_id = "atm-profile/ewd/v1";
    evidence.profile_version = "1";
    evidence.repository_id = "ewd";
    evidence.repository_version = "0.1.0";
    evidence.snapshot_sha = (char *) TEST_SHA;
    evidence.logical_source_id =
        "ewd:entity:variable:food_per_capita";
    evidence.source_path =
        "science/configs/real_model_structure.json";
    evidence.locator =
        "json:/modules/2/variables/8";
    evidence.evidence_kind = "entity";
    evidence.semantic_type = "ewd.variable_record";
    evidence.entity_type = "variable";
    evidence.native_id = "food_per_capita";
    evidence.raw_payload =
        "{\"id\":\"food_per_capita\",\"kind\":\"auxiliary\"}";

    return evidence;
}

static AtmScientificArtifact *
artifact_from (
    AtmScientificEvidenceAtom *evidence
)
{
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            evidence,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (artifact);
    return artifact;
}

static void
test_evidence_artifact_is_deterministic (void)
{
    AtmScientificEvidenceAtom evidence =
        valid_evidence ();
    AtmScientificArtifact *left =
        artifact_from (&evidence);
    AtmScientificArtifact *right =
        artifact_from (&evidence);
    GError *error = NULL;

    g_assert_cmpstr (
        left->schema_id,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_SCHEMA_ID
    );
    g_assert_cmpstr (
        left->scientific_content_id,
        ==,
        right->scientific_content_id
    );
    g_assert_cmpstr (
        left->qualified_artifact_id,
        ==,
        right->qualified_artifact_id
    );
    g_assert_cmpstr (
        left->storage_digest,
        ==,
        right->storage_digest
    );

    g_assert_true (
        atm_scientific_artifact_validate (
            left,
            &error
        )
    );
    g_assert_no_error (error);

    atm_scientific_artifact_free (right);
    atm_scientific_artifact_free (left);
}

static void
test_storage_format_can_change_without_content_identity (void)
{
    AtmScientificEvidenceAtom first =
        valid_evidence ();
    AtmScientificEvidenceAtom second =
        valid_evidence ();

    second.raw_payload =
        "{ \n \"kind\" : \"auxiliary\","
        " \"id\" : \"food_per_capita\" \n}";

    AtmScientificArtifact *left =
        artifact_from (&first);
    AtmScientificArtifact *right =
        artifact_from (&second);

    g_assert_cmpstr (
        left->scientific_content_id,
        ==,
        right->scientific_content_id
    );
    g_assert_cmpstr (
        left->qualified_artifact_id,
        ==,
        right->qualified_artifact_id
    );
    g_assert_cmpstr (
        left->storage_digest,
        !=,
        right->storage_digest
    );

    atm_scientific_artifact_free (right);
    atm_scientific_artifact_free (left);
}

static void
test_snapshot_changes_qualification_not_content (void)
{
    AtmScientificEvidenceAtom first =
        valid_evidence ();
    AtmScientificEvidenceAtom second =
        valid_evidence ();

    second.snapshot_sha = (char *) OTHER_SHA;

    AtmScientificArtifact *left =
        artifact_from (&first);
    AtmScientificArtifact *right =
        artifact_from (&second);

    g_assert_cmpstr (
        left->scientific_content_id,
        ==,
        right->scientific_content_id
    );
    g_assert_cmpstr (
        left->qualified_artifact_id,
        !=,
        right->qualified_artifact_id
    );
    g_assert_cmpstr (
        left->storage_digest,
        !=,
        right->storage_digest
    );

    atm_scientific_artifact_free (right);
    atm_scientific_artifact_free (left);
}

static void
test_semantic_payload_change_changes_content (void)
{
    AtmScientificEvidenceAtom first =
        valid_evidence ();
    AtmScientificEvidenceAtom second =
        valid_evidence ();

    second.raw_payload =
        "{\"id\":\"food_per_capita\",\"kind\":\"stock\"}";

    AtmScientificArtifact *left =
        artifact_from (&first);
    AtmScientificArtifact *right =
        artifact_from (&second);

    g_assert_cmpstr (
        left->scientific_content_id,
        !=,
        right->scientific_content_id
    );
    g_assert_cmpstr (
        left->qualified_artifact_id,
        !=,
        right->qualified_artifact_id
    );
    g_assert_cmpstr (
        left->storage_digest,
        !=,
        right->storage_digest
    );

    atm_scientific_artifact_free (right);
    atm_scientific_artifact_free (left);
}

static void
test_requires_typed_validated (void)
{
    AtmScientificEvidenceAtom evidence =
        valid_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    evidence.status =
        ATM_SCIENTIFIC_EVIDENCE_TEXTUAL_ONLY;

    g_assert_false (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_ARTIFACT_ERROR,
        ATM_SCIENTIFIC_ARTIFACT_ERROR_STATUS
    );
    g_assert_null (artifact);
    g_clear_error (&error);
}

static void
test_invalid_json_payload_is_rejected (void)
{
    AtmScientificEvidenceAtom evidence =
        valid_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    evidence.raw_payload = "not-json";

    g_assert_false (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_ARTIFACT_ERROR,
        ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY
    );
    g_assert_null (artifact);
    g_clear_error (&error);
}

static void
test_mutation_breaks_identity_validation (void)
{
    AtmScientificEvidenceAtom evidence =
        valid_evidence ();
    AtmScientificArtifact *artifact =
        artifact_from (&evidence);
    GError *error = NULL;

    g_free (artifact->payload);
    artifact->payload = g_strdup (
        "{\"tampered\":true}"
    );

    g_assert_false (
        atm_scientific_artifact_validate (
            artifact,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_ARTIFACT_ERROR,
        ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY
    );
    g_clear_error (&error);

    atm_scientific_artifact_free (artifact);
}

static void
test_control_artifact_is_canonical (void)
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

    g_assert_cmpstr (
        artifact->artifact_class,
        ==,
        "rmd.behavioural_closure_active_control"
    );
    g_assert_cmpstr (
        artifact->payload,
        ==,
        "false"
    );
    g_assert_true (
        atm_scientific_artifact_validate (
            artifact,
            &error
        )
    );
    g_assert_no_error (error);

    atm_scientific_artifact_free (artifact);
}

static void
test_control_contract_cannot_be_forged (void)
{
    AtmScientificControlValue control = { 0 };
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    control.control_id =
        ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE;
    control.repository_id = "rmd";
    control.snapshot_sha = (char *) TEST_SHA;
    control.source_path = "STATUS.md";
    control.json_pointer =
        "/behavioural_closure/active";
    control.value_type =
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN;

    g_assert_false (
        atm_scientific_artifact_from_control (
            &control,
            &artifact,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_ARTIFACT_ERROR,
        ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL
    );
    g_assert_null (artifact);
    g_clear_error (&error);
}

static void
test_missing_provenance_is_rejected (void)
{
    AtmScientificEvidenceAtom evidence =
        valid_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    evidence.snapshot_sha = NULL;

    g_assert_false (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_ARTIFACT_ERROR,
        ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE
    );
    g_assert_null (artifact);
    g_clear_error (&error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-artifact/evidence-deterministic",
        test_evidence_artifact_is_deterministic
    );
    g_test_add_func (
        "/scientific-artifact/storage-vs-content",
        test_storage_format_can_change_without_content_identity
    );
    g_test_add_func (
        "/scientific-artifact/snapshot-qualification",
        test_snapshot_changes_qualification_not_content
    );
    g_test_add_func (
        "/scientific-artifact/semantic-content-change",
        test_semantic_payload_change_changes_content
    );
    g_test_add_func (
        "/scientific-artifact/requires-typed-validated",
        test_requires_typed_validated
    );
    g_test_add_func (
        "/scientific-artifact/invalid-json-payload",
        test_invalid_json_payload_is_rejected
    );
    g_test_add_func (
        "/scientific-artifact/mutation-breaks-validation",
        test_mutation_breaks_identity_validation
    );
    g_test_add_func (
        "/scientific-artifact/control-canonical",
        test_control_artifact_is_canonical
    );
    g_test_add_func (
        "/scientific-artifact/control-cannot-be-forged",
        test_control_contract_cannot_be_forged
    );
    g_test_add_func (
        "/scientific-artifact/missing-provenance",
        test_missing_provenance_is_rejected
    );

    return g_test_run ();
}
