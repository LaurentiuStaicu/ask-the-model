#include "scientific_result.h"

#include "scientific_canonical.h"

#include <json-glib/json-glib.h>
#include <string.h>

GQuark
atm_sra_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-sra-error-quark"
    );
}

static gboolean
nonempty_utf8 (const char *value)
{
    return value != NULL &&
        value[0] != '\0' &&
        g_utf8_validate (value, -1, NULL);
}

static gboolean
optional_utf8 (const char *value)
{
    return value == NULL ||
        g_utf8_validate (value, -1, NULL);
}

static gboolean
hex64_is_valid (const char *value)
{
    if (value == NULL || strlen (value) != 64) {
        return FALSE;
    }

    for (guint i = 0; i < 64; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (value[i] >= 'A' && value[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static GPtrArray *
string_array_new (void)
{
    return g_ptr_array_new_with_free_func (g_free);
}

static gint
compare_strings (
    gconstpointer left,
    gconstpointer right
)
{
    const char *a = *(const char *const *) left;
    const char *b = *(const char *const *) right;

    return strcmp (a, b);
}

static void
sort_deduplicate_strings (GPtrArray *values)
{
    g_ptr_array_sort (values, compare_strings);

    for (gint i = (gint) values->len - 1; i > 0; i--) {
        const char *current = g_ptr_array_index (
            values,
            (guint) i
        );
        const char *previous = g_ptr_array_index (
            values,
            (guint) i - 1
        );

        if (strcmp (current, previous) == 0) {
            g_ptr_array_remove_index (
                values,
                (guint) i
            );
        }
    }
}

static gboolean
add_unique_string (
    AtmSraResult *result,
    GPtrArray *array,
    const char *value,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        array == NULL ||
        !nonempty_utf8 (value)) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_STATE,
            "SRA string mutation received invalid or finalized state."
        );
        return FALSE;
    }

    g_ptr_array_add (array, g_strdup (value));
    return TRUE;
}

static void
qualification_free (
    AtmSraQualificationEnvelope *qualification
)
{
    if (qualification == NULL) {
        return;
    }

    g_free (qualification->schema);
    g_clear_pointer (
        &qualification->canonical_obligations,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &qualification->repository_snapshots,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &qualification->evidence_atom_ids,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &qualification->control_evidence_ids,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &qualification->semantic_profiles,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &qualification->operations,
        g_ptr_array_unref
    );
    g_free (qualification->numeric_profile);
    g_clear_pointer (
        &qualification->temporal_dependencies,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &qualification->stochastic_dependencies,
        g_ptr_array_unref
    );
    g_free (qualification->scientific_content_id);
    g_free (qualification->qualified_artifact_id);
    g_free (qualification);
}

AtmSraResult *
atm_sra_result_new (
    AtmSraAnswerability answerability
)
{
    AtmSraResult *result = g_new0 (
        AtmSraResult,
        1
    );

    result->answerability = answerability;
    result->established_facts =
        g_ptr_array_new_with_free_func (
            (GDestroyNotify)
                atm_sra_established_fact_free
        );
    result->derived_facts = string_array_new ();
    result->constraint_results =
        g_ptr_array_new_with_free_func (
            (GDestroyNotify)
                atm_sra_constraint_free
        );
    result->conflicts =
        g_ptr_array_new_with_free_func (
            (GDestroyNotify)
                atm_sra_conflict_free
        );
    result->limitations = string_array_new ();
    result->qualification = g_new0 (
        AtmSraQualificationEnvelope,
        1
    );
    result->qualification->schema =
        g_strdup (ATM_SRA_SCHEMA_ID);
    result->qualification->canonical_obligations =
        string_array_new ();
    result->qualification->repository_snapshots =
        string_array_new ();
    result->qualification->evidence_atom_ids =
        string_array_new ();
    result->qualification->control_evidence_ids =
        string_array_new ();
    result->qualification->semantic_profiles =
        string_array_new ();
    result->qualification->operations =
        string_array_new ();
    result->qualification->temporal_dependencies =
        string_array_new ();
    result->qualification->stochastic_dependencies =
        string_array_new ();

    return result;
}

void
atm_sra_result_free (AtmSraResult *result)
{
    if (result == NULL) {
        return;
    }

    g_clear_pointer (
        &result->established_facts,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &result->derived_facts,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &result->constraint_results,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &result->conflicts,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &result->limitations,
        g_ptr_array_unref
    );
    qualification_free (result->qualification);
    g_free (result);
}

AtmSraEstablishedFact *
atm_sra_established_fact_new (
    const char *fact_id,
    const char *fact_type,
    const char *subject_id,
    const char *attribute,
    const char *value,
    const char *unit,
    const char *dimension,
    const char *qualifiers_json
)
{
    if (!nonempty_utf8 (fact_id) ||
        !nonempty_utf8 (fact_type) ||
        !nonempty_utf8 (subject_id) ||
        !nonempty_utf8 (attribute) ||
        !nonempty_utf8 (value) ||
        !optional_utf8 (unit) ||
        !optional_utf8 (dimension) ||
        !optional_utf8 (qualifiers_json)) {
        return NULL;
    }

    AtmSraEstablishedFact *fact = g_new0 (
        AtmSraEstablishedFact,
        1
    );

    fact->fact_id = g_strdup (fact_id);
    fact->fact_type = g_strdup (fact_type);
    fact->subject_id = g_strdup (subject_id);
    fact->attribute = g_strdup (attribute);
    fact->value = g_strdup (value);
    fact->unit = g_strdup (unit);
    fact->dimension = g_strdup (dimension);
    fact->qualifiers_json = g_strdup (
        qualifiers_json
    );

    if (qualifiers_json != NULL) {
        GError *canonical_error = NULL;

        if (!atm_scientific_content_id_json (
                "atm.sra.qualifiers.v1",
                qualifiers_json,
                &fact->qualifiers_content_id,
                &canonical_error
            )) {
            g_clear_error (&canonical_error);
            atm_sra_established_fact_free (fact);
            return NULL;
        }
    }

    fact->support = string_array_new ();
    return fact;
}

void
atm_sra_established_fact_free (
    AtmSraEstablishedFact *fact
)
{
    if (fact == NULL) {
        return;
    }

    g_free (fact->fact_id);
    g_free (fact->fact_type);
    g_free (fact->subject_id);
    g_free (fact->attribute);
    g_free (fact->value);
    g_free (fact->unit);
    g_free (fact->dimension);
    g_free (fact->qualifiers_json);
    g_free (fact->qualifiers_content_id);
    g_clear_pointer (&fact->support, g_ptr_array_unref);
    g_free (fact);
}

static gboolean
add_support (
    GPtrArray *support,
    const char *qualified_artifact_id,
    GError **error
)
{
    if (support == NULL ||
        !hex64_is_valid (qualified_artifact_id)) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "SRA support must be a canonical 64-hex qualified artifact ID."
        );
        return FALSE;
    }

    g_ptr_array_add (
        support,
        g_strdup (qualified_artifact_id)
    );
    return TRUE;
}

