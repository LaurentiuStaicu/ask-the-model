#include "scientific_result.h"

#include "scientific_canonical.h"

#include <errno.h>
#include <math.h>
#include <string.h>

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

static AtmSraEstablishedFact *
find_established_fact (
    const AtmSraResult *result,
    const char *fact_id
)
{
    if (result == NULL || !nonempty_utf8 (fact_id)) {
        return NULL;
    }

    for (guint i = 0;
         i < result->established_facts->len;
         i++) {
        AtmSraEstablishedFact *fact =
            g_ptr_array_index (
                result->established_facts,
                i
            );

        if (g_strcmp0 (
                fact->fact_id,
                fact_id
            ) == 0) {
            return fact;
        }
    }

    return NULL;
}

static gboolean
fact_id_exists (
    const AtmSraResult *result,
    const char *fact_id
)
{
    if (find_established_fact (
            result,
            fact_id
        ) != NULL) {
        return TRUE;
    }

    for (guint i = 0;
         i < result->derived_facts->len;
         i++) {
        AtmSraDerivedFact *fact =
            g_ptr_array_index (
                result->derived_facts,
                i
            );

        if (g_strcmp0 (
                fact->fact_id,
                fact_id
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
string_array_contains (
    GPtrArray *values,
    const char *value
)
{
    if (values == NULL || value == NULL) {
        return FALSE;
    }

    for (guint i = 0; i < values->len; i++) {
        if (g_strcmp0 (
                g_ptr_array_index (
                    values,
                    i
                ),
                value
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
qualification_has_operation_profile (
    const AtmSraResult *result,
    const AtmScientificOperationDescriptor *descriptor
)
{
    char *expected = g_strdup_printf (
        "%s@%s",
        descriptor->semantic_profile_id,
        descriptor->semantic_profile_version
    );

    gboolean found = string_array_contains (
        result->qualification->semantic_profiles,
        expected
    );

    g_free (expected);
    return found;
}

static gboolean
qualification_has_repository_snapshot (
    const AtmSraResult *result,
    const char *repository_id
)
{
    char *prefix = g_strdup_printf (
        "%s@",
        repository_id
    );
    gboolean found = FALSE;

    for (guint i = 0;
         i < result->qualification
             ->repository_snapshots->len;
         i++) {
        const char *snapshot = g_ptr_array_index (
            result->qualification
                ->repository_snapshots,
            i
        );

        if (g_str_has_prefix (
                snapshot,
                prefix
            ) &&
            strchr (snapshot, '#') != NULL) {
            found = TRUE;
            break;
        }
    }

    g_free (prefix);
    return found;
}

static gboolean
numeric_profile_allows (
    const char *sra_profile,
    const char *operation_profile
)
{
    if (g_strcmp0 (
            sra_profile,
            operation_profile
        ) == 0) {
        return TRUE;
    }

    return
        g_strcmp0 (
            sra_profile,
            "atm-numeric/exact-decimal+binary64-v1"
        ) == 0 &&
        g_strcmp0 (
            operation_profile,
            "atm-numeric/binary64-basic-v1"
        ) == 0;
}

static gboolean
parse_numeric_fact (
    const AtmSraEstablishedFact *fact,
    double *out_value,
    GError **error
)
{
    if (fact == NULL ||
        out_value == NULL ||
        !nonempty_utf8 (fact->value) ||
        !nonempty_utf8 (fact->unit)) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Scientific operation input facts require an explicit numeric value and unit."
        );
        return FALSE;
    }

    errno = 0;
    char *end = NULL;
    double value = g_ascii_strtod (
        fact->value,
        &end
    );

    if (end == fact->value ||
        end == NULL ||
        *end != '\0' ||
        errno == ERANGE ||
        !isfinite (value)) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Established fact '%s' is not a finite canonical numeric operation input.",
            fact->fact_id
        );
        return FALSE;
    }

    *out_value = value;
    return TRUE;
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
binding_free (AtmSraOperationBinding *binding)
{
    if (binding == NULL) {
        return;
    }

    g_free (binding->role);
    g_free (binding->fact_id);
    g_free (binding);
}

void
atm_sra_derived_fact_free (
    AtmSraDerivedFact *fact
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
    g_free (fact->reason_code);
    g_free (fact->operation);
    g_free (fact->operation_numeric_profile);
    g_clear_pointer (
        &fact->input_bindings,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &fact->support,
        g_ptr_array_unref
    );
    g_free (fact);
}

static gboolean
prepare_operation_inputs (
    const AtmSraResult *result,
    const AtmScientificOperationDescriptor *descriptor,
    const AtmSraOperationBinding *bindings,
    gsize binding_count,
    AtmScientificOperationInput **out_inputs,
    GPtrArray **out_support,
    GError **error
)
{
    if (binding_count != descriptor->input_count ||
        (binding_count > 0 && bindings == NULL)) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Operation %s requires exactly %" G_GSIZE_FORMAT " fact bindings.",
            descriptor->qualified_name,
            descriptor->input_count
        );
        return FALSE;
    }

    AtmScientificOperationInput *inputs = g_new0 (
        AtmScientificOperationInput,
        binding_count
    );
    GPtrArray *support =
        g_ptr_array_new_with_free_func (g_free);

    for (gsize i = 0; i < binding_count; i++) {
        if (g_strcmp0 (
                bindings[i].role,
                descriptor->input_roles[i]
            ) != 0 ||
            !nonempty_utf8 (bindings[i].fact_id)) {
            g_set_error (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_DERIVATION,
                "Operation %s binding %" G_GSIZE_FORMAT " must use role '%s'.",
                descriptor->qualified_name,
                i,
                descriptor->input_roles[i]
            );
            g_ptr_array_unref (support);
            g_free (inputs);
            return FALSE;
        }

        AtmSraEstablishedFact *fact =
            find_established_fact (
                result,
                bindings[i].fact_id
            );

        if (fact == NULL) {
            g_set_error (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_DERIVATION,
                "Operation %s input fact '%s' is not an established fact in this SRA.",
                descriptor->qualified_name,
                bindings[i].fact_id
            );
            g_ptr_array_unref (support);
            g_free (inputs);
            return FALSE;
        }

        double numeric_value = 0.0;

        if (!parse_numeric_fact (
                fact,
                &numeric_value,
                error
            )) {
            g_ptr_array_unref (support);
            g_free (inputs);
            return FALSE;
        }

        inputs[i].role = descriptor->input_roles[i];
        inputs[i].value = numeric_value;
        inputs[i].unit = fact->unit;

        for (guint j = 0;
             j < fact->support->len;
             j++) {
            g_ptr_array_add (
                support,
                g_strdup (
                    g_ptr_array_index (
                        fact->support,
                        j
                    )
                )
            );
        }
    }

    sort_deduplicate_strings (support);
    *out_inputs = inputs;
    *out_support = support;
    return TRUE;
}

static gboolean
operation_context_is_admissible (
    const AtmSraResult *result,
    const AtmScientificOperationDescriptor *descriptor,
    GError **error
)
{
    if (result->qualification == NULL ||
        !nonempty_utf8 (
            result->qualification->numeric_profile
        ) ||
        !numeric_profile_allows (
            result->qualification->numeric_profile,
            descriptor->numeric_profile
        )) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "SRA numeric profile does not authorize operation profile '%s'.",
            descriptor->numeric_profile
        );
        return FALSE;
    }

    if (!qualification_has_operation_profile (
            result,
            descriptor
        )) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "SRA qualification does not contain the semantic profile required by %s.",
            descriptor->qualified_name
        );
        return FALSE;
    }

    if (!qualification_has_repository_snapshot (
            result,
            descriptor->repository_id
        )) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "SRA qualification does not contain a pinned %s repository snapshot for %s.",
            descriptor->repository_id,
            descriptor->qualified_name
        );
        return FALSE;
    }

    return TRUE;
}

