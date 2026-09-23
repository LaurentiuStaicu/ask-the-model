#include <glib.h>
#include <json-glib/json-glib.h>

#include "scientific_artifact.h"
#include "scientific_result.h"

static const char *
required_string (
    JsonObject *object,
    const char *name
)
{
    g_assert_true (
        json_object_has_member (object, name)
    );
    const char *value =
        json_object_get_string_member (
            object,
            name
        );
    g_assert_nonnull (value);
    return value;
}

static AtmSraAnswerability
answerability_from_string (const char *value)
{
    if (g_strcmp0 (value, "ANSWERED") == 0) {
        return ATM_SRA_ANSWERED;
    }
    if (g_strcmp0 (value, "PARTIAL") == 0) {
        return ATM_SRA_PARTIAL;
    }
    if (g_strcmp0 (
            value,
            "NOT_ANSWERABLE"
        ) == 0) {
        return ATM_SRA_NOT_ANSWERABLE;
    }
    if (g_strcmp0 (value, "BLOCKED") == 0) {
        return ATM_SRA_BLOCKED;
    }

    g_assert_not_reached ();
}

static const char *
profile_id_for_repo (const char *repo)
{
    if (g_strcmp0 (repo, "ewd") == 0) {
        return "atm-profile/ewd/v1";
    }
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
    if (g_strcmp0 (repo, "ewd") == 0) {
        return "atm-profile/ewd/v1@1";
    }
    if (g_strcmp0 (repo, "cbd") == 0) {
        return "atm-profile/cbd/v1@1";
    }
    if (g_strcmp0 (repo, "rmd") == 0) {
        return "atm-profile/rmd/v1@1";
    }

    g_assert_not_reached ();
}

static const char *
version_for_repo (
    JsonObject *manifest,
    const char *repo
)
{
    JsonObject *pinned =
        json_object_get_object_member (
            manifest,
            "pinned_repositories"
        );
    JsonObject *entry =
        json_object_get_object_member (
            pinned,
            repo
        );

    return required_string (entry, "version");
}

static const char *
sha_for_repo (
    JsonObject *manifest,
    const char *repo
)
{
    JsonObject *pinned =
        json_object_get_object_member (
            manifest,
            "pinned_repositories"
        );
    JsonObject *entry =
        json_object_get_object_member (
            pinned,
            repo
        );

    return required_string (entry, "sha");
}

static char *
json_object_payload (JsonObject *object)
{
    JsonNode *node = json_node_new (
        JSON_NODE_OBJECT
    );
    json_node_set_object (node, object);
    char *json = json_to_string (node, FALSE);
    json_node_free (node);
    return json;
}

static AtmScientificArtifact *
evidence_artifact_for_case (
    JsonObject *manifest,
    JsonObject *gold_case
)
{
    const char *repo = required_string (
        gold_case,
        "repository_id"
    );
    AtmScientificEvidenceAtom evidence = { 0 };

    evidence.status =
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    evidence.reason_code = "typed_validated";
    evidence.profile_id =
        (char *) profile_id_for_repo (repo);
    evidence.profile_version = "1";
    evidence.repository_id = (char *) repo;
    evidence.repository_version =
        (char *) version_for_repo (
            manifest,
            repo
        );
    evidence.snapshot_sha =
        (char *) sha_for_repo (
            manifest,
            repo
        );
    evidence.logical_source_id =
        (char *) required_string (
            gold_case,
            "logical_source_id"
        );
    evidence.source_path =
        (char *) required_string (
            gold_case,
            "source_path"
        );
    evidence.locator =
        (char *) required_string (
            gold_case,
            "locator"
        );
    evidence.evidence_kind = "gold";
    evidence.semantic_type =
        (char *) required_string (
            gold_case,
            "artifact_class"
        );

    JsonObject *payload_object =
        json_object_get_object_member (
            gold_case,
            "payload"
        );
    char *payload = json_object_payload (
        payload_object
    );
    evidence.raw_payload = payload;

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
    g_free (payload);
    return artifact;
}

static AtmScientificArtifact *
control_artifact_for_case (
    JsonObject *manifest,
    JsonObject *gold_case
)
{
    AtmScientificControlValue control = { 0 };

    control.control_id =
        ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE;
    control.repository_id = "rmd";
    control.snapshot_sha =
        (char *) sha_for_repo (
            manifest,
            "rmd"
        );
    control.source_path =
        (char *) required_string (
            gold_case,
            "source_path"
        );
    control.json_pointer =
        (char *) required_string (
            gold_case,
            "locator"
        );
    control.value_type =
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN;
    control.boolean_value =
        g_strcmp0 (
            required_string (
                gold_case,
                "value"
            ),
            "true"
        ) == 0;

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
    return artifact;
}