gboolean
atm_sra_fact_add_support (
    AtmSraEstablishedFact *fact,
    const char *qualified_artifact_id,
    GError **error
)
{
    return fact != NULL &&
        add_support (
            fact->support,
            qualified_artifact_id,
            error
        );
}

gboolean
atm_sra_result_add_fact (
    AtmSraResult *result,
    AtmSraEstablishedFact *fact,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        fact == NULL ||
        fact->support->len == 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "Established SRA facts require typed support before insertion."
        );
        return FALSE;
    }

    g_ptr_array_add (
        result->established_facts,
        fact
    );
    return TRUE;
}

AtmSraConstraintResult *
atm_sra_constraint_new (
    const char *constraint_id,
    AtmSraConstraintStatus status,
    const char *reason_code
)
{
    if (!nonempty_utf8 (constraint_id) ||
        !nonempty_utf8 (reason_code)) {
        return NULL;
    }

    AtmSraConstraintResult *constraint = g_new0 (
        AtmSraConstraintResult,
        1
    );

    constraint->constraint_id = g_strdup (
        constraint_id
    );
    constraint->status = status;
    constraint->reason_code = g_strdup (
        reason_code
    );
    constraint->support = string_array_new ();
    return constraint;
}

void
atm_sra_constraint_free (
    AtmSraConstraintResult *constraint
)
{
    if (constraint == NULL) {
        return;
    }

    g_free (constraint->constraint_id);
    g_free (constraint->reason_code);
    g_clear_pointer (
        &constraint->support,
        g_ptr_array_unref
    );
    g_free (constraint);
}

