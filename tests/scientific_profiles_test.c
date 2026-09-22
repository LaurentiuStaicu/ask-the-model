#include <glib.h>

#include "retrieval_query.h"
#include "scientific_evidence.h"
#include "scientific_profile.h"
#include "scientific_profiles.h"

static AtmEvidenceRecord
base_record (
    const char *repository_id,
    const char *evidence_kind
)
{
    AtmEvidenceRecord record = { 0 };

    record.evidence_kind = (char *) evidence_kind;
    record.repository_id = (char *) repository_id;
    record.repository_version = "test-version";
    record.snapshot_sha =
        "0123456789abcdef0123456789abcdef01234567";
    record.logical_source_id = "test:logical-source";
    record.source_path = "test/source";
    record.locator = "json:/test";
    record.body = "{\"fixture\":true}";

    return record;
}

static AtmScientificEvidenceAtom *
classify (
    AtmScientificProfile *profile,
    AtmEvidenceRecord *record
)
{
    AtmScientificEvidenceAtom *atom = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_evidence_bridge_classify (
            profile,
            record,
            &atom,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (atom);

    return atom;
}

static void
assert_profile_identity (
    AtmScientificProfile *profile,
    const char *profile_id,
    const char *repository_id
)
{
    g_assert_nonnull (profile);
    g_assert_true (atm_scientific_profile_is_frozen (profile));
    g_assert_cmpstr (
        atm_scientific_profile_id (profile),
        ==,
        profile_id
    );
    g_assert_cmpstr (
        atm_scientific_profile_version (profile),
        ==,
        "1"
    );
    g_assert_cmpstr (
        atm_scientific_profile_repository_id (profile),
        ==,
        repository_id
    );
}

static void
test_ewd_profile_whitelist (void)
{
    GError *error = NULL;
    AtmScientificProfile *profile =
        atm_scientific_profile_ewd_v1_new (&error);

    g_assert_no_error (error);
    assert_profile_identity (
        profile,
        "atm-profile/ewd/v1",
        "ewd"
    );

    const struct {
        const char *entity_type;
        const char *semantic_type;
    } entities[] = {
        { "variable", "ewd.variable_record" },
        {
            "candidate_interface",
            "ewd.candidate_interface_record"
        },
        { "promotion_gate", "ewd.promotion_gate_record" }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (entities); i++) {
        AtmEvidenceRecord record = base_record (
            "ewd",
            "entity"
        );
        record.entity_type = (char *) entities[i].entity_type;
        record.native_id = "fixture-id";

        AtmScientificEvidenceAtom *atom = classify (
            profile,
            &record
        );

        g_assert_cmpint (
            atom->status,
            ==,
            ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
        );
        g_assert_cmpstr (
            atom->semantic_type,
            ==,
            entities[i].semantic_type
        );

        atm_scientific_evidence_atom_free (atom);
    }

    const struct {
        const char *dataset_id;
        const char *semantic_type;
    } datasets[] = {
        {
            "ewd:dataset:data/scenarios/fit_diagnostics.csv",
            "ewd.fit_diagnostic_row"
        },
        {
            "ewd:dataset:data/scenarios/parameter_identifiability.csv",
            "ewd.parameter_identifiability_row"
        }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (datasets); i++) {
        AtmEvidenceRecord record = base_record (
            "ewd",
            "dataset_row"
        );
        record.dataset_logical_source_id =
            (char *) datasets[i].dataset_id;
        record.row_key = "fixture-row";

        AtmScientificEvidenceAtom *atom = classify (
            profile,
            &record
        );

        g_assert_cmpint (
            atom->status,
            ==,
            ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
        );
        g_assert_cmpstr (
            atom->semantic_type,
            ==,
            datasets[i].semantic_type
        );

        atm_scientific_evidence_atom_free (atom);
    }

    AtmEvidenceRecord excluded = base_record (
        "ewd",
        "entity"
    );
    excluded.entity_type = "module";
    excluded.native_id = "population";

    AtmScientificEvidenceAtom *excluded_atom = classify (
        profile,
        &excluded
    );

    g_assert_cmpint (
        excluded_atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );
    g_assert_cmpstr (
        excluded_atom->reason_code,
        ==,
        "profile_rule_missing"
    );

    atm_scientific_evidence_atom_free (excluded_atom);
    atm_scientific_profile_free (profile);
}

static void
test_cbd_profile_whitelist (void)
{
    GError *error = NULL;
    AtmScientificProfile *profile =
        atm_scientific_profile_cbd_v1_new (&error);

    g_assert_no_error (error);
    assert_profile_identity (
        profile,
        "atm-profile/cbd/v1",
        "cbd"
    );

    AtmEvidenceRecord variable = base_record (
        "cbd",
        "entity"
    );
    variable.entity_type = "variable";
    variable.native_id = "VAR.FAMILIARITY.CLAIM";

    AtmScientificEvidenceAtom *variable_atom = classify (
        profile,
        &variable
    );

    g_assert_cmpint (
        variable_atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
    );
    g_assert_cmpstr (
        variable_atom->semantic_type,
        ==,
        "cbd.variable_record"
    );
    atm_scientific_evidence_atom_free (variable_atom);

    const char *relation_types[] = {
        "CAUSAL",
        "MODERATING",
        "INFORMATION_FLOW",
        "MEASUREMENT",
        "relation"
    };

    for (gsize i = 0;
         i < G_N_ELEMENTS (relation_types);
         i++) {
        AtmEvidenceRecord relation = base_record (
            "cbd",
            "relation"
        );
        relation.relation_type =
            (char *) relation_types[i];
        relation.native_id = "LINK.FIXTURE";
        relation.from_logical_source_id =
            "cbd:entity:variable:source";
        relation.to_logical_source_id =
            "cbd:entity:variable:target";

        AtmScientificEvidenceAtom *atom = classify (
            profile,
            &relation
        );

        g_assert_cmpint (
            atom->status,
            ==,
            ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
        );
        g_assert_cmpstr (
            atom->semantic_type,
            ==,
            "cbd.relation_record"
        );

        atm_scientific_evidence_atom_free (atom);
    }

    AtmEvidenceRecord row = base_record (
        "cbd",
        "dataset_row"
    );
    row.dataset_logical_source_id =
        "cbd:dataset:model/benchmarks/results/m1_e4_candidate_recovery_authoritative_2026-09-16.csv";
    row.row_key = "EVSD|medium|40|40|5|200";

    AtmScientificEvidenceAtom *row_atom = classify (
        profile,
        &row
    );

    g_assert_cmpint (
        row_atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
    );
    g_assert_cmpstr (
        row_atom->semantic_type,
        ==,
        "cbd.candidate_recovery_row"
    );
    atm_scientific_evidence_atom_free (row_atom);

    AtmEvidenceRecord excluded = base_record (
        "cbd",
        "entity"
    );
    excluded.entity_type = "process";
    excluded.native_id = "PROC.FIXTURE";

    AtmScientificEvidenceAtom *excluded_atom = classify (
        profile,
        &excluded
    );

    g_assert_cmpint (
        excluded_atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );

    atm_scientific_evidence_atom_free (excluded_atom);
    atm_scientific_profile_free (profile);
}

static void
test_rmd_profile_whitelist (void)
{
    GError *error = NULL;
    AtmScientificProfile *profile =
        atm_scientific_profile_rmd_v1_new (&error);

    g_assert_no_error (error);
    assert_profile_identity (
        profile,
        "atm-profile/rmd/v1",
        "rmd"
    );

    const struct {
        const char *entity_type;
        const char *semantic_type;
    } entities[] = {
        { "stock", "rmd.stock_record" },
        { "flow", "rmd.flow_record" },
        { "auxiliary", "rmd.auxiliary_record" },
        { "equation", "rmd.equation_record" }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (entities); i++) {
        AtmEvidenceRecord record = base_record (
            "rmd",
            "entity"
        );
        record.entity_type = (char *) entities[i].entity_type;
        record.native_id = "fixture-id";

        AtmScientificEvidenceAtom *atom = classify (
            profile,
            &record
        );

        g_assert_cmpint (
            atom->status,
            ==,
            ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
        );
        g_assert_cmpstr (
            atom->semantic_type,
            ==,
            entities[i].semantic_type
        );

        atm_scientific_evidence_atom_free (atom);
    }

    AtmEvidenceRecord excluded = base_record (
        "rmd",
        "entity"
    );
    excluded.entity_type = "feedback_loop";
    excluded.native_id = "F.FIXTURE";

    AtmScientificEvidenceAtom *excluded_atom = classify (
        profile,
        &excluded
    );

    g_assert_cmpint (
        excluded_atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );

    atm_scientific_evidence_atom_free (excluded_atom);
    atm_scientific_profile_free (profile);
}

static void
test_exact_dataset_ids_do_not_widen (void)
{
    GError *error = NULL;
    AtmScientificProfile *ewd =
        atm_scientific_profile_ewd_v1_new (&error);

    g_assert_no_error (error);

    AtmEvidenceRecord near_match = base_record (
        "ewd",
        "dataset_row"
    );
    near_match.dataset_logical_source_id =
        "ewd:dataset:data/scenarios/fit_diagnostics_copy.csv";
    near_match.row_key = "population";

    AtmScientificEvidenceAtom *atom = classify (
        ewd,
        &near_match
    );

    g_assert_cmpint (
        atom->status,
        ==,
        ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED
    );
    g_assert_cmpstr (
        atom->reason_code,
        ==,
        "profile_rule_missing"
    );

    atm_scientific_evidence_atom_free (atom);
    atm_scientific_profile_free (ewd);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-profiles/ewd-whitelist",
        test_ewd_profile_whitelist
    );
    g_test_add_func (
        "/scientific-profiles/cbd-whitelist",
        test_cbd_profile_whitelist
    );
    g_test_add_func (
        "/scientific-profiles/rmd-whitelist",
        test_rmd_profile_whitelist
    );
    g_test_add_func (
        "/scientific-profiles/exact-dataset-ids",
        test_exact_dataset_ids_do_not_widen
    );

    return g_test_run ();
}
