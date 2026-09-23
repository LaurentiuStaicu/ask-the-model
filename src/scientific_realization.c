#include "scientific_realization.h"

#include <string.h>

struct AtmScientificRealizationBlock {
    AtmScientificRealizationBlockKind kind;
    char *block_id;
    char *template_id;
    char *source_id;

    char *fact_type;
    char *subject_id;
    char *attribute;
    char *value;
    char *unit;
    char *dimension;
    char *qualifiers_json;

    char *status;
    char *reason_code;
    char *operation;
    char *operation_numeric_profile;

    GPtrArray *support;
};

struct AtmScientificRealizationView {
    char *schema;
    AtmSraAnswerability answerability;
    char *scientific_content_id;
    char *qualified_artifact_id;
    GPtrArray *blocks;
    GPtrArray *mandatory_block_ids;
};

struct AtmScientificDirectRender {
    char *schema;
    AtmScientificRenderLanguage language;
    char *text;
    char *scientific_content_id;
    char *qualified_artifact_id;
    GPtrArray *rendered_block_ids;
    GPtrArray *support_ids;
};

GQuark
atm_scientific_realization_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-realization-error-quark"
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

static GPtrArray *
string_array_new (void)
{
    return g_ptr_array_new_with_free_func (g_free);
}

static void
copy_strings (
    GPtrArray *destination,
    GPtrArray *source
)
{
    if (source == NULL) {
        return;
    }

    for (guint i = 0; i < source->len; i++) {
        g_ptr_array_add (
            destination,
            g_strdup (
                g_ptr_array_index (source, i)
            )
        );
    }
}