gboolean
atm_sra_constraint_add_support (
    AtmSraConstraintResult *constraint,
    const char *qualified_artifact_id,
    GError **error
)
{
    return constraint != NULL &&
        add_support (
            constraint->support,
            qualified_artifact_id,
            error
        );
}

gboolean
atm_sra_result_add_constraint (
    AtmSraResult *result,
    AtmSraConstraintResult *constraint,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        constraint == NULL ||
        constraint->support->len == 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "SRA constraints require deterministic support before insertion."
        );
        return FALSE;
    }

    g_ptr_array_add (
        result->constraint_results,
        constraint
    );
    return TRUE;
}

AtmSraConflict *
atm_sra_conflict_new (
    const char *conflict_id,
    const char *reason_code
)
{
    if (!nonempty_utf8 (conflict_id) ||
        !nonempty_utf8 (reason_code)) {
        return NULL;
    }

    AtmSraConflict *conflict = g_new0 (
        AtmSraConflict,
        1
    );

    conflict->conflict_id = g_strdup (conflict_id);
    conflict->reason_code = g_strdup (reason_code);
    conflict->support = string_array_new ();
    return conflict;
}

void
atm_sra_conflict_free (
    AtmSraConflict *conflict
)
{
    if (conflict == NULL) {
        return;
    }

    g_free (conflict->conflict_id);
    g_free (conflict->reason_code);
    g_clear_pointer (
        &conflict->support,
        g_ptr_array_unref
    );
    g_free (conflict);
}

gboolean
atm_sra_conflict_add_support (
    AtmSraConflict *conflict,
    const char *qualified_artifact_id,
    GError **error
)
{
    return conflict != NULL &&
        add_support (
            conflict->support,
            qualified_artifact_id,
            error
        );
}

gboolean
atm_sra_result_add_conflict (
    AtmSraResult *result,
    AtmSraConflict *conflict,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        conflict == NULL ||
        conflict->support->len == 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "SRA conflicts require explicit typed support."
        );
        return FALSE;
    }

    g_ptr_array_add (
        result->conflicts,
        conflict
    );
    return TRUE;
}

gboolean
atm_sra_result_add_limitation (
    AtmSraResult *result,
    const char *limitation,
    GError **error
)
{
    return add_unique_string (
        result,
        result != NULL
            ? result->limitations
            : NULL,
        limitation,
        error
    );
}

gboolean
atm_sra_qualification_add_canonical_obligation (
    AtmSraResult *result,
    const char *obligation,
    GError **error
)
{
    return add_unique_string (
        result,
        result != NULL
            ? result->qualification
                ->canonical_obligations
            : NULL,
        obligation,
        error
    );
}

gboolean
atm_sra_qualification_add_repository_snapshot (
    AtmSraResult *result,
    const char *repository_snapshot,
    GError **error
)
{
    return add_unique_string (
        result,
        result != NULL
            ? result->qualification
                ->repository_snapshots
            : NULL,
        repository_snapshot,
        error
    );
}

static gboolean
add_validated_artifact (
    AtmSraResult *result,
    const AtmScientificArtifact *artifact,
    AtmScientificArtifactKind expected_kind,
    GPtrArray *target,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        artifact == NULL ||
        target == NULL) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "SRA qualification artifact admission received invalid state."
        );
        return FALSE;
    }

    GError *artifact_error = NULL;

    if (!atm_scientific_artifact_validate (
            artifact,
            &artifact_error
        )) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "SRA support artifact failed SCI-04 validation: %s.",
            artifact_error != NULL
                ? artifact_error->message
                : "unknown artifact validation error"
        );
        g_clear_error (&artifact_error);
        return FALSE;
    }

    if (artifact->kind != expected_kind ||
        !hex64_is_valid (
            artifact->qualified_artifact_id
        )) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "SRA support artifact kind or qualified identity is invalid for this qualification set."
        );
        return FALSE;
    }

    return add_unique_string (
        result,
        target,
        artifact->qualified_artifact_id,
        error
    );
}

gboolean
atm_sra_qualification_add_evidence_artifact (
    AtmSraResult *result,
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    return add_validated_artifact (
        result,
        artifact,
        ATM_SCIENTIFIC_ARTIFACT_KIND_EVIDENCE,
        result != NULL
            ? result->qualification
                ->evidence_atom_ids
            : NULL,
        error
    );
}

