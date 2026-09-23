#include "scientific_operation.h"

#include <float.h>
#include <math.h>
#include <string.h>

static const char *CBD_BAYES_INPUTS[] = {
    "prior_probability",
    "likelihood_ratio"
};

static const char *RMD_STOCK_CLOSE_INPUTS[] = {
    "opening",
    "transactions",
    "revaluations",
    "other_changes"
};

static const char *RMD_STOCK_CHECK_INPUTS[] = {
    "opening",
    "transactions",
    "revaluations",
    "other_changes",
    "observed_closing",
    "absolute_tolerance",
    "relative_tolerance"
};

static const char *RMD_SAFE_RATIO_INPUTS[] = {
    "numerator",
    "denominator"
};

static const AtmScientificOperationDescriptor DESCRIPTORS[] = {
    {
        "CBD_BAYES_LR_UPDATE@1",
        "CBD_BAYES_LR_UPDATE",
        1,
        ATM_SCIENTIFIC_OPERATION_DERIVATION,
        "cbd",
        "atm-profile/cbd/v1",
        "1",
        "atm-numeric/binary64-basic-v1",
        CBD_BAYES_INPUTS,
        G_N_ELEMENTS (CBD_BAYES_INPUTS)
    },
    {
        "RMD_FINANCIAL_STOCK_CLOSE@1",
        "RMD_FINANCIAL_STOCK_CLOSE",
        1,
        ATM_SCIENTIFIC_OPERATION_DERIVATION,
        "rmd",
        "atm-profile/rmd/v1",
        "1",
        "atm-numeric/binary64-basic-v1",
        RMD_STOCK_CLOSE_INPUTS,
        G_N_ELEMENTS (RMD_STOCK_CLOSE_INPUTS)
    },
    {
        "RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1",
        "RMD_FINANCIAL_STOCK_IDENTITY_CHECK",
        1,
        ATM_SCIENTIFIC_OPERATION_CHECK,
        "rmd",
        "atm-profile/rmd/v1",
        "1",
        "atm-numeric/binary64-basic-v1",
        RMD_STOCK_CHECK_INPUTS,
        G_N_ELEMENTS (RMD_STOCK_CHECK_INPUTS)
    },
    {
        "RMD_SAFE_RATIO@1",
        "RMD_SAFE_RATIO",
        1,
        ATM_SCIENTIFIC_OPERATION_DERIVATION,
        "rmd",
        "atm-profile/rmd/v1",
        "1",
        "atm-numeric/binary64-basic-v1",
        RMD_SAFE_RATIO_INPUTS,
        G_N_ELEMENTS (RMD_SAFE_RATIO_INPUTS)
    }
};

GQuark
atm_scientific_operation_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-operation-error-quark"
    );
}

static gboolean
nonempty (const char *value)
{
    return value != NULL && value[0] != '\0';
}

const AtmScientificOperationDescriptor *
atm_scientific_operation_lookup (
    const char *qualified_name
)
{
    if (!nonempty (qualified_name)) {
        return NULL;
    }

    for (gsize i = 0;
         i < G_N_ELEMENTS (DESCRIPTORS);
         i++) {
        if (g_strcmp0 (
                DESCRIPTORS[i].qualified_name,
                qualified_name
            ) == 0) {
            return &DESCRIPTORS[i];
        }
    }

    return NULL;
}

gsize
atm_scientific_operation_count (void)
{
    return G_N_ELEMENTS (DESCRIPTORS);
}

const AtmScientificOperationDescriptor *
atm_scientific_operation_at (gsize index)
{
    if (index >= G_N_ELEMENTS (DESCRIPTORS)) {
        return NULL;
    }

    return &DESCRIPTORS[index];
}

void
atm_scientific_operation_result_free (
    AtmScientificOperationResult *result
)
{
    if (result == NULL) {
        return;
    }

    g_free (result->unit);
    g_free (result->reason_code);
    g_free (result);
}

