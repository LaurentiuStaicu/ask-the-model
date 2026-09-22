#include <glib.h>

#include "retrieval_query.h"
#include "scientific_evidence.h"
#include "scientific_profile.h"

static AtmScientificProfile *
new_test_profile (void)
{
    GError *error = NULL;
    AtmScientificProfile *profile = atm_scientific_profile_new (
        "atm-test-profile",
        "1",
        "fixture",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (profile);

    g_assert_true (
        atm_scientific_profile_add_entity_rule (
            profile,
            "variable",
            "fixture.variable",
            ATM_SCIENTIFIC_REQUIRE_NATIVE_ID |
            ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_profile_add_relation_rule (
            profile,
            "INFLUENCE",
            "fixture.influence",
            ATM_SCIENTIFIC_REQUIRE_NATIVE_ID |
            ATM_SCIENTIFIC_REQUIRE_RELATION_ENDPOINTS |
            ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_profile_add_dataset_row_rule (
            profile,
            "fixture:dataset:data/series.csv",
            "fixture.series_row",
            ATM_SCIENTIFIC_REQUIRE_DATASET_CONTEXT |
            ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_profile_freeze (
            profile,
            &error
        )
    );
    g_assert_no_error (error);

    return profile;
}

static AtmEvidenceRecord
base_record (
    const char *evidence_kind
)
{
    AtmEvidenceRecord record = { 0 };

    record.repository_id = "fixture";
    record.repository_version = "0.1.0";
    record.snapshot_sha =
        "0123456789abcdef0123456789abcdef01234567";
    record.logical_source_id = "fixture:source:test";
    record.source_path = "model/test.json";
    record.locator = "json:/test";
    record.evidence_kind = (char *) evidence_kind;
    record.body = "{\"value\":1}";

    return record;
}

static void
test_known_entity_becomes_typed (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.logical_source_id =
        "fixture:entity:variable:food_per_capita";
    record.entity_type = "variable";
    record.native_id = "food_per_capita";
    record.match_kind = ATM_EVIDENCE_MATCH_EXACT;

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (atom);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
    );
    g_assert_cmpstr (atom->reason_code, ==, "typed_validated");
    g_assert_cmpstr (atom->profile_id, ==, "atm-test-profile");
    g_assert_cmpstr (atom->profile_version, ==, "1");
    g_assert_cmpstr (atom->semantic_type, ==, "fixture.variable");
    g_assert_cmpstr (atom->native_id, ==, "food_per_capita");
    g_assert_cmpstr (
        atom->snapshot_sha,
        ==,
        record.snapshot_sha
    );

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_text_section_remains_textual_only (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("section");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.logical_source_id = "fixture:section:status";
    record.source_path = "STATUS.md";
    record.locator = "lines:1-5";
    record.body = "Narrative repository status.";

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_TEXTUAL_ONLY
    );
    g_assert_cmpstr (atom->reason_code, ==, "textual_evidence");
    g_assert_null (atom->semantic_type);

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_unknown_structured_record_is_unsupported (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.entity_type = "unknown_type";
    record.native_id = "x";

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );
    g_assert_cmpstr (atom->reason_code, ==, "profile_rule_missing");
    g_assert_null (atom->semantic_type);

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_payload_cannot_self_authorize (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.entity_type = "repository_claimed_type";
    record.native_id = "x";
    record.body =
        "{\"entity_type\":\"variable\","
        "\"semantic_type\":\"fixture.variable\","
        "\"profile_id\":\"atm-test-profile\"}";

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );
    g_assert_cmpstr (atom->reason_code, ==, "profile_rule_missing");
    g_assert_null (atom->semantic_type);

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_recognized_malformed_record_is_invalid (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.entity_type = "variable";
    record.native_id = NULL;

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_INVALID
    );
    g_assert_cmpstr (atom->reason_code, ==, "native_id_missing");
    g_assert_null (atom->semantic_type);

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_relation_requires_profile_declared_endpoints (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("relation");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.relation_type = "INFLUENCE";
    record.native_id = "LINK.A.B";
    record.from_logical_source_id = "fixture:entity:variable:a";
    record.to_logical_source_id = NULL;

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_INVALID
    );
    g_assert_cmpstr (
        atom->reason_code,
        ==,
        "relation_endpoint_missing"
    );

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_dataset_row_becomes_typed (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("dataset_row");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.logical_source_id =
        "fixture:dataset:data/series.csv:row:2025";
    record.source_path = "data/series.csv";
    record.locator = "lines:2-2";
    record.dataset_logical_source_id =
        "fixture:dataset:data/series.csv";
    record.row_key = "2025";
    record.body = "{\"year\":\"2025\",\"value\":\"1\"}";

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
    );
    g_assert_cmpstr (
        atom->semantic_type,
        ==,
        "fixture.series_row"
    );
    g_assert_cmpstr (atom->row_key, ==, "2025");

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_retrieval_route_does_not_change_classification (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord exact = base_record ("entity");
    AtmEvidenceRecord lexical = base_record ("entity");
    AtmScientificEvidenceAtom *exact_atom = NULL;
    AtmScientificEvidenceAtom *lexical_atom = NULL;
    GError *error = NULL;

    exact.logical_source_id =
        "fixture:entity:variable:food_per_capita";
    exact.entity_type = "variable";
    exact.native_id = "food_per_capita";
    exact.match_kind = ATM_EVIDENCE_MATCH_EXACT;
    exact.has_lexical_score = FALSE;
    exact.lexical_score = 0.0;

    lexical.logical_source_id = exact.logical_source_id;
    lexical.entity_type = exact.entity_type;
    lexical.native_id = exact.native_id;
    lexical.match_kind = ATM_EVIDENCE_MATCH_LEXICAL;
    lexical.has_lexical_score = TRUE;
    lexical.lexical_score = 0.125;

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &exact,
            &exact_atom,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &lexical,
            &lexical_atom,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        exact_atom->status,
        ==,
        lexical_atom->status
    );
    g_assert_cmpstr (
        exact_atom->semantic_type,
        ==,
        lexical_atom->semantic_type
    );
    g_assert_cmpstr (
        exact_atom->reason_code,
        ==,
        lexical_atom->reason_code
    );

    atm_scientific_evidence_atom_free (exact_atom);
    atm_scientific_evidence_atom_free (lexical_atom);
    atm_scientific_profile_free (profile);
}

static void
test_repository_mismatch_is_unsupported (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.repository_id = "other";
    record.entity_type = "variable";
    record.native_id = "food_per_capita";

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );
    g_assert_cmpstr (
        atom->reason_code,
        ==,
        "profile_repository_mismatch"
    );

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

static void
test_profile_must_be_frozen_and_rules_are_unique (void)
{
    GError *error = NULL;
    AtmScientificProfile *profile = atm_scientific_profile_new (
        "atm-test-profile",
        "1",
        "fixture",
        &error
    );
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;

    g_assert_no_error (error);
    g_assert_nonnull (profile);

    g_assert_true (
        atm_scientific_profile_add_entity_rule (
            profile,
            "variable",
            "fixture.variable",
            ATM_SCIENTIFIC_REQUIRE_NATIVE_ID,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_scientific_profile_add_entity_rule (
            profile,
            "variable",
            "fixture.variable.duplicate",
            ATM_SCIENTIFIC_REQUIRE_NATIVE_ID,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_PROFILE_ERROR,
        ATM_SCIENTIFIC_PROFILE_ERROR_DUPLICATE
    );
    g_clear_error (&error);

    record.entity_type = "variable";
    record.native_id = "x";

    g_assert_false (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_EVIDENCE_ERROR,
        ATM_SCIENTIFIC_EVIDENCE_ERROR_PROFILE
    );
    g_assert_null (atom);
    g_clear_error (&error);

    g_assert_true (
        atm_scientific_profile_freeze (
            profile,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_scientific_profile_add_relation_rule (
            profile,
            "INFLUENCE",
            "fixture.influence",
            ATM_SCIENTIFIC_REQUIRE_NONE,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_PROFILE_ERROR,
        ATM_SCIENTIFIC_PROFILE_ERROR_STATE
    );
    g_clear_error (&error);

    atm_scientific_profile_free (profile);
}

static void
test_incomplete_provenance_is_invalid (void)
{
    AtmScientificProfile *profile = new_test_profile ();
    AtmEvidenceRecord record = base_record ("entity");
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    record.snapshot_sha = NULL;
    record.entity_type = "variable";
    record.native_id = "food_per_capita";

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            &record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_INVALID
    );
    g_assert_cmpstr (
        atom->reason_code,
        ==,
        "incomplete_provenance"
    );

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (profile);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-evidence/known-entity-typed",
        test_known_entity_becomes_typed
    );
    g_test_add_func (
        "/scientific-evidence/textual-only",
        test_text_section_remains_textual_only
    );
    g_test_add_func (
        "/scientific-evidence/unknown-unsupported",
        test_unknown_structured_record_is_unsupported
    );
    g_test_add_func (
        "/scientific-evidence/payload-cannot-self-authorize",
        test_payload_cannot_self_authorize
    );
    g_test_add_func (
        "/scientific-evidence/malformed-invalid",
        test_recognized_malformed_record_is_invalid
    );
    g_test_add_func (
        "/scientific-evidence/relation-endpoints",
        test_relation_requires_profile_declared_endpoints
    );
    g_test_add_func (
        "/scientific-evidence/dataset-row-typed",
        test_dataset_row_becomes_typed
    );
    g_test_add_func (
        "/scientific-evidence/retrieval-route-independent",
        test_retrieval_route_does_not_change_classification
    );
    g_test_add_func (
        "/scientific-evidence/repository-mismatch",
        test_repository_mismatch_is_unsupported
    );
    g_test_add_func (
        "/scientific-evidence/profile-frozen-unique",
        test_profile_must_be_frozen_and_rules_are_unique
    );
    g_test_add_func (
        "/scientific-evidence/incomplete-provenance",
        test_incomplete_provenance_is_invalid
    );

    return g_test_run ();
}
