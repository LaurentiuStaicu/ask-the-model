#include <glib.h>

#include <string.h>

#include "scientific_artifact.h"

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

static AtmScientificEvidenceAtom
base_evidence (void)
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
        "{\"id\":\"food_per_capita\","
        "\"kind\":\"auxiliary\","
        "\"unit\":\"food_unit/person/year\"}";

    return evidence;
}

static AtmScientificControlValue
base_control (gboolean active)
{
    AtmScientificControlValue control = { 0 };

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
    control.boolean_value = active;

    return control;
}

static void
assert_sha256_id (const char *artifact_id)
{
    g_assert_nonnull (artifact_id);
    g_assert_true (
        g_str_has_prefix (
            artifact_id,
            "sha256:"
        )
    );
    g_assert_cmpuint (
        strlen (artifact_id),
        ==,
        71
    );

    for (const char *cursor =
            artifact_id + strlen ("sha256:");
         *cursor != '\0';
         cursor++) {
        g_assert_true (
            g_ascii_isxdigit (*cursor)
        );
        g_assert_false (
            *cursor >= 'A' && *cursor <= 'F'
        );
    }
}

static void
test_typed_evidence_artifact (void)
{
    AtmScientificEvidenceAtom evidence =
        base_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (artifact);
    g_assert_cmpuint (
        artifact->schema_version,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_SCHEMA_VERSION
    );
    g_assert_cmpstr (
        artifact->artifact_profile_id,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_PROFILE_ID
    );
    g_assert_cmpint (
        artifact->origin,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_ORIGIN_TYPED_EVIDENCE
    );
    g_assert_cmpstr (
        artifact->repository_id,
        ==,
        "ewd"
    );
    g_assert_cmpstr (
        artifact->logical_source_id,
        ==,
        evidence.logical_source_id
    );
    g_assert_cmpstr (
        artifact->source_locator_kind,
        ==,
        "retrieval_locator"
    );
    g_assert_cmpstr (
        artifact->semantic_type,
        ==,
        "ewd.variable_record"
    );
    g_assert_cmpint (
        artifact->payload_type,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_TEXT
    );
    g_assert_cmpstr (
        artifact->text_value,
        ==,
        evidence.raw_payload
    );
    assert_sha256_id (artifact->artifact_id);
    g_assert_cmpstr (
        artifact->artifact_id,
        ==,
        "sha256:10adaa89026193e254a2a760e76ce7911592df4b6af9631a12dab95e214e3858"
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
test_same_evidence_same_id (void)
{
    AtmScientificEvidenceAtom left = base_evidence ();
    AtmScientificEvidenceAtom right = base_evidence ();
    AtmScientificArtifact *a = NULL;
    AtmScientificArtifact *b = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &left,
            &a,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &right,
            &b,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        a->artifact_id,
        ==,
        b->artifact_id
    );

    atm_scientific_artifact_free (a);
    atm_scientific_artifact_free (b);
}

static void
test_payload_change_changes_id (void)
{
    AtmScientificEvidenceAtom left = base_evidence ();
    AtmScientificEvidenceAtom right = base_evidence ();
    AtmScientificArtifact *a = NULL;
    AtmScientificArtifact *b = NULL;
    GError *error = NULL;

    right.raw_payload =
        "{\"id\":\"food_per_capita\","
        "\"kind\":\"auxiliary\","
        "\"unit\":\"changed\"}";

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &left,
            &a,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &right,
            &b,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        a->artifact_id,
        !=,
        b->artifact_id
    );

    atm_scientific_artifact_free (a);
    atm_scientific_artifact_free (b);
}

static void
test_provenance_change_changes_id (void)
{
    AtmScientificEvidenceAtom left = base_evidence ();
    AtmScientificEvidenceAtom right = base_evidence ();
    AtmScientificArtifact *a = NULL;
    AtmScientificArtifact *b = NULL;
    GError *error = NULL;

    right.snapshot_sha =
        "1123456789abcdef0123456789abcdef01234567";

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &left,
            &a,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &right,
            &b,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        a->artifact_id,
        !=,
        b->artifact_id
    );

    atm_scientific_artifact_free (a);
    atm_scientific_artifact_free (b);
}

static void
test_cross_repository_profile_rejected (void)
{
    AtmScientificEvidenceAtom evidence =
        base_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    evidence.profile_id = "atm-profile/cbd/v1";

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
        ATM_SCIENTIFIC_ARTIFACT_ERROR_SOURCE_STATUS
    );
    g_assert_null (artifact);
    g_clear_error (&error);
}