static gboolean
input_shape_valid (
    const AtmScientificOperationDescriptor *descriptor,
    const AtmScientificOperationInput *inputs,
    gsize input_count,
    GError **error
)
{
    if (inputs == NULL ||
        input_count != descriptor->input_count) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_INPUT_SHAPE,
            "Operation %s requires exactly %" G_GSIZE_FORMAT " inputs.",
            descriptor->qualified_name,
            descriptor->input_count
        );
        return FALSE;
    }

    for (gsize i = 0; i < input_count; i++) {
        if (g_strcmp0 (
                inputs[i].role,
                descriptor->input_roles[i]
            ) != 0) {
            g_set_error (
                error,
                ATM_SCIENTIFIC_OPERATION_ERROR,
                ATM_SCIENTIFIC_OPERATION_ERROR_INPUT_SHAPE,
                "Operation %s input %" G_GSIZE_FORMAT " must have role '%s'.",
                descriptor->qualified_name,
                i,
                descriptor->input_roles[i]
            );
            return FALSE;
        }

        if (!isfinite (inputs[i].value)) {
            g_set_error (
                error,
                ATM_SCIENTIFIC_OPERATION_ERROR,
                ATM_SCIENTIFIC_OPERATION_ERROR_PRECONDITION,
                "Operation %s received a non-finite input.",
                descriptor->qualified_name
            );
            return FALSE;
        }

        if (!nonempty (inputs[i].unit)) {
            g_set_error (
                error,
                ATM_SCIENTIFIC_OPERATION_ERROR,
                ATM_SCIENTIFIC_OPERATION_ERROR_UNIT,
                "Operation %s requires an explicit unit for every input.",
                descriptor->qualified_name
            );
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
all_units_equal (
    const AtmScientificOperationInput *inputs,
    gsize first,
    gsize count,
    GError **error
)
{
    const char *unit = inputs[first].unit;

    for (gsize i = first + 1;
         i < first + count;
         i++) {
        if (g_strcmp0 (unit, inputs[i].unit) != 0) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_OPERATION_ERROR,
                ATM_SCIENTIFIC_OPERATION_ERROR_UNIT,
                "Operation inputs that represent the same accounting quantity must use the same unit."
            );
            return FALSE;
        }
    }

    return TRUE;
}

static AtmScientificOperationResult *
new_result (
    const AtmScientificOperationDescriptor *descriptor,
    AtmScientificOperationOutcome outcome,
    const char *unit,
    const char *reason_code
)
{
    AtmScientificOperationResult *result = g_new0 (
        AtmScientificOperationResult,
        1
    );

    result->descriptor = descriptor;
    result->outcome = outcome;
    result->unit = g_strdup (unit);
    result->reason_code = g_strdup (reason_code);
    return result;
}

static gboolean
execute_bayes (
    const AtmScientificOperationDescriptor *descriptor,
    const AtmScientificOperationInput *inputs,
    AtmScientificOperationResult **out_result,
    GError **error
)
{
    if (g_strcmp0 (inputs[0].unit, "1") != 0 ||
        g_strcmp0 (inputs[1].unit, "1") != 0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_UNIT,
            "Bayes prior probability and likelihood ratio must be dimensionless (unit '1')."
        );
        return FALSE;
    }

    const double p0 = inputs[0].value;
    const double lr = inputs[1].value;

    if (p0 < 0.0 || p0 > 1.0 ||
        lr <= 0.0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_PRECONDITION,
            "Bayes update requires 0 <= prior_probability <= 1 and likelihood_ratio > 0."
        );
        return FALSE;
    }

    double posterior;

    if (p0 == 0.0) {
        posterior = 0.0;
    } else if (p0 == 1.0) {
        posterior = 1.0;
    } else if (lr >= 1.0) {
        posterior = p0 /
            (p0 + (1.0 - p0) / lr);
    } else {
        const double scaled = p0 * lr;
        posterior = scaled /
            (scaled + (1.0 - p0));
    }

    if (!isfinite (posterior) ||
        posterior < 0.0 ||
        posterior > 1.0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_NUMERIC,
            "Bayes update produced an invalid binary64 result."
        );
        return FALSE;
    }

    AtmScientificOperationResult *result = new_result (
        descriptor,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC,
        "1",
        "OK"
    );
    result->numeric_value = posterior;
    *out_result = result;
    return TRUE;
}

static gboolean
stock_close_value (
    const AtmScientificOperationInput *inputs,
    double *out_value,
    GError **error
)
{
    if (!all_units_equal (
            inputs,
            0,
            4,
            error
        )) {
        return FALSE;
    }

    double value =
        ((inputs[0].value + inputs[1].value) +
         inputs[2].value) +
        inputs[3].value;

    if (!isfinite (value)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_NUMERIC,
            "RMD stock identity overflowed or produced a non-finite result."
        );
        return FALSE;
    }

    *out_value = value;
    return TRUE;
}

static gboolean
execute_stock_close (
    const AtmScientificOperationDescriptor *descriptor,
    const AtmScientificOperationInput *inputs,
    AtmScientificOperationResult **out_result,
    GError **error
)
{
    double closing = 0.0;

    if (!stock_close_value (
            inputs,
            &closing,
            error
        )) {
        return FALSE;
    }

    AtmScientificOperationResult *result = new_result (
        descriptor,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC,
        inputs[0].unit,
        "OK"
    );
    result->numeric_value = closing;
    *out_result = result;
    return TRUE;
}