gboolean
atm_sra_qualification_add_control_artifact (
    AtmSraResult *result,
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    return add_validated_artifact (
        result,
        artifact,
        ATM_SCIENTIFIC_ARTIFACT_KIND_CONTROL,
        result != NULL
            ? result->qualification
                ->control_evidence_ids
            : NULL,
        error
    );
}

gboolean
atm_sra_qualification_add_semantic_profile (
    AtmSraResult *result,
    const char *semantic_profile,
    GError **error
)
{
    return add_unique_string (
        result,
        result != NULL
            ? result->qualification
                ->semantic_profiles
            : NULL,
        semantic_profile,
        error
    );
}

gboolean
atm_sra_qualification_add_operation (
    AtmSraResult *result,
    const char *operation,
    GError **error
)
{
    return add_unique_string (
        result,
        result != NULL
            ? result->qualification
                ->operations
            : NULL,
        operation,
        error
    );
}

gboolean
atm_sra_qualification_set_numeric_profile (
    AtmSraResult *result,
    const char *numeric_profile,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        !nonempty_utf8 (numeric_profile)) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_STATE,
            "SRA numeric profile mutation is invalid."
        );
        return FALSE;
    }

    g_free (result->qualification->numeric_profile);
    result->qualification->numeric_profile =
        g_strdup (numeric_profile);
    return TRUE;
}

static gint
compare_facts (
    gconstpointer left,
    gconstpointer right
)
{
    const AtmSraEstablishedFact *a =
        *(AtmSraEstablishedFact *const *) left;
    const AtmSraEstablishedFact *b =
        *(AtmSraEstablishedFact *const *) right;

    return strcmp (a->fact_id, b->fact_id);
}

static gint
compare_constraints (
    gconstpointer left,
    gconstpointer right
)
{
    const AtmSraConstraintResult *a =
        *(AtmSraConstraintResult *const *) left;
    const AtmSraConstraintResult *b =
        *(AtmSraConstraintResult *const *) right;

    return strcmp (
        a->constraint_id,
        b->constraint_id
    );
}

static gint
compare_conflicts (
    gconstpointer left,
    gconstpointer right
)
{
    const AtmSraConflict *a =
        *(AtmSraConflict *const *) left;
    const AtmSraConflict *b =
        *(AtmSraConflict *const *) right;

    return strcmp (
        a->conflict_id,
        b->conflict_id
    );
}

static const char *
answerability_name (AtmSraAnswerability value)
{
    switch (value) {
        case ATM_SRA_ANSWERED:
            return "ANSWERED";
        case ATM_SRA_PARTIAL:
            return "PARTIAL";
        case ATM_SRA_NOT_ANSWERABLE:
            return "NOT_ANSWERABLE";
        case ATM_SRA_BLOCKED:
            return "BLOCKED";
        default:
            return NULL;
    }
}

static const char *
constraint_status_name (
    AtmSraConstraintStatus value
)
{
    switch (value) {
        case ATM_SRA_CONSTRAINT_PASS:
            return "PASS";
        case ATM_SRA_CONSTRAINT_DENY:
            return "DENY";
        case ATM_SRA_CONSTRAINT_NOT_EVALUATED:
            return "NOT_EVALUATED";
        default:
            return NULL;
    }
}

static void
json_add_string_array (
    JsonBuilder *builder,
    const char *name,
    GPtrArray *array
)
{
    json_builder_set_member_name (builder, name);
    json_builder_begin_array (builder);

    for (guint i = 0; i < array->len; i++) {
        json_builder_add_string_value (
            builder,
            g_ptr_array_index (array, i)
        );
    }

    json_builder_end_array (builder);
}