static void
test_non_typed_evidence_rejected (void)
{
    AtmScientificEvidenceAtom evidence =
        base_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    evidence.status =
        ATM_SCIENTIFIC_EVIDENCE_TEXTUAL_ONLY;
    evidence.reason_code = "textual_evidence";

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
        ATM_SCIENTIFIC_ARTIFACT_ERROR_SOURCE_STATUS
    );
    g_assert_null (artifact);
    g_clear_error (&error);
}

static void
test_uppercase_sha_rejected (void)
{
    AtmScientificEvidenceAtom evidence =
        base_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    evidence.snapshot_sha =
        "ABCDEF6789abcdef0123456789abcdef01234567";

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

static void
test_control_artifact (void)
{
    AtmScientificControlValue control =
        base_control (FALSE);
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_control (
            &control,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (artifact);
    g_assert_cmpint (
        artifact->origin,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_ORIGIN_CONTROL_EVIDENCE
    );
    g_assert_cmpstr (
        artifact->source_profile_id,
        ==,
        "atm-scientific-control/v1"
    );
    g_assert_cmpstr (
        artifact->source_locator_kind,
        ==,
        "json_pointer"
    );
    g_assert_cmpstr (
        artifact->source_locator,
        ==,
        "/behavioural_closure/active"
    );
    g_assert_cmpstr (
        artifact->semantic_type,
        ==,
        "rmd.behavioural_closure_active.control"
    );
    g_assert_cmpint (
        artifact->payload_type,
        ==,
        ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN
    );
    g_assert_false (artifact->boolean_value);
    assert_sha256_id (artifact->artifact_id);
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
test_control_value_changes_id (void)
{
    AtmScientificControlValue left =
        base_control (FALSE);
    AtmScientificControlValue right =
        base_control (TRUE);
    AtmScientificArtifact *a = NULL;
    AtmScientificArtifact *b = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_control (
            &left,
            &a,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_scientific_artifact_from_control (
            &right,
            &b,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        a->artifact_id,
        !=,
        b->artifact_id
    );

    atm_scientific_artifact_free (a);
    atm_scientific_artifact_free (b);
}

static void
test_control_metadata_tampering_rejected (void)
{
    AtmScientificControlValue control =
        base_control (FALSE);
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    control.source_path =
        "model/dynamics/other.json";

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
test_artifact_mutation_detected (void)
{
    AtmScientificEvidenceAtom evidence =
        base_evidence ();
    AtmScientificArtifact *artifact = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (artifact->source_path);
    artifact->source_path = g_strdup (
        "science/configs/changed.json"
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
        ATM_SCIENTIFIC_ARTIFACT_ERROR_INTEGRITY
    );
    g_clear_error (&error);

    atm_scientific_artifact_free (artifact);
}

static void
test_recompute_matches_stored_id (void)
{
    AtmScientificEvidenceAtom evidence =
        base_evidence ();
    AtmScientificArtifact *artifact = NULL;
    char *recomputed = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_artifact_from_evidence (
            &evidence,
            &artifact,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_artifact_recompute_id (
            artifact,
            &recomputed,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        recomputed,
        ==,
        artifact->artifact_id
    );

    g_free (recomputed);
    atm_scientific_artifact_free (artifact);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-artifact/typed-evidence",
        test_typed_evidence_artifact
    );
    g_test_add_func (
        "/scientific-artifact/same-input-same-id",
        test_same_evidence_same_id
    );
    g_test_add_func (
        "/scientific-artifact/payload-change",
        test_payload_change_changes_id
    );
    g_test_add_func (
        "/scientific-artifact/provenance-change",
        test_provenance_change_changes_id
    );
    g_test_add_func (
        "/scientific-artifact/cross-repository-profile-rejected",
        test_cross_repository_profile_rejected
    );
    g_test_add_func (
        "/scientific-artifact/non-typed-rejected",
        test_non_typed_evidence_rejected
    );
    g_test_add_func (
        "/scientific-artifact/uppercase-sha-rejected",
        test_uppercase_sha_rejected
    );
    g_test_add_func (
        "/scientific-artifact/control",
        test_control_artifact
    );
    g_test_add_func (
        "/scientific-artifact/control-value-change",
        test_control_value_changes_id
    );
    g_test_add_func (
        "/scientific-artifact/control-metadata-tampering",
        test_control_metadata_tampering_rejected
    );
    g_test_add_func (
        "/scientific-artifact/mutation-detected",
        test_artifact_mutation_detected
    );
    g_test_add_func (
        "/scientific-artifact/recompute",
        test_recompute_matches_stored_id
    );

    return g_test_run ();
}
