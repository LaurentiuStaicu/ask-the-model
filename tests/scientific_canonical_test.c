#include <glib.h>

#include <math.h>
#include <string.h>

#include "scientific_canonical.h"

static void
test_decimal_normalization (void)
{
    AtmScientificDecimal *decimal = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_decimal_parse (
            "7.005760432822278",
            &decimal,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        decimal->coefficient,
        ==,
        "7005760432822278"
    );
    g_assert_cmpint (
        decimal->exponent,
        ==,
        -15
    );
    atm_scientific_decimal_free (decimal);
    decimal = NULL;

    g_assert_true (
        atm_scientific_decimal_parse (
            "0.8300",
            &decimal,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        decimal->coefficient,
        ==,
        "83"
    );
    g_assert_cmpint (
        decimal->exponent,
        ==,
        -2
    );
    atm_scientific_decimal_free (decimal);
    decimal = NULL;

    g_assert_true (
        atm_scientific_decimal_parse (
            "-0.000",
            &decimal,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        decimal->coefficient,
        ==,
        "0"
    );
    g_assert_cmpint (
        decimal->exponent,
        ==,
        0
    );
    atm_scientific_decimal_free (decimal);
}

static void
test_decimal_rejects_invalid (void)
{
    AtmScientificDecimal *decimal = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_decimal_parse (
            "1.",
            &decimal,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CANONICAL_ERROR,
        ATM_SCIENTIFIC_CANONICAL_ERROR_DECIMAL
    );
    g_assert_null (decimal);
    g_clear_error (&error);
}

static void
test_binary64_uses_exact_bits (void)
{
    guint64 bits = 0;
    guint64 expected = 0;
    double value = -0.0;
    GError *error = NULL;

    memcpy (
        &expected,
        &value,
        sizeof value
    );

    g_assert_true (
        atm_scientific_binary64_bits (
            value,
            &bits,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (bits, ==, expected);
}

static void
test_binary64_rejects_nonfinite (void)
{
    guint64 bits = 0;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_binary64_bits (
            INFINITY,
            &bits,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CANONICAL_ERROR,
        ATM_SCIENTIFIC_CANONICAL_ERROR_NUMBER
    );
    g_clear_error (&error);
}

static void
test_json_content_id_ignores_object_order_and_whitespace (void)
{
    char *left = NULL;
    char *right = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_content_id_json (
            "fixture.record",
            "{\"b\":2,\"a\":1}",
            &left,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_content_id_json (
            "fixture.record",
            "{ \n  \"a\" : 1, \"b\" : 2 \n}",
            &right,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (left, ==, right);

    g_free (right);
    g_free (left);
}

static void
test_json_array_order_is_semantic (void)
{
    char *left = NULL;
    char *right = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_content_id_json (
            "fixture.record",
            "{\"x\":[1,2]}",
            &left,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_content_id_json (
            "fixture.record",
            "{\"x\":[2,1]}",
            &right,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (left, !=, right);

    g_free (right);
    g_free (left);
}

static void
test_qualification_changes_with_snapshot (void)
{
    char *first = NULL;
    char *second = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_qualified_artifact_id (
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "ewd",
            "0.1.0",
            "0123456789abcdef0123456789abcdef01234567",
            "model.json",
            "json:/x",
            "ewd:entity:variable:x",
            "atm-profile/ewd/v1",
            "1",
            &first,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_scientific_qualified_artifact_id (
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "ewd",
            "0.1.0",
            "1123456789abcdef0123456789abcdef01234567",
            "model.json",
            "json:/x",
            "ewd:entity:variable:x",
            "atm-profile/ewd/v1",
            "1",
            &second,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (first, !=, second);

    g_free (second);
    g_free (first);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-canonical/decimal-normalization",
        test_decimal_normalization
    );
    g_test_add_func (
        "/scientific-canonical/decimal-invalid",
        test_decimal_rejects_invalid
    );
    g_test_add_func (
        "/scientific-canonical/binary64-bits",
        test_binary64_uses_exact_bits
    );
    g_test_add_func (
        "/scientific-canonical/binary64-nonfinite",
        test_binary64_rejects_nonfinite
    );
    g_test_add_func (
        "/scientific-canonical/json-order-whitespace",
        test_json_content_id_ignores_object_order_and_whitespace
    );
    g_test_add_func (
        "/scientific-canonical/json-array-order",
        test_json_array_order_is_semantic
    );
    g_test_add_func (
        "/scientific-canonical/qualification-snapshot",
        test_qualification_changes_with_snapshot
    );

    return g_test_run ();
}