static char *
content_json (const AtmSraResult *result)
{
    JsonBuilder *builder = json_builder_new ();
    JsonGenerator *generator = json_generator_new ();
    JsonNode *root;

    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "answerability"
    );
    json_builder_add_string_value (
        builder,
        answerability_name (
            result->answerability
        )
    );

    json_builder_set_member_name (
        builder,
        "established_facts"
    );
    json_builder_begin_array (builder);

    for (guint i = 0;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *fact =
            g_ptr_array_index (
                result->established_facts,
                i
            );

        json_builder_begin_object (builder);
        json_builder_set_member_name (
            builder,
            "fact_id"
        );
        json_builder_add_string_value (
            builder,
            fact->fact_id
        );
        json_builder_set_member_name (
            builder,
            "fact_type"
        );
        json_builder_add_string_value (
            builder,
            fact->fact_type
        );
        json_builder_set_member_name (
            builder,
            "subject_id"
        );
        json_builder_add_string_value (
            builder,
            fact->subject_id
        );
        json_builder_set_member_name (
            builder,
            "attribute"
        );
        json_builder_add_string_value (
            builder,
            fact->attribute
        );
        json_builder_set_member_name (
            builder,
            "value"
        );
        json_builder_add_string_value (
            builder,
            fact->value
        );

        if (fact->unit != NULL) {
            json_builder_set_member_name (
                builder,
                "unit"
            );
            json_builder_add_string_value (
                builder,
                fact->unit
            );
        }

        if (fact->dimension != NULL) {
            json_builder_set_member_name (
                builder,
                "dimension"
            );
            json_builder_add_string_value (
                builder,
                fact->dimension
            );
        }

        if (fact->qualifiers_content_id != NULL) {
            json_builder_set_member_name (
                builder,
                "qualifiers_content_id"
            );
            json_builder_add_string_value (
                builder,
                fact->qualifiers_content_id
            );
        }

        json_builder_end_object (builder);
    }

    json_builder_end_array (builder);
    json_add_string_array (
        builder,
        "derived_facts",
        result->derived_facts
    );

    json_builder_set_member_name (
        builder,
        "constraint_results"
    );
    json_builder_begin_array (builder);

    for (guint i = 0;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *constraint =
            g_ptr_array_index (
                result->constraint_results,
                i
            );

        json_builder_begin_object (builder);
        json_builder_set_member_name (
            builder,
            "constraint_id"
        );
        json_builder_add_string_value (
            builder,
            constraint->constraint_id
        );
        json_builder_set_member_name (
            builder,
            "status"
        );
        json_builder_add_string_value (
            builder,
            constraint_status_name (
                constraint->status
            )
        );
        json_builder_set_member_name (
            builder,
            "reason_code"
        );
        json_builder_add_string_value (
            builder,
            constraint->reason_code
        );
        json_builder_end_object (builder);
    }

    json_builder_end_array (builder);

    json_builder_set_member_name (
        builder,
        "conflicts"
    );
    json_builder_begin_array (builder);

    for (guint i = 0;
         i < result->conflicts->len;
         i++) {
        AtmSraConflict *conflict =
            g_ptr_array_index (
                result->conflicts,
                i
            );

        json_builder_begin_object (builder);
        json_builder_set_member_name (
            builder,
            "conflict_id"
        );
        json_builder_add_string_value (
            builder,
            conflict->conflict_id
        );
        json_builder_set_member_name (
            builder,
            "reason_code"
        );
        json_builder_add_string_value (
            builder,
            conflict->reason_code
        );
        json_builder_end_object (builder);
    }

    json_builder_end_array (builder);
    json_add_string_array (
        builder,
        "limitations",
        result->limitations
    );

    json_builder_end_object (builder);

    root = json_builder_get_root (builder);
    json_generator_set_root (generator, root);
    char *json = json_generator_to_data (
        generator,
        NULL
    );

    json_node_free (root);
    g_object_unref (generator);
    g_object_unref (builder);
    return json;
}