static char *
numeric_value_string (double value)
{
    char buffer[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_dtostr (
        buffer,
        sizeof buffer,
        value
    );
    return g_strdup (buffer);
}

gboolean
atm_sra_result_derive_fact (
    AtmSraResult *result,
    const char *qualified_operation,
    const AtmSraOperationBinding *bindings,
    gsize binding_count,
    const char *fact_id,
    const char *fact_type,
    const char *subject_id,
    const char *attribute,
    const char *dimension,
    GError **error
)
{
    if (result == NULL ||
        result->finalized ||
        !nonempty_utf8 (qualified_operation) ||
        !nonempty_utf8 (fact_id) ||
        !nonempty_utf8 (fact_type) ||
        !nonempty_utf8 (subject_id) ||
        !nonempty_utf8 (attribute) ||
        !optional_utf8 (dimension)) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_STATE,
            "Scientific derivation received invalid or finalized SRA state."
        );
        return FALSE;
    }

    if (fact_id_exists (result, fact_id)) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SHAPE,
            "Scientific fact ID '%s' already exists in this SRA.",
            fact_id
        );
        return FALSE;
    }

    const AtmScientificOperationDescriptor *descriptor =
        atm_scientific_operation_lookup (
            qualified_operation
        );

    if (descriptor == NULL ||
        descriptor->operation_class !=
            ATM_SCIENTIFIC_OPERATION_DERIVATION) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Operation '%s' is not an SRA-admissible registered derivation.",
            qualified_operation
        );
        return FALSE;
    }

    if (!operation_context_is_admissible (
            result,
            descriptor,
            error
        )) {
        return FALSE;
    }

    AtmScientificOperationInput *inputs = NULL;
    GPtrArray *support = NULL;

    if (!prepare_operation_inputs (
            result,
            descriptor,
            bindings,
            binding_count,
            &inputs,
            &support,
            error
        )) {
        return FALSE;
    }

    AtmScientificOperationResult *operation_result = NULL;
    GError *operation_error = NULL;

    if (!atm_scientific_operation_execute (
            descriptor->qualified_name,
            inputs,
            binding_count,
            &operation_result,
            &operation_error
        )) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Scientific operation %s rejected the bound SRA facts: %s.",
            descriptor->qualified_name,
            operation_error != NULL
                ? operation_error->message
                : "unknown operation error"
        );
        g_clear_error (&operation_error);
        g_ptr_array_unref (support);
        g_free (inputs);
        return FALSE;
    }

    if (operation_result->outcome !=
            ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC &&
        operation_result->outcome !=
            ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Operation %s produced an outcome not admitted as an SRA derived fact.",
            descriptor->qualified_name
        );
        atm_scientific_operation_result_free (
            operation_result
        );
        g_ptr_array_unref (support);
        g_free (inputs);
        return FALSE;
    }

    AtmSraDerivedFact *derived = g_new0 (
        AtmSraDerivedFact,
        1
    );

    derived->fact_id = g_strdup (fact_id);
    derived->fact_type = g_strdup (fact_type);
    derived->subject_id = g_strdup (subject_id);
    derived->attribute = g_strdup (attribute);
    derived->outcome = operation_result->outcome;
    derived->unit = g_strdup (operation_result->unit);
    derived->dimension = g_strdup (dimension);
    derived->reason_code = g_strdup (
        operation_result->reason_code
    );
    derived->operation = g_strdup (
        descriptor->qualified_name
    );
    derived->operation_numeric_profile = g_strdup (
        descriptor->numeric_profile
    );
    derived->input_bindings =
        g_ptr_array_new_with_free_func (
            (GDestroyNotify) binding_free
        );
    derived->support = support;

    for (gsize i = 0; i < binding_count; i++) {
        AtmSraOperationBinding *binding = g_new0 (
            AtmSraOperationBinding,
            1
        );
        binding->role = g_strdup (
            bindings[i].role
        );
        binding->fact_id = g_strdup (
            bindings[i].fact_id
        );
        g_ptr_array_add (
            derived->input_bindings,
            binding
        );
    }

    if (operation_result->outcome ==
        ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC) {
        GError *canonical_error = NULL;

        derived->value = numeric_value_string (
            operation_result->numeric_value
        );
        derived->has_binary64_bits = TRUE;

        if (!atm_scientific_binary64_bits (
                operation_result->numeric_value,
                &derived->binary64_bits,
                &canonical_error
            )) {
            g_set_error (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_DERIVATION,
                "Could not canonicalize %s output: %s.",
                descriptor->qualified_name,
                canonical_error != NULL
                    ? canonical_error->message
                    : "unknown binary64 error"
            );
            g_clear_error (&canonical_error);
            atm_sra_derived_fact_free (derived);
            atm_scientific_operation_result_free (
                operation_result
            );
            g_free (inputs);
            return FALSE;
        }
    } else {
        derived->value = g_strdup ("MISSING");
        derived->has_binary64_bits = FALSE;
    }

    if (!atm_sra_qualification_add_operation (
            result,
            descriptor->qualified_name,
            error
        )) {
        atm_sra_derived_fact_free (derived);
        atm_scientific_operation_result_free (
            operation_result
        );
        g_free (inputs);
        return FALSE;
    }

    g_ptr_array_add (
        result->derived_facts,
        derived
    );

    atm_scientific_operation_result_free (
        operation_result
    );
    g_free (inputs);
    return TRUE;
}