static gint
compare_string_pointers (
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
    g_ptr_array_sort (
        values,
        compare_string_pointers
    );

    for (gint i = (gint) values->len - 1;
         i > 0;
         i--) {
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

static void
block_free (AtmScientificRealizationBlock *block)
{
    if (block == NULL) {
        return;
    }

    g_free (block->block_id);
    g_free (block->template_id);
    g_free (block->source_id);
    g_free (block->fact_type);
    g_free (block->subject_id);
    g_free (block->attribute);
    g_free (block->value);
    g_free (block->unit);
    g_free (block->dimension);
    g_free (block->qualifiers_json);
    g_free (block->status);
    g_free (block->reason_code);
    g_free (block->operation);
    g_free (block->operation_numeric_profile);
    g_clear_pointer (&block->support, g_ptr_array_unref);
    g_free (block);
}

static AtmScientificRealizationBlock *
block_new (
    AtmScientificRealizationBlockKind kind,
    const char *block_id,
    const char *template_id,
    const char *source_id
)
{
    AtmScientificRealizationBlock *block = g_new0 (
        AtmScientificRealizationBlock,
        1
    );

    block->kind = kind;
    block->block_id = g_strdup (block_id);
    block->template_id = g_strdup (template_id);
    block->source_id = g_strdup (source_id);
    block->support = string_array_new ();
    return block;
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
constraint_status_name (AtmSraConstraintStatus status)
{
    switch (status) {
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

static const char *
outcome_name (AtmScientificOperationOutcome outcome)
{
    switch (outcome) {
        case ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC:
            return "NUMERIC";
        case ATM_SCIENTIFIC_OPERATION_OUTCOME_BOOLEAN:
            return "BOOLEAN";
        case ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING:
            return "MISSING";
        default:
            return NULL;
    }
}

static gboolean
add_block (
    AtmScientificRealizationView *view,
    AtmScientificRealizationBlock *block,
    GError **error
)
{
    if (view == NULL ||
        block == NULL ||
        !nonempty_utf8 (block->block_id) ||
        !nonempty_utf8 (block->template_id)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_REALIZATION_ERROR,
            ATM_SCIENTIFIC_REALIZATION_ERROR_VIEW,
            "Scientific realization block is incomplete."
        );
        return FALSE;
    }

    for (guint i = 0;
         i < view->blocks->len;
         i++) {
        AtmScientificRealizationBlock *existing =
            g_ptr_array_index (
                view->blocks,
                i
            );

        if (g_strcmp0 (
                existing->block_id,
                block->block_id
            ) == 0) {
            g_set_error (
                error,
                ATM_SCIENTIFIC_REALIZATION_ERROR,
                ATM_SCIENTIFIC_REALIZATION_ERROR_VIEW,
                "Duplicate realization block ID '%s'.",
                block->block_id
            );
            return FALSE;
        }
    }

    g_ptr_array_add (
        view->blocks,
        block
    );
    g_ptr_array_add (
        view->mandatory_block_ids,
        g_strdup (block->block_id)
    );
    return TRUE;
}

static gboolean
add_answerability_block (
    AtmScientificRealizationView *view,
    GError **error
)
{
    const char *status = answerability_name (
        view->answerability
    );

    if (status == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_REALIZATION_ERROR,
            ATM_SCIENTIFIC_REALIZATION_ERROR_SRA,
            "SRA contains an unknown answerability state."
        );
        return FALSE;
    }

    AtmScientificRealizationBlock *block = block_new (
        ATM_SCIENTIFIC_REALIZATION_BLOCK_ANSWERABILITY,
        "answerability",
        "answerability.v1",
        NULL
    );
    block->status = g_strdup (status);

    if (!add_block (view, block, error)) {
        block_free (block);
        return FALSE;
    }

    return TRUE;
}

static gboolean
add_established_fact_blocks (
    AtmScientificRealizationView *view,
    const AtmSraResult *result,
    GError **error
)
{
    for (guint i = 0;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *fact =
            g_ptr_array_index (
                result->established_facts,
                i
            );
        char *block_id = g_strdup_printf (
            "fact:%s",
            fact->fact_id
        );
        AtmScientificRealizationBlock *block =
            block_new (
                ATM_SCIENTIFIC_REALIZATION_BLOCK_ESTABLISHED_FACT,
                block_id,
                "established_fact.v1",
                fact->fact_id
            );

        g_free (block_id);
        block->fact_type = g_strdup (fact->fact_type);
        block->subject_id = g_strdup (fact->subject_id);
        block->attribute = g_strdup (fact->attribute);
        block->value = g_strdup (fact->value);
        block->unit = g_strdup (fact->unit);
        block->dimension = g_strdup (fact->dimension);
        block->qualifiers_json = g_strdup (
            fact->qualifiers_json
        );
        copy_strings (
            block->support,
            fact->support
        );

        if (!add_block (view, block, error)) {
            block_free (block);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
add_derived_fact_blocks (
    AtmScientificRealizationView *view,
    const AtmSraResult *result,
    GError **error
)
{
    for (guint i = 0;
         i < result->derived_facts->len;
         i++) {
        AtmSraDerivedFact *fact =
            g_ptr_array_index (
                result->derived_facts,
                i
            );
        const char *outcome = outcome_name (
            fact->outcome
        );

        if (outcome == NULL) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_REALIZATION_ERROR,
                ATM_SCIENTIFIC_REALIZATION_ERROR_SRA,
                "SRA contains an unknown derived outcome."
            );
            return FALSE;
        }

        char *block_id = g_strdup_printf (
            "derived:%s",
            fact->fact_id
        );
        AtmScientificRealizationBlock *block =
            block_new (
                ATM_SCIENTIFIC_REALIZATION_BLOCK_DERIVED_FACT,
                block_id,
                "derived_fact.v1",
                fact->fact_id
            );

        g_free (block_id);
        block->fact_type = g_strdup (fact->fact_type);
        block->subject_id = g_strdup (fact->subject_id);
        block->attribute = g_strdup (fact->attribute);
        block->value = g_strdup (fact->value);
        block->unit = g_strdup (fact->unit);
        block->dimension = g_strdup (fact->dimension);
        block->status = g_strdup (outcome);
        block->reason_code = g_strdup (fact->reason_code);
        block->operation = g_strdup (fact->operation);
        block->operation_numeric_profile = g_strdup (
            fact->operation_numeric_profile
        );
        copy_strings (
            block->support,
            fact->support
        );

        if (!add_block (view, block, error)) {
            block_free (block);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
add_constraint_blocks (
    AtmScientificRealizationView *view,
    const AtmSraResult *result,
    GError **error
)
{
    for (guint i = 0;
         i < result->constraint_results->len;
         i++) {
        AtmSraConstraintResult *constraint =
            g_ptr_array_index (
                result->constraint_results,
                i
            );
        const char *status = constraint_status_name (
            constraint->status
        );

        if (status == NULL) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_REALIZATION_ERROR,
                ATM_SCIENTIFIC_REALIZATION_ERROR_SRA,
                "SRA contains an unknown constraint status."
            );
            return FALSE;
        }

        char *block_id = g_strdup_printf (
            "constraint:%s",
            constraint->constraint_id
        );
        AtmScientificRealizationBlock *block =
            block_new (
                ATM_SCIENTIFIC_REALIZATION_BLOCK_CONSTRAINT,
                block_id,
                "constraint.v1",
                constraint->constraint_id
            );

        g_free (block_id);
        block->status = g_strdup (status);
        block->reason_code = g_strdup (
            constraint->reason_code
        );
        copy_strings (
            block->support,
            constraint->support
        );

        if (!add_block (view, block, error)) {
            block_free (block);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
add_conflict_blocks (
    AtmScientificRealizationView *view,
    const AtmSraResult *result,
    GError **error
)
{
    for (guint i = 0;
         i < result->conflicts->len;
         i++) {
        AtmSraConflict *conflict =
            g_ptr_array_index (
                result->conflicts,
                i
            );
        char *block_id = g_strdup_printf (
            "conflict:%s",
            conflict->conflict_id
        );
        AtmScientificRealizationBlock *block =
            block_new (
                ATM_SCIENTIFIC_REALIZATION_BLOCK_CONFLICT,
                block_id,
                "conflict.v1",
                conflict->conflict_id
            );

        g_free (block_id);
        block->reason_code = g_strdup (
            conflict->reason_code
        );
        copy_strings (
            block->support,
            conflict->support
        );

        if (!add_block (view, block, error)) {
            block_free (block);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
add_limitation_blocks (
    AtmScientificRealizationView *view,
    const AtmSraResult *result,
    GError **error
)
{
    for (guint i = 0;
         i < result->limitations->len;
         i++) {
        const char *limitation = g_ptr_array_index (
            result->limitations,
            i
        );
        char *block_id = g_strdup_printf (
            "limitation:%u",
            i
        );
        AtmScientificRealizationBlock *block =
            block_new (
                ATM_SCIENTIFIC_REALIZATION_BLOCK_LIMITATION,
                block_id,
                "limitation.v1",
                NULL
            );

        g_free (block_id);
        block->value = g_strdup (limitation);

        if (!add_block (view, block, error)) {
            block_free (block);
            return FALSE;
        }
    }

    return TRUE;
}

AtmScientificRealizationView *
atm_scientific_realization_view_new (
    const AtmSraResult *result,
    GError **error
)
{
    if (result == NULL ||
        !result->finalized ||
        result->qualification == NULL ||
        !nonempty_utf8 (
            result->qualification->scientific_content_id
        ) ||
        !nonempty_utf8 (
            result->qualification->qualified_artifact_id
        )) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_REALIZATION_ERROR,
            ATM_SCIENTIFIC_REALIZATION_ERROR_ARGUMENT,
            "Scientific realization requires a finalized SRA."
        );
        return NULL;
    }

    GError *validation_error = NULL;

    if (!atm_sra_result_validate (
            result,
            &validation_error
        )) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_REALIZATION_ERROR,
            ATM_SCIENTIFIC_REALIZATION_ERROR_SRA,
            "Scientific realization rejected an invalid SRA: %s.",
            validation_error != NULL
                ? validation_error->message
                : "unknown SRA validation error"
        );
        g_clear_error (&validation_error);
        return NULL;
    }

    AtmScientificRealizationView *view = g_new0 (
        AtmScientificRealizationView,
        1
    );
    view->schema = g_strdup (
        ATM_SCIENTIFIC_REALIZATION_VIEW_SCHEMA
    );
    view->answerability = result->answerability;
    view->scientific_content_id = g_strdup (
        result->qualification->scientific_content_id
    );
    view->qualified_artifact_id = g_strdup (
        result->qualification->qualified_artifact_id
    );
    view->blocks = g_ptr_array_new_with_free_func (
        (GDestroyNotify) block_free
    );
    view->mandatory_block_ids = string_array_new ();

    if (!add_answerability_block (view, error) ||
        !add_established_fact_blocks (
            view,
            result,
            error
        ) ||
        !add_derived_fact_blocks (
            view,
            result,
            error
        ) ||
        !add_constraint_blocks (
            view,
            result,
            error
        ) ||
        !add_conflict_blocks (
            view,
            result,
            error
        ) ||
        !add_limitation_blocks (
            view,
            result,
            error
        ) ||
        !atm_scientific_realization_view_validate (
            view,
            error
        )) {
        atm_scientific_realization_view_free (view);
        return NULL;
    }

    return view;
}

void
atm_scientific_realization_view_free (
    AtmScientificRealizationView *view
)
{
    if (view == NULL) {
        return;
    }

    g_free (view->schema);
    g_free (view->scientific_content_id);
    g_free (view->qualified_artifact_id);
    g_clear_pointer (&view->blocks, g_ptr_array_unref);
    g_clear_pointer (
        &view->mandatory_block_ids,
        g_ptr_array_unref
    );
    g_free (view);
}

gboolean
atm_scientific_realization_view_validate (
    const AtmScientificRealizationView *view,
    GError **error
)
{
    if (view == NULL ||
        g_strcmp0 (
            view->schema,
            ATM_SCIENTIFIC_REALIZATION_VIEW_SCHEMA
        ) != 0 ||
        !nonempty_utf8 (view->scientific_content_id) ||
        !nonempty_utf8 (view->qualified_artifact_id) ||
        view->blocks == NULL ||
        view->mandatory_block_ids == NULL ||
        view->blocks->len == 0 ||
        view->blocks->len !=
            view->mandatory_block_ids->len) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_REALIZATION_ERROR,
            ATM_SCIENTIFIC_REALIZATION_ERROR_VIEW,
            "Scientific realization view has an invalid shape."
        );
        return FALSE;
    }

    GHashTable *seen = g_hash_table_new (
        g_str_hash,
        g_str_equal
    );

    for (guint i = 0; i < view->blocks->len; i++) {
        AtmScientificRealizationBlock *block =
            g_ptr_array_index (
                view->blocks,
                i
            );
        const char *mandatory =
            g_ptr_array_index (
                view->mandatory_block_ids,
                i
            );

        if (block == NULL ||
            !nonempty_utf8 (block->block_id) ||
            !nonempty_utf8 (block->template_id) ||
            g_strcmp0 (
                mandatory,
                block->block_id
            ) != 0 ||
            g_hash_table_contains (
                seen,
                block->block_id
            )) {
            g_hash_table_unref (seen);
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_REALIZATION_ERROR,
                ATM_SCIENTIFIC_REALIZATION_ERROR_VIEW,
                "Scientific realization blocks are not a unique mandatory sequence."
            );
            return FALSE;
        }

        if (!optional_utf8 (block->source_id) ||
            !optional_utf8 (block->fact_type) ||
            !optional_utf8 (block->subject_id) ||
            !optional_utf8 (block->attribute) ||
            !optional_utf8 (block->value) ||
            !optional_utf8 (block->unit) ||
            !optional_utf8 (block->dimension) ||
            !optional_utf8 (block->qualifiers_json) ||
            !optional_utf8 (block->status) ||
            !optional_utf8 (block->reason_code) ||
            !optional_utf8 (block->operation) ||
            !optional_utf8 (
                block->operation_numeric_profile
            )) {
            g_hash_table_unref (seen);
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_REALIZATION_ERROR,
                ATM_SCIENTIFIC_REALIZATION_ERROR_VIEW,
                "Scientific realization block contains invalid UTF-8."
            );
            return FALSE;
        }

        g_hash_table_add (
            seen,
            block->block_id
        );
    }

    g_hash_table_unref (seen);
    return TRUE;
}

static char *
escape_text (const char *value)
{
    if (value == NULL) {
        return NULL;
    }

    GString *escaped = g_string_new (NULL);

    for (const guint8 *cursor =
             (const guint8 *) value;
         *cursor != 0;
         cursor++) {
        guint8 byte = *cursor;

        switch (byte) {
            case '\\':
                g_string_append (escaped, "\\\\");
                break;
            case '\n':
                g_string_append (escaped, "\\n");
                break;
            case '\r':
                g_string_append (escaped, "\\r");
                break;
            case '\t':
                g_string_append (escaped, "\\t");
                break;
            default:
                if (byte < 0x20 || byte == 0x7f) {
                    g_string_append_printf (
                        escaped,
                        "\\x%02X",
                        byte
                    );
                } else {
                    g_string_append_c (
                        escaped,
                        (char) byte
                    );
                }
                break;
        }
    }

    return g_string_free (escaped, FALSE);
}

static void
append_field (
    GString *text,
    const char *label,
    const char *value
)
{
    if (value == NULL) {
        return;
    }

    char *escaped = escape_text (value);

    g_string_append_printf (
        text,
        "; %s=%s",
        label,
        escaped
    );
    g_free (escaped);
}

static void
render_block_line (
    GString *text,
    const AtmScientificRealizationBlock *block,
    AtmScientificRenderLanguage language
)
{
    const char *prefix = NULL;

    switch (block->kind) {
        case ATM_SCIENTIFIC_REALIZATION_BLOCK_ANSWERABILITY:
            prefix = language ==
                ATM_SCIENTIFIC_RENDER_LANGUAGE_RO
                ? "Stare"
                : "Status";
            g_string_append_printf (
                text,
                "%s: %s\n",
                prefix,
                block->status
            );
            return;

        case ATM_SCIENTIFIC_REALIZATION_BLOCK_ESTABLISHED_FACT:
            prefix = language ==
                ATM_SCIENTIFIC_RENDER_LANGUAGE_RO
                ? "Fapt stabilit"
                : "Established fact";
            break;

        case ATM_SCIENTIFIC_REALIZATION_BLOCK_DERIVED_FACT:
            prefix = language ==
                ATM_SCIENTIFIC_RENDER_LANGUAGE_RO
                ? "Fapt derivat"
                : "Derived fact";
            break;

        case ATM_SCIENTIFIC_REALIZATION_BLOCK_CONSTRAINT:
            prefix = language ==
                ATM_SCIENTIFIC_RENDER_LANGUAGE_RO
                ? "Constrângere"
                : "Constraint";
            break;

        case ATM_SCIENTIFIC_REALIZATION_BLOCK_CONFLICT:
            prefix = language ==
                ATM_SCIENTIFIC_RENDER_LANGUAGE_RO
                ? "Conflict"
                : "Conflict";
            break;

        case ATM_SCIENTIFIC_REALIZATION_BLOCK_LIMITATION:
            prefix = language ==
                ATM_SCIENTIFIC_RENDER_LANGUAGE_RO
                ? "Limitare"
                : "Limitation";
            break;

        default:
            return;
    }

    g_string_append_printf (
        text,
        "%s: id=%s",
        prefix,
        block->source_id != NULL
            ? block->source_id
            : block->block_id
    );

    append_field (
        text,
        "type",
        block->fact_type
    );
    append_field (
        text,
        "subject",
        block->subject_id
    );
    append_field (
        text,
        "attribute",
        block->attribute
    );
    append_field (
        text,
        "value",
        block->value
    );
    append_field (
        text,
        "unit",
        block->unit
    );
    append_field (
        text,
        "dimension",
        block->dimension
    );
    append_field (
        text,
        "qualifiers",
        block->qualifiers_json
    );
    append_field (
        text,
        "status",
        block->status
    );
    append_field (
        text,
        "reason",
        block->reason_code
    );
    append_field (
        text,
        "operation",
        block->operation
    );
    append_field (
        text,
        "numeric_profile",
        block->operation_numeric_profile
    );
    g_string_append_c (text, '\n');
}

gboolean
atm_scientific_realization_render_direct (
    const AtmScientificRealizationView *view,
    AtmScientificRenderLanguage language,
    AtmScientificDirectRender **out_render,
    GError **error
)
{
    if (out_render == NULL ||
        *out_render != NULL ||
        (language !=
            ATM_SCIENTIFIC_RENDER_LANGUAGE_RO &&
         language !=
            ATM_SCIENTIFIC_RENDER_LANGUAGE_EN)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_REALIZATION_ERROR,
            ATM_SCIENTIFIC_REALIZATION_ERROR_ARGUMENT,
            "Direct scientific render received invalid arguments."
        );
        return FALSE;
    }

    if (!atm_scientific_realization_view_validate (
            view,
            error
        )) {
        return FALSE;
    }

    AtmScientificDirectRender *render = g_new0 (
        AtmScientificDirectRender,
        1
    );
    render->schema = g_strdup (
        ATM_SCIENTIFIC_DIRECT_RENDER_SCHEMA
    );
    render->language = language;
    render->scientific_content_id = g_strdup (
        view->scientific_content_id
    );
    render->qualified_artifact_id = g_strdup (
        view->qualified_artifact_id
    );
    render->rendered_block_ids = string_array_new ();
    render->support_ids = string_array_new ();

    GString *text = g_string_new (NULL);

    for (guint i = 0; i < view->blocks->len; i++) {
        AtmScientificRealizationBlock *block =
            g_ptr_array_index (
                view->blocks,
                i
            );

        render_block_line (
            text,
            block,
            language
        );
        g_ptr_array_add (
            render->rendered_block_ids,
            g_strdup (block->block_id)
        );
        copy_strings (
            render->support_ids,
            block->support
        );
    }

    sort_deduplicate_strings (
        render->support_ids
    );
    render->text = g_string_free (
        text,
        FALSE
    );

    *out_render = render;
    return TRUE;
}

void
atm_scientific_direct_render_free (
    AtmScientificDirectRender *render
)
{
    if (render == NULL) {
        return;
    }

    g_free (render->schema);
    g_free (render->text);
    g_free (render->scientific_content_id);
    g_free (render->qualified_artifact_id);
    g_clear_pointer (
        &render->rendered_block_ids,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &render->support_ids,
        g_ptr_array_unref
    );
    g_free (render);
}

const char *
atm_scientific_realization_view_schema (
    const AtmScientificRealizationView *view
)
{
    return view != NULL ? view->schema : NULL;
}

AtmSraAnswerability
atm_scientific_realization_view_answerability (
    const AtmScientificRealizationView *view
)
{
    g_return_val_if_fail (
        view != NULL,
        ATM_SRA_NOT_ANSWERABLE
    );
    return view->answerability;
}

const char *
atm_scientific_realization_view_content_id (
    const AtmScientificRealizationView *view
)
{
    return view != NULL
        ? view->scientific_content_id
        : NULL;
}

const char *
atm_scientific_realization_view_qualified_id (
    const AtmScientificRealizationView *view
)
{
    return view != NULL
        ? view->qualified_artifact_id
        : NULL;
}

gsize
atm_scientific_realization_view_block_count (
    const AtmScientificRealizationView *view
)
{
    return view != NULL && view->blocks != NULL
        ? view->blocks->len
        : 0;
}

const AtmScientificRealizationBlock *
atm_scientific_realization_view_block_at (
    const AtmScientificRealizationView *view,
    gsize index
)
{
    if (view == NULL ||
        view->blocks == NULL ||
        index >= view->blocks->len) {
        return NULL;
    }

    return g_ptr_array_index (
        view->blocks,
        index
    );
}

gsize
atm_scientific_realization_view_mandatory_count (
    const AtmScientificRealizationView *view
)
{
    return view != NULL &&
        view->mandatory_block_ids != NULL
        ? view->mandatory_block_ids->len
        : 0;
}

const char *
atm_scientific_realization_view_mandatory_at (
    const AtmScientificRealizationView *view,
    gsize index
)
{
    if (view == NULL ||
        view->mandatory_block_ids == NULL ||
        index >= view->mandatory_block_ids->len) {
        return NULL;
    }

    return g_ptr_array_index (
        view->mandatory_block_ids,
        index
    );
}

AtmScientificRealizationBlockKind
atm_scientific_realization_block_kind (
    const AtmScientificRealizationBlock *block
)
{
    g_return_val_if_fail (
        block != NULL,
        ATM_SCIENTIFIC_REALIZATION_BLOCK_ANSWERABILITY
    );
    return block->kind;
}

const char *
atm_scientific_realization_block_id (
    const AtmScientificRealizationBlock *block
)
{
    return block != NULL ? block->block_id : NULL;
}

const char *
atm_scientific_realization_block_source_id (
    const AtmScientificRealizationBlock *block
)
{
    return block != NULL ? block->source_id : NULL;
}

const char *
atm_scientific_realization_block_value (
    const AtmScientificRealizationBlock *block
)
{
    return block != NULL ? block->value : NULL;
}

const char *
atm_scientific_realization_block_reason (
    const AtmScientificRealizationBlock *block
)
{
    return block != NULL ? block->reason_code : NULL;
}

const char *
atm_scientific_realization_block_operation (
    const AtmScientificRealizationBlock *block
)
{
    return block != NULL ? block->operation : NULL;
}

gsize
atm_scientific_realization_block_support_count (
    const AtmScientificRealizationBlock *block
)
{
    return block != NULL && block->support != NULL
        ? block->support->len
        : 0;
}

const char *
atm_scientific_realization_block_support_at (
    const AtmScientificRealizationBlock *block,
    gsize index
)
{
    if (block == NULL ||
        block->support == NULL ||
        index >= block->support->len) {
        return NULL;
    }

    return g_ptr_array_index (
        block->support,
        index
    );
}

const char *
atm_scientific_direct_render_schema (
    const AtmScientificDirectRender *render
)
{
    return render != NULL ? render->schema : NULL;
}

const char *
atm_scientific_direct_render_text (
    const AtmScientificDirectRender *render
)
{
    return render != NULL ? render->text : NULL;
}

const char *
atm_scientific_direct_render_content_id (
    const AtmScientificDirectRender *render
)
{
    return render != NULL
        ? render->scientific_content_id
        : NULL;
}

const char *
atm_scientific_direct_render_qualified_id (
    const AtmScientificDirectRender *render
)
{
    return render != NULL
        ? render->qualified_artifact_id
        : NULL;
}

gsize
atm_scientific_direct_render_block_count (
    const AtmScientificDirectRender *render
)
{
    return render != NULL &&
        render->rendered_block_ids != NULL
        ? render->rendered_block_ids->len
        : 0;
}

const char *
atm_scientific_direct_render_block_at (
    const AtmScientificDirectRender *render,
    gsize index
)
{
    if (render == NULL ||
        render->rendered_block_ids == NULL ||
        index >= render->rendered_block_ids->len) {
        return NULL;
    }

    return g_ptr_array_index (
        render->rendered_block_ids,
        index
    );
}

gsize
atm_scientific_direct_render_support_count (
    const AtmScientificDirectRender *render
)
{
    return render != NULL &&
        render->support_ids != NULL
        ? render->support_ids->len
        : 0;
}

const char *
atm_scientific_direct_render_support_at (
    const AtmScientificDirectRender *render,
    gsize index
)
{
    if (render == NULL ||
        render->support_ids == NULL ||
        index >= render->support_ids->len) {
        return NULL;
    }

    return g_ptr_array_index (
        render->support_ids,
        index
    );
}