static char *
qualification_json (
    const AtmSraResult *result,
    const char *scientific_content_id
)
{
    AtmSraQualificationEnvelope *q =
        result->qualification;
    JsonBuilder *builder = json_builder_new ();
    JsonGenerator *generator = json_generator_new ();

    json_builder_begin_object (builder);
    json_builder_set_member_name (
        builder,
        "schema"
    );
    json_builder_add_string_value (
        builder,
        q->schema
    );
    json_builder_set_member_name (
        builder,
        "scientific_content_id"
    );
    json_builder_add_string_value (
        builder,
        scientific_content_id
    );
    json_add_string_array (
        builder,
        "canonical_obligations",
        q->canonical_obligations
    );
    json_add_string_array (
        builder,
        "repository_snapshots",
        q->repository_snapshots
    );
    json_add_string_array (
        builder,
        "evidence_atom_ids",
        q->evidence_atom_ids
    );
    json_add_string_array (
        builder,
        "control_evidence_ids",
        q->control_evidence_ids
    );
    json_add_string_array (
        builder,
        "semantic_profiles",
        q->semantic_profiles
    );
    json_add_string_array (
        builder,
        "operations",
        q->operations
    );
    json_builder_set_member_name (
        builder,
        "numeric_profile"
    );
    json_builder_add_string_value (
        builder,
        q->numeric_profile
    );
    json_add_string_array (
        builder,
        "temporal_dependencies",
        q->temporal_dependencies
    );
    json_add_string_array (
        builder,
        "stochastic_dependencies",
        q->stochastic_dependencies
    );

    json_builder_set_member_name (
        builder,
        "fact_support"
    );
    json_builder_begin_array (builder);
    for (guint i = 0;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *fact =
            g_ptr_array_index (
                result->established_facts,
                i
            );

        json_builder_begin_object (builder);
        json_builder_set_member_name (
            builder,
            "fact_id"
        );
        json_builder_add_string_value (
            builder,
            fact->fact_id
        );
        json_add_string_array (
            builder,
            "support",
            fact->support
        );
        json_builder_end_object (builder);
    }
    json_builder_end_array (builder);

    json_builder_set_member_name (
        builder,
        "constraint_support"
    );
    json_builder_begin_array (builder);
    for (guint i = 0;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *constraint =
            g_ptr_array_index (
                result->constraint_results,
                i
            );

        json_builder_begin_object (builder);
        json_builder_set_member_name (
            builder,
            "constraint_id"
        );
        json_builder_add_string_value (
            builder,
            constraint->constraint_id
        );
        json_add_string_array (
            builder,
            "support",
            constraint->support
        );
        json_builder_end_object (builder);
    }
    json_builder_end_array (builder);

    json_builder_set_member_name (
        builder,
        "conflict_support"
    );
    json_builder_begin_array (builder);
    for (guint i = 0;
         i < result->conflicts->len;
         i++) {
        AtmSraConflict *conflict =
            g_ptr_array_index (
                result->conflicts,
                i
            );

        json_builder_begin_object (builder);
        json_builder_set_member_name (
            builder,
            "conflict_id"
        );
        json_builder_add_string_value (
            builder,
            conflict->conflict_id
        );
        json_add_string_array (
            builder,
            "support",
            conflict->support
        );
        json_builder_end_object (builder);
    }
    json_builder_end_array (builder);

    json_builder_end_object (builder);

    JsonNode *root = json_builder_get_root (
        builder
    );
    json_generator_set_root (generator, root);
    char *json = json_generator_to_data (
        generator,
        NULL
    );

    json_node_free (root);
    g_object_unref (generator);
    g_object_unref (builder);
    return json;
}