static gboolean
string_arrays_equal (
    GPtrArray *left,
    GPtrArray *right
)
{
    if (left->len != right->len) {
        return FALSE;
    }

    for (guint i = 0; i < left->len; i++) {
        if (g_strcmp0 (
                g_ptr_array_index (left, i),
                g_ptr_array_index (right, i)
            ) != 0) {
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
atm_sra_derived_fact_validate (
    const AtmSraResult *result,
    const AtmSraDerivedFact *derived,
    GError **error
)
{
    if (result == NULL ||
        derived == NULL ||
        !nonempty_utf8 (derived->fact_id) ||
        !nonempty_utf8 (derived->fact_type) ||
        !nonempty_utf8 (derived->subject_id) ||
        !nonempty_utf8 (derived->attribute) ||
        !nonempty_utf8 (derived->value) ||
        !nonempty_utf8 (derived->unit) ||
        !optional_utf8 (derived->dimension) ||
        !nonempty_utf8 (derived->reason_code) ||
        !nonempty_utf8 (derived->operation) ||
        !nonempty_utf8 (
            derived->operation_numeric_profile
        ) ||
        derived->input_bindings == NULL ||
        derived->support == NULL ||
        derived->support->len == 0) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Derived SRA fact has an incomplete derivation shape."
        );
        return FALSE;
    }

    const AtmScientificOperationDescriptor *descriptor =
        atm_scientific_operation_lookup (
            derived->operation
        );

    if (descriptor == NULL ||
        descriptor->operation_class !=
            ATM_SCIENTIFIC_OPERATION_DERIVATION ||
        g_strcmp0 (
            descriptor->numeric_profile,
            derived->operation_numeric_profile
        ) != 0 ||
        !operation_context_is_admissible (
            result,
            descriptor,
            error
        )) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                ATM_SRA_ERROR,
                ATM_SRA_ERROR_DERIVATION,
                "Derived SRA fact references an inadmissible operation binding."
            );
        }
        return FALSE;
    }

    gsize binding_count =
        derived->input_bindings->len;
    AtmSraOperationBinding *bindings = g_new0 (
        AtmSraOperationBinding,
        binding_count
    );

    for (gsize i = 0; i < binding_count; i++) {
        AtmSraOperationBinding *stored =
            g_ptr_array_index (
                derived->input_bindings,
                i
            );
        bindings[i].role = stored->role;
        bindings[i].fact_id = stored->fact_id;
    }

    AtmScientificOperationInput *inputs = NULL;
    GPtrArray *expected_support = NULL;

    if (!prepare_operation_inputs (
            result,
            descriptor,
            bindings,
            binding_count,
            &inputs,
            &expected_support,
            error
        )) {
        g_free (bindings);
        return FALSE;
    }

    g_free (bindings);

    if (!string_arrays_equal (
            expected_support,
            derived->support
        )) {
        g_ptr_array_unref (expected_support);
        g_free (inputs);
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_SUPPORT,
            "Derived SRA support is not exactly the union of its established input support."
        );
        return FALSE;
    }

    g_ptr_array_unref (expected_support);

    AtmScientificOperationResult *expected = NULL;
    GError *operation_error = NULL;

    if (!atm_scientific_operation_execute (
            descriptor->qualified_name,
            inputs,
            binding_count,
            &expected,
            &operation_error
        )) {
        g_set_error (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Derived SRA operation no longer validates against its bound input facts: %s.",
            operation_error != NULL
                ? operation_error->message
                : "unknown operation error"
        );
        g_clear_error (&operation_error);
        g_free (inputs);
        return FALSE;
    }

    g_free (inputs);

    gboolean valid =
        expected->outcome == derived->outcome &&
        g_strcmp0 (
            expected->unit,
            derived->unit
        ) == 0 &&
        g_strcmp0 (
            expected->reason_code,
            derived->reason_code
        ) == 0;

    if (valid &&
        expected->outcome ==
            ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC) {
        guint64 expected_bits = 0;
        GError *canonical_error = NULL;
        char *expected_value = numeric_value_string (
            expected->numeric_value
        );

        valid =
            derived->has_binary64_bits &&
            atm_scientific_binary64_bits (
                expected->numeric_value,
                &expected_bits,
                &canonical_error
            ) &&
            expected_bits == derived->binary64_bits &&
            g_strcmp0 (
                expected_value,
                derived->value
            ) == 0;

        g_free (expected_value);
        g_clear_error (&canonical_error);
    } else if (
        valid &&
        expected->outcome ==
            ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING
    ) {
        valid =
            !derived->has_binary64_bits &&
            g_strcmp0 (
                derived->value,
                "MISSING"
            ) == 0;
    } else {
        valid = FALSE;
    }

    atm_scientific_operation_result_free (expected);

    if (!valid) {
        g_set_error_literal (
            error,
            ATM_SRA_ERROR,
            ATM_SRA_ERROR_DERIVATION,
            "Derived SRA fact does not match deterministic re-execution of its operation binding."
        );
        return FALSE;
    }

    return TRUE;
}
