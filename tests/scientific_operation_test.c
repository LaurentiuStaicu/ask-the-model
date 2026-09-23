#include <glib.h>

#include <float.h>
#include <math.h>

#include "scientific_operation.h"

static void
test_registry (void)
{
    g_assert_cmpuint (
        atm_scientific_operation_count (),
        ==,
        4
    );

    const AtmScientificOperationDescriptor *bayes =
        atm_scientific_operation_lookup (
            "CBD_BAYES_LR_UPDATE@1"
        );

    g_assert_nonnull (bayes);
    g_assert_cmpstr (
        bayes->operation_id,
        ==,
        "CBD_BAYES_LR_UPDATE"
    );
    g_assert_cmpuint (bayes->version, ==, 1);
    g_assert_cmpint (
        bayes->operation_class,
        ==,
        ATM_SCIENTIFIC_OPERATION_DERIVATION
    );
    g_assert_cmpstr (bayes->repository_id, ==, "cbd");
    g_assert_cmpstr (
        bayes->semantic_profile_id,
        ==,
        "atm-profile/cbd/v1"
    );
    g_assert_cmpstr (
        bayes->semantic_profile_version,
        ==,
        "1"
    );
    g_assert_cmpstr (
        bayes->numeric_profile,
        ==,
        "atm-numeric/binary64-basic-v1"
    );

    g_assert_null (
        atm_scientific_operation_lookup (
            "NOT_REGISTERED@1"
        )
    );
}

static AtmScientificOperationResult *
execute (
    const char *name,
    AtmScientificOperationInput *inputs,
    gsize input_count
)
{
    AtmScientificOperationResult *result = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_operation_execute (
            name,
            inputs,
            input_count,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (result);
    return result;
}

static void
test_bayes_lr (void)
{
    AtmScientificOperationInput inputs[] = {
        { "prior_probability", 0.01, "1" },
        { "likelihood_ratio", 8.0, "1" }
    };

    AtmScientificOperationResult *result = execute (
        "CBD_BAYES_LR_UPDATE@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );

    g_assert_cmpint (
        result->outcome,
        ==,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC
    );
    g_assert_cmpfloat_with_epsilon (
        result->numeric_value,
        0.07476635514018691,
        1e-15
    );
    g_assert_cmpstr (result->unit, ==, "1");
    atm_scientific_operation_result_free (result);

    inputs[0].value = 0.0;
    result = execute (
        "CBD_BAYES_LR_UPDATE@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );
    g_assert_cmpfloat (result->numeric_value, ==, 0.0);
    atm_scientific_operation_result_free (result);

    inputs[0].value = 1.0;
    result = execute (
        "CBD_BAYES_LR_UPDATE@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );
    g_assert_cmpfloat (result->numeric_value, ==, 1.0);
    atm_scientific_operation_result_free (result);

    inputs[0].value = 0.5;
    inputs[1].value = DBL_MAX;
    result = execute (
        "CBD_BAYES_LR_UPDATE@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );
    g_assert_true (result->numeric_value > 0.999999999999);
    atm_scientific_operation_result_free (result);
}

static void
test_bayes_rejects_bad_lr (void)
{
    AtmScientificOperationInput inputs[] = {
        { "prior_probability", 0.25, "1" },
        { "likelihood_ratio", 0.0, "1" }
    };
    AtmScientificOperationResult *result = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_operation_execute (
            "CBD_BAYES_LR_UPDATE@1",
            inputs,
            G_N_ELEMENTS (inputs),
            &result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_OPERATION_ERROR,
        ATM_SCIENTIFIC_OPERATION_ERROR_PRECONDITION
    );
    g_assert_null (result);
    g_clear_error (&error);
}

static void
test_rmd_stock_close (void)
{
    AtmScientificOperationInput inputs[] = {
        { "opening", 100.0, "million_RON" },
        { "transactions", 12.0, "million_RON" },
        { "revaluations", -3.0, "million_RON" },
        { "other_changes", 1.0, "million_RON" }
    };

    AtmScientificOperationResult *result = execute (
        "RMD_FINANCIAL_STOCK_CLOSE@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );

    g_assert_cmpfloat (
        result->numeric_value,
        ==,
        110.0
    );
    g_assert_cmpstr (
        result->unit,
        ==,
        "million_RON"
    );
    atm_scientific_operation_result_free (result);
}

static void
test_rmd_identity_check (void)
{
    AtmScientificOperationInput inputs[] = {
        { "opening", 100.0, "million_RON" },
        { "transactions", 12.0, "million_RON" },
        { "revaluations", -3.0, "million_RON" },
        { "other_changes", 1.0, "million_RON" },
        { "observed_closing", 110.0004, "million_RON" },
        { "absolute_tolerance", 0.001, "million_RON" },
        { "relative_tolerance", 0.0, "1" }
    };

    AtmScientificOperationResult *result = execute (
        "RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );

    g_assert_cmpint (
        result->outcome,
        ==,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_BOOLEAN
    );
    g_assert_true (result->boolean_value);
    g_assert_cmpstr (
        result->reason_code,
        ==,
        "ACCOUNTING_IDENTITY_PASS"
    );
    g_assert_cmpfloat (
        result->numeric_value,
        ==,
        110.0
    );
    atm_scientific_operation_result_free (result);

    inputs[4].value = 110.01;
    result = execute (
        "RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );
    g_assert_false (result->boolean_value);
    g_assert_cmpstr (
        result->reason_code,
        ==,
        "ACCOUNTING_IDENTITY_FAIL"
    );
    atm_scientific_operation_result_free (result);
}

static void
test_safe_ratio_zero_is_missing (void)
{
    AtmScientificOperationInput inputs[] = {
        { "numerator", 10.0, "million_RON" },
        { "denominator", 0.0, "million_RON" }
    };

    AtmScientificOperationResult *result = execute (
        "RMD_SAFE_RATIO@1",
        inputs,
        G_N_ELEMENTS (inputs)
    );

    g_assert_cmpint (
        result->outcome,
        ==,
        ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING
    );
    g_assert_cmpstr (
        result->reason_code,
        ==,
        "ZERO_DENOMINATOR"
    );
    atm_scientific_operation_result_free (result);
}

static void
test_units_fail_closed (void)
{
    AtmScientificOperationInput inputs[] = {
        { "opening", 100.0, "million_RON" },
        { "transactions", 12.0, "RON" },
        { "revaluations", -3.0, "million_RON" },
        { "other_changes", 1.0, "million_RON" }
    };
    AtmScientificOperationResult *result = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_operation_execute (
            "RMD_FINANCIAL_STOCK_CLOSE@1",
            inputs,
            G_N_ELEMENTS (inputs),
            &result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_OPERATION_ERROR,
        ATM_SCIENTIFIC_OPERATION_ERROR_UNIT
    );
    g_assert_null (result);
    g_clear_error (&error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-operation/registry",
        test_registry
    );
    g_test_add_func (
        "/scientific-operation/bayes-lr",
        test_bayes_lr
    );
    g_test_add_func (
        "/scientific-operation/bayes-bad-lr",
        test_bayes_rejects_bad_lr
    );
    g_test_add_func (
        "/scientific-operation/rmd-stock-close",
        test_rmd_stock_close
    );
    g_test_add_func (
        "/scientific-operation/rmd-identity-check",
        test_rmd_identity_check
    );
    g_test_add_func (
        "/scientific-operation/safe-ratio-zero",
        test_safe_ratio_zero_is_missing
    );
    g_test_add_func (
        "/scientific-operation/unit-gate",
        test_units_fail_closed
    );

    return g_test_run ();
}