static gboolean
has_duplicate_fact_ids (AtmSraResult *result)
{
    for (guint i = 1;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *previous =
            g_ptr_array_index (
                result->established_facts,
                i - 1
            );
        AtmSraEstablishedFact *current =
            g_ptr_array_index (
                result->established_facts,
                i
            );

        if (strcmp (
                previous->fact_id,
                current->fact_id
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
has_duplicate_constraint_ids (AtmSraResult *result)
{
    for (guint i = 1;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *previous =
            g_ptr_array_index (
                result->constraint_results,
                i - 1
            );
        AtmSraConstraintResult *current =
            g_ptr_array_index (
                result->constraint_results,
                i
            );

        if (strcmp (
                previous->constraint_id,
                current->constraint_id
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
has_duplicate_conflict_ids (AtmSraResult *result)
{
    for (guint i = 1;
         i < result->conflicts->len;
         i++) {
        AtmSraConflict *previous =
            g_ptr_array_index (
                result->conflicts,
                i - 1
            );
        AtmSraConflict *current =
            g_ptr_array_index (
                result->conflicts,
                i
            );

        if (strcmp (
                previous->conflict_id,
                current->conflict_id
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
qualification_contains_support (
    AtmSraQualificationEnvelope *qualification,
    const char *support_id
)
{
    for (guint i = 0;
         i < qualification->evidence_atom_ids->len;
         i++) {
        if (g_strcmp0 (
                support_id,
                g_ptr_array_index (
                    qualification->evidence_atom_ids,
                    i
                )
            ) == 0) {
            return TRUE;
        }
    }

    for (guint i = 0;
         i < qualification->control_evidence_ids->len;
         i++) {
        if (g_strcmp0 (
                support_id,
                g_ptr_array_index (
                    qualification->control_evidence_ids,
                    i
                )
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
support_is_qualified (
    AtmSraResult *result,
    GPtrArray *support,
    GError **error
)
{
    for (guint i = 0; i < support->len; i++) {
        const char *support_id = g_ptr_array_index (
            support,
            i
        );

        if (!qualification_contains_support (
                result->qualification,
                support_id
            )) {
            g_set_error (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_SUPPORT,
                "Support ID '%s' is not present in the qualification evidence/control sets.",
                support_id
            );
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
pre_sci06_shape_valid (
    AtmSraResult *result,
    GError **error
)
{
    if (answerability_name (
            result->answerability
        ) == NULL) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SHAPE,
            "SRA answerability value is invalid."
        );
        return FALSE;
    }

    for (guint i = 0;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *constraint =
            g_ptr_array_index (
                result->constraint_results,
                i
            );

        if (constraint_status_name (
                constraint->status
            ) == NULL) {
            g_set_error_literal (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_SHAPE,
                "SRA constraint status is invalid."
            );
            return FALSE;
        }
    }

    if (result->derived_facts->len != 0 ||
        result->qualification->operations->len != 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "SCI-05 forbids derived facts and operation claims before SCI-06."
        );
        return FALSE;
    }

    if (!nonempty_utf8 (
            result->qualification
                ->numeric_profile
        ) ||
        result->qualification
            ->canonical_obligations->len == 0 ||
        result->qualification
            ->repository_snapshots->len == 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SHAPE,
            "SRA qualification is missing required canonical, snapshot or numeric-profile fields."
        );
        return FALSE;
    }

    if ((result->answerability ==
            ATM_SRA_ANSWERED ||
         result->answerability ==
            ATM_SRA_PARTIAL) &&
        result->established_facts->len == 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SHAPE,
            "Answered or partial SRA results require at least one established fact."
        );
        return FALSE;
    }

    for (guint i = 0;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *fact =
            g_ptr_array_index (
                result->established_facts,
                i
            );

        if (!support_is_qualified (
                result,
                fact->support,
                error
            )) {
            return FALSE;
        }
    }

    for (guint i = 0;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *constraint =
            g_ptr_array_index (
                result->constraint_results,
                i
            );

        if (!support_is_qualified (
                result,
                constraint->support,
                error
            )) {
            return FALSE;
        }
    }

    for (guint i = 0;
         i < result->conflicts->len;
         i++) {
        AtmSraConflict *conflict =
            g_ptr_array_index (
                result->conflicts,
                i
            );

        if (!support_is_qualified (
                result,
                conflict->support,
                error
            )) {
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
atm_sra_result_finalize (
    AtmSraResult *result,
    GError **error
)
{
    if (result == NULL || result->finalized) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_STATE,
            "SRA result is missing or already finalized."
        );
        return FALSE;
    }

    for (guint i = 0;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *fact =
            g_ptr_array_index (
                result->established_facts,
                i
            );
        sort_deduplicate_strings (fact->support);

        if (fact->support->len == 0) {
            g_set_error_literal (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_SUPPORT,
                "Established SRA fact lost all support."
            );
            return FALSE;
        }
    }

    for (guint i = 0;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *constraint =
            g_ptr_array_index (
                result->constraint_results,
                i
            );
        sort_deduplicate_strings (
            constraint->support
        );
    }

    for (guint i = 0;
         i < result->conflicts->len;
         i++) {
        AtmSraConflict *conflict =
            g_ptr_array_index (
                result->conflicts,
                i
            );
        sort_deduplicate_strings (
            conflict->support
        );
    }

    g_ptr_array_sort (
        result->established_facts,
        compare_facts
    );
    g_ptr_array_sort (
        result->constraint_results,
        compare_constraints
    );
    g_ptr_array_sort (
        result->conflicts,
        compare_conflicts
    );
    sort_deduplicate_strings (
        result->limitations
    );

    AtmSraQualificationEnvelope *q =
        result->qualification;
    sort_deduplicate_strings (
        q->canonical_obligations
    );
    sort_deduplicate_strings (
        q->repository_snapshots
    );
    sort_deduplicate_strings (
        q->evidence_atom_ids
    );
    sort_deduplicate_strings (
        q->control_evidence_ids
    );
    sort_deduplicate_strings (
        q->semantic_profiles
    );
    sort_deduplicate_strings (q->operations);
    sort_deduplicate_strings (
        q->temporal_dependencies
    );
    sort_deduplicate_strings (
        q->stochastic_dependencies
    );

    if (has_duplicate_fact_ids (result) ||
        has_duplicate_constraint_ids (result) ||
        has_duplicate_conflict_ids (result) ||
        !pre_sci06_shape_valid (
            result,
            error
        )) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_SHAPE,
                "SRA result contains duplicate semantic identifiers."
            );
        }
        return FALSE;
    }

    char *content = content_json (result);
    GError *canonical_error = NULL;

    if (!atm_scientific_content_id_json (
            "atm.sra.result.v1",
            content,
            &q->scientific_content_id,
            &canonical_error
        )) {
        g_free (content);
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_IDENTITY,
            "Could not canonicalize SRA scientific content: %s.",
            canonical_error != NULL
                ? canonical_error->message
                : "unknown error"
        );
        g_clear_error (&canonical_error);
        return FALSE;
    }

    char *qualification = qualification_json (
        result,
        q->scientific_content_id
    );

    if (!atm_scientific_content_id_json (
            "atm.sra.qualification.v1",
            qualification,
            &q->qualified_artifact_id,
            &canonical_error
        )) {
        g_free (qualification);
        g_free (content);
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_IDENTITY,
            "Could not canonicalize SRA qualification envelope: %s.",
            canonical_error != NULL
                ? canonical_error->message
                : "unknown error"
        );
        g_clear_error (&canonical_error);
        g_clear_pointer (
            &q->scientific_content_id,
            g_free
        );
        return FALSE;
    }

    g_free (qualification);
    g_free (content);
    result->finalized = TRUE;
    return TRUE;
}

gboolean
atm_sra_result_validate (
    const AtmSraResult *result,
    GError **error
)
{
    if (result == NULL ||
        !result->finalized ||
        result->qualification == NULL ||
        !hex64_is_valid (
            result->qualification
                ->scientific_content_id
        ) ||
        !hex64_is_valid (
            result->qualification
                ->qualified_artifact_id
        )) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_STATE,
            "SRA result is not a finalized qualified artifact."
        );
        return FALSE;
    }

    AtmSraResult *mutable_result =
        (AtmSraResult *) result;

    if (!pre_sci06_shape_valid (
            mutable_result,
            error
        )) {
        return FALSE;
    }

    char *content = content_json (result);
    char *expected_content_id = NULL;
    GError *canonical_error = NULL;

    if (!atm_scientific_content_id_json (
            "atm.sra.result.v1",
            content,
            &expected_content_id,
            &canonical_error
        )) {
        g_free (content);
        goto identity_failure;
    }

    char *qualification = qualification_json (
        result,
        expected_content_id
    );
    char *expected_qualified_id = NULL;

    if (!atm_scientific_content_id_json (
            "atm.sra.qualification.v1",
            qualification,
            &expected_qualified_id,
            &canonical_error
        )) {
        g_free (qualification);
        g_free (content);
        g_free (expected_content_id);
        goto identity_failure;
    }

    gboolean matches =
        g_strcmp0 (
            expected_content_id,
            result->qualification
                ->scientific_content_id
        ) == 0 &&
        g_strcmp0 (
            expected_qualified_id,
            result->qualification
                ->qualified_artifact_id
        ) == 0;

    g_free (expected_qualified_id);
    g_free (qualification);
    g_free (expected_content_id);
    g_free (content);

    if (!matches) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_IDENTITY,
            "SRA identities do not match current result contents."
        );
        return FALSE;
    }

    return TRUE;

identity_failure:
    g_set_error (
        error,
        ATM_SRA_ERROR,
        ATM_SRA_ERROR_IDENTITY,
        "Could not recompute SRA identity: %s.",
        canonical_error != NULL
            ? canonical_error->message
            : "unknown canonicalization error"
    );
    g_clear_error (&canonical_error);
    return FALSE;
}