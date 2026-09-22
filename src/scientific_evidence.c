#include "scientific_evidence.h"

GQuark
atm_scientific_evidence_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-evidence-error-quark"
    );
}

static gboolean
string_is_present (const char *value)
{
    return value != NULL && value[0] != '\0';
}

static char *
duplicate_optional (const char *value)
{
    return value != NULL ? g_strdup (value) : NULL;
}

void
atm_scientific_evidence_atom_free (
    AtmScientificEvidenceAtom *atom
)
{
    if (atom == NULL) {
        return;
    }

    g_free (atom->reason_code);
    g_free (atom->profile_id);
    g_free (atom->profile_version);
    g_free (atom->repository_id);
    g_free (atom->repository_version);
    g_free (atom->snapshot_sha);
    g_free (atom->logical_source_id);
    g_free (atom->source_path);
    g_free (atom->locator);
    g_free (atom->evidence_kind);
    g_free (atom->semantic_type);
    g_free (atom->entity_type);
    g_free (atom->relation_type);
    g_free (atom->native_id);
    g_free (atom->from_logical_source_id);
    g_free (atom->to_logical_source_id);
    g_free (atom->dataset_logical_source_id);
    g_free (atom->row_key);
    g_free (atom->raw_payload);
    g_free (atom);
}

static AtmScientificEvidenceAtom *
new_atom (
    const AtmScientificProfile *profile,
    const AtmEvidenceRecord *record
)
{
    AtmScientificEvidenceAtom *atom = g_new0 (
        AtmScientificEvidenceAtom,
        1
    );

    atom->profile_id = g_strdup (
        atm_scientific_profile_id (profile)
    );
    atom->profile_version = g_strdup (
        atm_scientific_profile_version (profile)
    );

    atom->repository_id = duplicate_optional (
        record->repository_id
    );
    atom->repository_version = duplicate_optional (
        record->repository_version
    );
    atom->snapshot_sha = duplicate_optional (
        record->snapshot_sha
    );
    atom->logical_source_id = duplicate_optional (
        record->logical_source_id
    );
    atom->source_path = duplicate_optional (
        record->source_path
    );
    atom->locator = duplicate_optional (
        record->locator
    );
    atom->evidence_kind = duplicate_optional (
        record->evidence_kind
    );

    atom->entity_type = duplicate_optional (
        record->entity_type
    );
    atom->relation_type = duplicate_optional (
        record->relation_type
    );
    atom->native_id = duplicate_optional (
        record->native_id
    );
    atom->from_logical_source_id = duplicate_optional (
        record->from_logical_source_id
    );
    atom->to_logical_source_id = duplicate_optional (
        record->to_logical_source_id
    );
    atom->dataset_logical_source_id = duplicate_optional (
        record->dataset_logical_source_id
    );
    atom->row_key = duplicate_optional (
        record->row_key
    );
    atom->raw_payload = duplicate_optional (
        record->body
    );

    return atom;
}

static void
set_status (
    AtmScientificEvidenceAtom *atom,
    AtmScientificEvidenceStatus status,
    const char *reason_code
)
{
    atom->status = status;
    g_free (atom->reason_code);
    atom->reason_code = g_strdup (reason_code);
}

static gboolean
provenance_is_complete (
    const AtmEvidenceRecord *record
)
{
    return
        string_is_present (record->repository_id) &&
        string_is_present (record->repository_version) &&
        string_is_present (record->snapshot_sha) &&
        string_is_present (record->logical_source_id) &&
        string_is_present (record->source_path) &&
        string_is_present (record->locator) &&
        string_is_present (record->evidence_kind);
}

