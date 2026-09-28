#include "annual_series_candidate.h"
#include <string.h>

#define HEADER "year,temperature_anomaly_c_1951_1980\n"

static AtmAnnualSeriesCandidate *
parse (const char *text)
{
    GBytes *bytes = g_bytes_new (text, strlen (text));
    AtmAnnualSeriesCandidate *candidate = NULL;
    GError *error = NULL;
    g_assert_true (atm_annual_series_candidate_parse (bytes, &candidate, &error));
    g_assert_no_error (error);
    g_bytes_unref (bytes);
    return candidate;
}

static void
reject_bytes (const void *data, gsize size, gint code)
{
    GBytes *bytes = g_bytes_new (data, size);
    AtmAnnualSeriesCandidate *candidate = NULL;
    GError *error = NULL;
    g_assert_false (atm_annual_series_candidate_parse (bytes, &candidate, &error));
    g_assert_null (candidate);
    g_assert_error (error, ATM_ANNUAL_SERIES_ERROR, code);
    g_clear_error (&error);
    g_bytes_unref (bytes);
}

static void
test_source (void)
{
    gchar *text = NULL;
    gsize size = 0;
    GError *error = NULL;
    g_assert_true (g_file_get_contents (g_getenv ("ATM_CHART01_CSV"), &text, &size, &error));
    g_assert_no_error (error);
    AtmAnnualSeriesCandidate *c = parse (text);
    g_assert_cmpuint (atm_annual_series_candidate_count (c), ==, 146);
    g_assert_cmpuint (atm_annual_series_candidate_year (c, 0), ==, 1880);
    g_assert_cmpuint (atm_annual_series_candidate_year (c, 145), ==, 2025);
    g_assert_cmpuint (atm_annual_series_candidate_source_row (c, 145), ==, 147);
    g_assert_cmpstr (atm_annual_series_candidate_decimal (c, 0), ==, "-0.1700");
    g_assert_cmpstr (atm_annual_series_candidate_coefficient (c, 0), ==, "-17");
    g_assert_cmpint (atm_annual_series_candidate_exponent (c, 0), ==, -2);
    g_assert_cmpstr (atm_annual_series_candidate_source_sha256 (c), ==,
                    "c03e15198201c491cfbd665ad655f72c54f2df19db9c93614b5a2fb4ee5590fb");
    g_assert_null (atm_annual_series_candidate_decimal (c, 146));
    g_assert_cmpuint (atm_annual_series_candidate_year (NULL, 0), ==, 0);
    atm_annual_series_candidate_free (c);
    g_free (text);
}

static void
test_ownership_and_zero (void)
{
    char *source = g_strdup (HEADER "2000,-0.0000\n2001,1.2300");
    GBytes *bytes = g_bytes_new_static (source, strlen (source));
    AtmAnnualSeriesCandidate *c = NULL;
    g_assert_true (atm_annual_series_candidate_parse (bytes, &c, NULL));
    g_bytes_unref (bytes);
    memset (source, 'x', strlen (source));
    g_free (source);
    g_assert_cmpstr (atm_annual_series_candidate_decimal (c, 0), ==, "-0.0000");
    g_assert_cmpstr (atm_annual_series_candidate_coefficient (c, 0), ==, "0");
    g_assert_cmpint (atm_annual_series_candidate_exponent (c, 0), ==, 0);
    g_assert_cmpstr (atm_annual_series_candidate_coefficient (c, 1), ==, "123");
    g_assert_cmpint (atm_annual_series_candidate_exponent (c, 1), ==, -2);
    atm_annual_series_candidate_free (c);
}