static void
apply_policy (
    JsonObject *manifest,
    JsonObject *gold_case,
    AtmSraResult *result,
    AtmScientificArtifact *artifact,
    gboolean control
)
{
    GError *error = NULL;
    JsonObject *policy =
        json_object_get_object_member (
            manifest,
            "policy"
        );
    JsonArray *obligations =
        json_object_get_array_member (
            policy,
            "canonical_obligations"
        );

    for (guint i = 0;
         i < json_array_get_length (obligations);
         i++) {
        g_assert_true (
            atm_sra_qualification_add_canonical_obligation (
                result,
                json_array_get_string_element (
                    obligations,
                    i
                ),
                &error
            )
        );
        g_assert_no_error (error);
    }

    const char *repo = required_string (
        gold_case,
        "repository_id"
    );
    char *snapshot = g_strdup_printf (
        "%s@%s#%s",
        repo,
        version_for_repo (
            manifest,
            repo
        ),
        sha_for_repo (
            manifest,
            repo
        )
    );

    g_assert_true (
        atm_sra_qualification_add_repository_snapshot (
            result,
            snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_free (snapshot);

    if (artifact != NULL) {
        if (control) {
            g_assert_true (
                atm_sra_qualification_add_control_artifact (
                    result,
                    artifact,
                    &error
                )
            );
        } else {
            g_assert_true (
                atm_sra_qualification_add_evidence_artifact (
                    result,
                    artifact,
                    &error
                )
            );
        }
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
            required_string (
                policy,
                "numeric_profile"
            ),
            &error
        )
    );
    g_assert_no_error (error);
}

static void
add_expected_facts (
    JsonObject *gold_case,
    AtmScientificArtifact *artifact,
    AtmSraResult *result
)
{
    JsonArray *facts =
        json_object_get_array_member (
            gold_case,
            "expected_facts"
        );
    GError *error = NULL;

    for (guint i = 0;
         i < json_array_get_length (facts);
         i++) {
        JsonObject *spec =
            json_array_get_object_element (
                facts,
                i
            );
        const char *unit =
            json_object_has_member (
                spec,
                "unit"
            )
            ? json_object_get_string_member (
                spec,
                "unit"
            )
            : NULL;
        const char *dimension =
            json_object_has_member (
                spec,
                "dimension"
            )
            ? json_object_get_string_member (
                spec,
                "dimension"
            )
            : NULL;

        AtmSraEstablishedFact *fact =
            atm_sra_established_fact_new (
                required_string (
                    spec,
                    "fact_id"
                ),
                required_string (
                    spec,
                    "fact_type"
                ),
                required_string (
                    spec,
                    "subject_id"
                ),
                required_string (
                    spec,
                    "attribute"
                ),
                required_string (
                    spec,
                    "value"
                ),
                unit,
                dimension,
                NULL
            );

        g_assert_nonnull (fact);
        g_assert_true (
            atm_sra_fact_add_support (
                fact,
                artifact->qualified_artifact_id,
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
    }
}

static void
verify_expected_facts (
    JsonObject *gold_case,
    AtmSraResult *result
)
{
    JsonArray *facts =
        json_object_get_array_member (
            gold_case,
            "expected_facts"
        );

    g_assert_cmpuint (
        result->established_facts->len,
        ==,
        json_array_get_length (facts)
    );

    for (guint i = 0;
         i < json_array_get_length (facts);
         i++) {
        JsonObject *expected =
            json_array_get_object_element (
                facts,
                i
            );
        const char *fact_id =
            required_string (
                expected,
                "fact_id"
            );
        gboolean found = FALSE;

        for (guint j = 0;
             j < result->established_facts->len;
             j++) {
            AtmSraEstablishedFact *fact =
                g_ptr_array_index (
                    result->established_facts,
                    j
                );

            if (g_strcmp0 (
                    fact->fact_id,
                    fact_id
                ) == 0) {
                g_assert_cmpstr (
                    fact->value,
                    ==,
                    required_string (
                        expected,
                        "value"
                    )
                );
                found = TRUE;
                break;
            }
        }

        g_assert_true (found);
    }
}

static void
run_supported_case (
    JsonObject *manifest,
    JsonObject *gold_case
)
{
    AtmScientificArtifact *artifact =
        evidence_artifact_for_case (
            manifest,
            gold_case
        );
    AtmSraResult *result =
        atm_sra_result_new (
            answerability_from_string (
                required_string (
                    gold_case,
                    "answerability"
                )
            )
        );

    apply_policy (
        manifest,
        gold_case,
        result,
        artifact,
        FALSE
    );
    add_expected_facts (
        gold_case,
        artifact,
        result
    );

    GError *error = NULL;

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
    g_assert_cmpuint (
        result->derived_facts->len,
        ==,
        0
    );
    g_assert_cmpuint (
        result->qualification->operations->len,
        ==,
        0
    );
    verify_expected_facts (
        gold_case,
        result
    );

    atm_sra_result_free (result);
    atm_scientific_artifact_free (artifact);
}

static void
run_control_case (
    JsonObject *manifest,
    JsonObject *gold_case
)
{
    AtmScientificArtifact *artifact =
        control_artifact_for_case (
            manifest,
            gold_case
        );
    AtmSraResult *result =
        atm_sra_result_new (ATM_SRA_BLOCKED);

    apply_policy (
        manifest,
        gold_case,
        result,
        artifact,
        TRUE
    );

    JsonObject *expected =
        json_object_get_object_member (
            gold_case,
            "expected_constraint"
        );
    AtmSraConstraintResult *constraint =
        atm_sra_constraint_new (
            required_string (
                expected,
                "constraint_id"
            ),
            ATM_SRA_CONSTRAINT_DENY,
            required_string (
                expected,
                "reason_code"
            )
        );
    GError *error = NULL;

    g_assert_true (
        atm_sra_constraint_add_support (
            constraint,
            artifact->qualified_artifact_id,
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

    g_assert_cmpuint (
        result->constraint_results->len,
        ==,
        1
    );
    AtmSraConstraintResult *actual =
        g_ptr_array_index (
            result->constraint_results,
            0
        );
    g_assert_cmpint (
        actual->status,
        ==,
        ATM_SRA_CONSTRAINT_DENY
    );
    g_assert_cmpstr (
        actual->reason_code,
        ==,
        "CONTROL_CONSTRAINT_DENIED"
    );

    atm_sra_result_free (result);
    atm_scientific_artifact_free (artifact);
}

static void
run_unsupported_case (
    JsonObject *manifest,
    JsonObject *gold_case
)
{
    AtmSraResult *result =
        atm_sra_result_new (
            ATM_SRA_NOT_ANSWERABLE
        );

    apply_policy (
        manifest,
        gold_case,
        result,
        NULL,
        FALSE
    );

    GError *error = NULL;
    g_assert_true (
        atm_sra_result_add_limitation (
            result,
            required_string (
                gold_case,
                "expected_reason_code"
            ),
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
    g_assert_cmpuint (
        result->established_facts->len,
        ==,
        0
    );
    g_assert_cmpuint (
        result->qualification->evidence_atom_ids->len,
        ==,
        0
    );
    g_assert_cmpstr (
        g_ptr_array_index (
            result->limitations,
            0
        ),
        ==,
        "UNSUPPORTED_SEMANTICS"
    );

    atm_sra_result_free (result);
}

static void
test_gold_v0 (void)
{
    const char *path = g_getenv (
        "ATM_SCIENTIFIC_GOLD_V0"
    );
    g_assert_nonnull (path);

    JsonParser *parser = json_parser_new ();
    GError *error = NULL;

    g_assert_true (
        json_parser_load_from_file (
            parser,
            path,
            &error
        )
    );
    g_assert_no_error (error);

    JsonObject *manifest =
        json_node_get_object (
            json_parser_get_root (parser)
        );

    g_assert_cmpstr (
        required_string (manifest, "schema"),
        ==,
        "atm-scientific-gold/0"
    );

    JsonObject *policy =
        json_object_get_object_member (
            manifest,
            "policy"
        );
    g_assert_false (
        json_object_get_boolean_member (
            policy,
            "llm_required"
        )
    );
    g_assert_false (
        json_object_get_boolean_member (
            policy,
            "derived_facts_allowed"
        )
    );
    g_assert_false (
        json_object_get_boolean_member (
            policy,
            "operations_allowed"
        )
    );

    JsonArray *cases =
        json_object_get_array_member (
            manifest,
            "cases"
        );
    g_assert_cmpuint (
        json_array_get_length (cases),
        ==,
        7
    );

    for (guint i = 0;
         i < json_array_get_length (cases);
         i++) {
        JsonObject *gold_case =
            json_array_get_object_element (
                cases,
                i
            );
        const char *id = required_string (
            gold_case,
            "id"
        );

        if (g_strcmp0 (id, "SG-005") == 0) {
            run_control_case (
                manifest,
                gold_case
            );
        } else if (
            g_strcmp0 (id, "SG-007") == 0
        ) {
            run_unsupported_case (
                manifest,
                gold_case
            );
        } else {
            run_supported_case (
                manifest,
                gold_case
            );
        }
    }

    g_object_unref (parser);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-gold/v0",
        test_gold_v0
    );

    return g_test_run ();
}