static gboolean
execute_stock_check (
    const AtmScientificOperationDescriptor *descriptor,
    const AtmScientificOperationInput *inputs,
    AtmScientificOperationResult **out_result,
    GError **error
)
{
    if (!all_units_equal (
            inputs,
            0,
            6,
            error
        )) {
        return FALSE;
    }

    if (g_strcmp0 (inputs[6].unit, "1") != 0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_UNIT,
            "RMD relative tolerance must be dimensionless (unit '1')."
        );
        return FALSE;
    }

    if (inputs[5].value < 0.0 ||
        inputs[6].value < 0.0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_PRECONDITION,
            "RMD accounting tolerances must be non-negative."
        );
        return FALSE;
    }

    double expected = 0.0;

    if (!stock_close_value (
            inputs,
            &expected,
            error
        )) {
        return FALSE;
    }

    const double observed = inputs[4].value;
    const double difference =
        observed - expected;
    const double absolute_error =
        difference < 0.0 ? -difference : difference;
    const double observed_abs =
        observed < 0.0 ? -observed : observed;
    const double expected_abs =
        expected < 0.0 ? -expected : expected;
    const double relative_scale =
        observed_abs > expected_abs
            ? observed_abs
            : expected_abs;
    const double relative_tolerance =
        inputs[6].value * relative_scale;
    const double tolerance =
        inputs[5].value > relative_tolerance
            ? inputs[5].value
            : relative_tolerance;

    if (!isfinite (absolute_error) ||
        !isfinite (tolerance)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_NUMERIC,
            "RMD identity check produced a non-finite comparison."
        );
        return FALSE;
    }

    AtmScientificOperationResult *result = new_result (
        descriptor,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_BOOLEAN,
        inputs[0].unit,
        absolute_error <= tolerance
            ? "ACCOUNTING_IDENTITY_PASS"
            : "ACCOUNTING_IDENTITY_FAIL"
    );

    result->boolean_value =
        absolute_error <= tolerance;
    result->numeric_value = expected;
    result->secondary_numeric_value =
        absolute_error;
    *out_result = result;
    return TRUE;
}

static gboolean
execute_safe_ratio (
    const AtmScientificOperationDescriptor *descriptor,
    const AtmScientificOperationInput *inputs,
    AtmScientificOperationResult **out_result,
    GError **error
)
{
    if (g_strcmp0 (
            inputs[0].unit,
            inputs[1].unit
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_UNIT,
            "RMD safe ratio v1 requires numerator and denominator to use the same unit."
        );
        return FALSE;
    }

    if (inputs[1].value == 0.0) {
        *out_result = new_result (
            descriptor,
            ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING,
            "1",
            "ZERO_DENOMINATOR"
        );
        return TRUE;
    }

    const double ratio =
        inputs[0].value / inputs[1].value;

    if (!isfinite (ratio)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_NUMERIC,
            "RMD safe ratio produced a non-finite result."
        );
        return FALSE;
    }

    AtmScientificOperationResult *result = new_result (
        descriptor,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC,
        "1",
        "OK"
    );
    result->numeric_value = ratio;
    *out_result = result;
    return TRUE;
}

gboolean
atm_scientific_operation_execute (
    const char *qualified_name,
    const AtmScientificOperationInput *inputs,
    gsize input_count,
    AtmScientificOperationResult **out_result,
    GError **error
)
{
    if (!nonempty (qualified_name) ||
        out_result == NULL ||
        *out_result != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_ARGUMENT,
            "Scientific operation execution received invalid arguments."
        );
        return FALSE;
    }

    const AtmScientificOperationDescriptor *descriptor =
        atm_scientific_operation_lookup (
            qualified_name
        );

    if (descriptor == NULL) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_OPERATION_ERROR,
            ATM_SCIENTIFIC_OPERATION_ERROR_UNKNOWN,
            "Scientific operation '%s' is not registered.",
            qualified_name
        );
        return FALSE;
    }

    if (!input_shape_valid (
            descriptor,
            inputs,
            input_count,
            error
        )) {
        return FALSE;
    }

    if (g_strcmp0 (
            qualified_name,
            "CBD_BAYES_LR_UPDATE@1"
        ) == 0) {
        return execute_bayes (
            descriptor,
            inputs,
            out_result,
            error
        );
    }

    if (g_strcmp0 (
            qualified_name,
            "RMD_FINANCIAL_STOCK_CLOSE@1"
        ) == 0) {
        return execute_stock_close (
            descriptor,
            inputs,
            out_result,
            error
        );
    }

    if (g_strcmp0 (
            qualified_name,
            "RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1"
        ) == 0) {
        return execute_stock_check (
            descriptor,
            inputs,
            out_result,
            error
        );
    }

    if (g_strcmp0 (
            qualified_name,
            "RMD_SAFE_RATIO@1"
        ) == 0) {
        return execute_safe_ratio (
            descriptor,
            inputs,
            out_result,
            error
        );
    }

    g_set_error_literal (
        error,
        ATM_SCIENTIFIC_OPERATION_ERROR,
        ATM_SCIENTIFIC_OPERATION_ERROR_UNKNOWN,
        "Registered scientific operation has no implementation."
    );
    return FALSE;
}