static gboolean
requirements_are_satisfied (
    guint requirements,
    const AtmEvidenceRecord *record,
    const char **out_reason_code
)
{
    if ((requirements & ATM_SCIENTIFIC_REQUIRE_NATIVE_ID) != 0 &&
        !string_is_present (record->native_id)) {
        *out_reason_code = "native_id_missing";
        return FALSE;
    }

    if ((requirements &
         ATM_SCIENTIFIC_REQUIRE_RELATION_ENDPOINTS) != 0 &&
        (!string_is_present (record->from_logical_source_id) ||
         !string_is_present (record->to_logical_source_id))) {
        *out_reason_code = "relation_endpoint_missing";
        return FALSE;
    }

    if ((requirements &
         ATM_SCIENTIFIC_REQUIRE_DATASET_CONTEXT) != 0 &&
        (!string_is_present (record->dataset_logical_source_id) ||
         !string_is_present (record->row_key))) {
        *out_reason_code = "dataset_context_missing";
        return FALSE;
    }

    if ((requirements & ATM_SCIENTIFIC_REQUIRE_PAYLOAD) != 0 &&
        !string_is_present (record->body)) {
        *out_reason_code = "payload_missing";
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_scientific_evidence_bridge_classify (
    const AtmScientificProfile *profile,
    const AtmEvidenceRecord *record,
    AtmScientificEvidenceAtom **out_atom,
    GError **error
)
{
    const char *selector = NULL;
    const char *semantic_type = NULL;
    const char *reason_code = NULL;
    guint requirements = ATM_SCIENTIFIC_REQUIRE_NONE;
    AtmScientificRuleKind rule_kind;

    if (profile == NULL ||
        record == NULL ||
        out_atom == NULL ||
        *out_atom != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_EVIDENCE_ERROR,
            ATM_SCIENTIFIC_EVIDENCE_ERROR_ARGUMENT,
            "Scientific evidence bridge received invalid arguments."
        );
        return FALSE;
    }

    if (!atm_scientific_profile_is_frozen (profile)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_EVIDENCE_ERROR,
            ATM_SCIENTIFIC_EVIDENCE_ERROR_PROFILE,
            "Scientific evidence classification requires a frozen AtM profile."
        );
        return FALSE;
    }

    AtmScientificEvidenceAtom *atom = new_atom (
        profile,
        record
    );

    if (!provenance_is_complete (record)) {
        set_status (
            atom,
            ATM_SCIENTIFIC_EVIDENCE_INVALID,
            "incomplete_provenance"
        );
        *out_atom = atom;
        return TRUE;
    }

    if (g_strcmp0 (
            record->repository_id,
            atm_scientific_profile_repository_id (profile)
        ) != 0) {
        set_status (
            atom,
            ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED,
            "profile_repository_mismatch"
        );
        *out_atom = atom;
        return TRUE;
    }

    if (g_strcmp0 (record->evidence_kind, "section") == 0) {
        set_status (
            atom,
            ATM_SCIENTIFIC_EVIDENCE_TEXTUAL_ONLY,
            "textual_evidence"
        );
        *out_atom = atom;
        return TRUE;
    }

    if (g_strcmp0 (record->evidence_kind, "entity") == 0) {
        if (!string_is_present (record->entity_type)) {
            set_status (
                atom,
                ATM_SCIENTIFIC_EVIDENCE_INVALID,
                "entity_type_missing"
            );
            *out_atom = atom;
            return TRUE;
        }

        rule_kind = ATM_SCIENTIFIC_RULE_ENTITY;
        selector = record->entity_type;
    } else if (
        g_strcmp0 (record->evidence_kind, "relation") == 0
    ) {
        if (!string_is_present (record->relation_type)) {
            set_status (
                atom,
                ATM_SCIENTIFIC_EVIDENCE_INVALID,
                "relation_type_missing"
            );
            *out_atom = atom;
            return TRUE;
        }

        rule_kind = ATM_SCIENTIFIC_RULE_RELATION;
        selector = record->relation_type;
    } else if (
        g_strcmp0 (record->evidence_kind, "dataset_row") == 0
    ) {
        if (!string_is_present (
                record->dataset_logical_source_id
            )) {
            set_status (
                atom,
                ATM_SCIENTIFIC_EVIDENCE_INVALID,
                "dataset_source_missing"
            );
            *out_atom = atom;
            return TRUE;
        }

        if (!string_is_present (record->row_key)) {
            set_status (
                atom,
                ATM_SCIENTIFIC_EVIDENCE_INVALID,
                "dataset_row_key_missing"
            );
            *out_atom = atom;
            return TRUE;
        }

        rule_kind = ATM_SCIENTIFIC_RULE_DATASET_ROW;
        selector = record->dataset_logical_source_id;
    } else {
        set_status (
            atom,
            ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED,
            "unsupported_evidence_kind"
        );
        *out_atom = atom;
        return TRUE;
    }

    if (!atm_scientific_profile_lookup (
            profile,
            rule_kind,
            selector,
            &semantic_type,
            &requirements
        )) {
        set_status (
            atom,
            ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED,
            "profile_rule_missing"
        );
        *out_atom = atom;
        return TRUE;
    }

    if (!requirements_are_satisfied (
            requirements,
            record,
            &reason_code
        )) {
        set_status (
            atom,
            ATM_SCIENTIFIC_EVIDENCE_INVALID,
            reason_code
        );
        *out_atom = atom;
        return TRUE;
    }

    atom->semantic_type = g_strdup (semantic_type);
    set_status (
        atom,
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED,
        "typed_validated"
    );

    *out_atom = atom;
    return TRUE;
}