static void
test_malformed (void)
{
    const char *shape[] = {HEADER, HEADER "2000,\n", HEADER "2000,1.0000\n\n",
                          "year,ppol\n2000,1.0000\n", HEADER "abcd,1.0000\n"};
    for (guint i = 0; i < G_N_ELEMENTS (shape); i++)
        reject_bytes (shape[i], strlen (shape[i]), ATM_ANNUAL_SERIES_ERROR_SHAPE);
    const char *values[] = {"NaN000", "Inf000", "1.000e0", "01.0000", "+1.0000",
                           "1.00000", "1.0000,extra", "1.0000\r", "-0.00xx"};
    for (guint i = 0; i < G_N_ELEMENTS (values); i++) {
        char *text = g_strdup_printf (HEADER "2000,%s\n", values[i]);
        reject_bytes (text, strlen (text), ATM_ANNUAL_SERIES_ERROR_NUMBER);
        g_free (text);
    }
    const char embedded[] = HEADER "2000,1.0000\0ignored";
    reject_bytes (embedded, sizeof embedded - 1, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    const char *order[] = {HEADER "0000,1.0000", HEADER "2000,1.0000\n2000,2.0000",
                          HEADER "2000,1.0000\n1999,2.0000", HEADER "2000,1.0000\n2002,2.0000"};
    for (guint i = 0; i < G_N_ELEMENTS (order); i++)
        reject_bytes (order[i], strlen (order[i]), ATM_ANNUAL_SERIES_ERROR_ORDER);
}

static void
test_limits (void)
{
    GString *text = g_string_new (HEADER);
    for (guint i = 0; i < ATM_ANNUAL_SERIES_MAX_POINTS; i++)
        g_string_append_printf (text, "%u,1.0000\n", 1800 + i);
    AtmAnnualSeriesCandidate *c = parse (text->str);
    g_assert_cmpuint (atm_annual_series_candidate_count (c), ==, ATM_ANNUAL_SERIES_MAX_POINTS);
    atm_annual_series_candidate_free (c);
    g_string_append_printf (text, "%u,1.0000\n", 1800 + ATM_ANNUAL_SERIES_MAX_POINTS);
    reject_bytes (text->str, text->len, ATM_ANNUAL_SERIES_ERROR_LIMIT);
    g_string_free (text, TRUE);
    char *large = g_malloc0 (ATM_ANNUAL_SERIES_MAX_BYTES + 1);
    reject_bytes (large, ATM_ANNUAL_SERIES_MAX_BYTES + 1, ATM_ANNUAL_SERIES_ERROR_LIMIT);
    g_free (large);
    text = g_string_new (HEADER "2000,");
    for (guint i = 0; i < ATM_ANNUAL_SERIES_MAX_DECIMAL_BYTES - 5; i++)
        g_string_append_c (text, '1');
    g_string_append (text, ".0000");
    c = parse (text->str);
    atm_annual_series_candidate_free (c);
    g_string_append_c (text, '0');
    reject_bytes (text->str, text->len, ATM_ANNUAL_SERIES_ERROR_LIMIT);
    g_string_free (text, TRUE);
}

static void
test_arguments (void)
{
    AtmAnnualSeriesCandidate *c = NULL;
    GError *error = NULL;
    g_assert_false (atm_annual_series_candidate_parse (NULL, &c, &error));
    g_assert_error (error, ATM_ANNUAL_SERIES_ERROR, ATM_ANNUAL_SERIES_ERROR_ARGUMENT);
    g_clear_error (&error);
    GBytes *bytes = g_bytes_new_static (HEADER "2000,1.0000", strlen (HEADER "2000,1.0000"));
    g_assert_false (atm_annual_series_candidate_parse (bytes, NULL, &error));
    g_clear_error (&error);
    c = parse (HEADER "2000,1.0000");
    AtmAnnualSeriesCandidate *original = c;
    g_assert_false (atm_annual_series_candidate_parse (bytes, &c, &error));
    g_assert_true (original == c);
    g_clear_error (&error);
    atm_annual_series_candidate_free (c);
    g_bytes_unref (bytes);
    atm_annual_series_candidate_free (NULL);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/annual-series/source", test_source);
    g_test_add_func ("/annual-series/ownership-zero", test_ownership_and_zero);
    g_test_add_func ("/annual-series/malformed", test_malformed);
    g_test_add_func ("/annual-series/limits", test_limits);
    g_test_add_func ("/annual-series/arguments", test_arguments);
    return g_test_run ();
}
