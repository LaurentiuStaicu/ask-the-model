#include "verified_series.h"
#include <json-glib/json-glib.h>
#ifndef ATM_VERIFIED_SERIES_TESTING
#error VerifiedSeries qualification tests require the test-only fault injection seam.
#endif
gboolean atm_verified_series_test_rehash (AtmVerifiedSeries *s, GError **error);

static AtmGistempAdmission *admit (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json", "science/data/registry.csv"
    };
    GBytes *sources[4];
    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (root, "ewd", paths[i], NULL);
        char *bytes; gsize length;
        g_assert_true (g_file_get_contents (path, &bytes, &length, NULL));
        sources[i] = g_bytes_new_take (bytes, length);
        g_free (path);
    }
    AtmGistempAdmission *a = NULL;
    g_assert_true (atm_gistemp_admission_new ("LaurentiuStaicu/empirical-world3-dynamics",
        "d9e249339663015f6d1c05752338a955bf64ad0b", sources, &a, NULL));
    for (guint i = 0; i < 4; i++) g_bytes_unref (sources[i]);
    return a;
}
static AtmVerifiedSeries *build (void)
{
    AtmGistempAdmission *a = admit ();
    AtmVerifiedSeries *s = NULL;
    GError *error = NULL;
    g_assert_true (atm_verified_series_from_gistemp (a, &s, &error));
    g_assert_no_error (error);
    atm_gistemp_admission_free (a);
    return s;
}
static void test_complete_series (void)
{
    AtmVerifiedSeries *s = build (), *again = build ();
    g_assert_true (atm_verified_series_validate (s, NULL));
    g_test_message ("scientific_id=%s", atm_verified_series_scientific_id (s));
    g_test_message ("qualified_id=%s", atm_verified_series_qualified_id (s));
    char *reference_path = g_build_filename (g_getenv ("ATM_CHART01_FIXTURE"),
                                              "verified-series-identity.json", NULL);
    JsonParser *reference = json_parser_new ();
    g_assert_true (json_parser_load_from_file (reference, reference_path, NULL));
    JsonObject *ids = json_node_get_object (json_parser_get_root (reference));
    g_assert_cmpstr (json_object_get_string_member (ids, "series_profile"), ==, ATM_VERIFIED_SERIES_PROFILE);
    g_assert_cmpstr (atm_verified_series_scientific_id (s), ==,
                    json_object_get_string_member (ids, "scientific_id"));
    g_assert_cmpstr (atm_verified_series_qualified_id (s), ==,
                    json_object_get_string_member (ids, "qualified_id"));
    g_object_unref (reference); g_free (reference_path);
    g_assert_cmpuint (atm_verified_series_count (s), ==, 146);
    g_assert_cmpuint (atm_verified_series_support_count (s), ==, 150);
    g_assert_cmpstr (atm_verified_series_scientific_id (s), ==, atm_verified_series_scientific_id (again));
    g_assert_cmpstr (atm_verified_series_qualified_id (s), ==, atm_verified_series_qualified_id (again));
    g_assert_cmpstr (atm_verified_series_scientific_id (s), !=, atm_verified_series_qualified_id (s));
    g_assert_cmpstr (atm_verified_series_unit (s), ==, "degree_Celsius_anomaly");
    g_assert_cmpstr (atm_verified_series_reference_period (s), ==, "1951-1980");
    GHashTable *aggregate = g_hash_table_new (g_str_hash, g_str_equal);
    GHashTable *used = g_hash_table_new (g_str_hash, g_str_equal);
    for (guint i = 0; i < 150; i++)
        g_assert_true (g_hash_table_add (aggregate, (gpointer) atm_verified_series_support (s, i)));
    for (guint i = 0; i < 146; i++) {
        const AtmVerifiedPoint *p = atm_verified_series_point (s, i);
        g_assert_cmpuint (p->order, ==, i);
        g_assert_cmpuint (p->calendar_year, ==, 1880 + i);
        g_assert_cmpuint (p->source_row, ==, i + 2);
        g_assert_cmpint (p->y_status, ==, ATM_VERIFIED_Y_NUMERIC);
        g_assert_false (p->break_before);
        g_assert_null (p->missing_reason);
        g_assert_null (p->break_reason);
        g_assert_cmpuint (p->support->len, ==, 5);
        for (guint j = 0; j < p->support->len; j++) {
            gpointer id = g_ptr_array_index (p->support, j);
            g_assert_true (g_hash_table_contains (aggregate, id));
            g_hash_table_add (used, id);
        }
        AtmScientificDecimal *decimal = NULL;
        g_assert_true (atm_scientific_decimal_parse (p->source_decimal, &decimal, NULL));
        g_assert_cmpstr (p->coefficient, ==, decimal->coefficient);
        g_assert_cmpint (p->exponent, ==, decimal->exponent);
        atm_scientific_decimal_free (decimal);
    }
    g_assert_cmpuint (g_hash_table_size (used), ==, 150);
    g_assert_cmpstr (atm_verified_series_point (s, 0)->source_decimal, ==, "-0.1700");
    g_assert_cmpstr (atm_verified_series_point (s, 0)->coefficient, ==, "-17");
    g_assert_cmpint (atm_verified_series_point (s, 0)->exponent, ==, -2);
    g_hash_table_unref (used); g_hash_table_unref (aggregate);
    atm_verified_series_free (again); atm_verified_series_free (s);
}
static void test_point_corruption (void)
{
    AtmVerifiedSeries *s = build ();
    /* Deliberately violate the public read-only contract for rejection tests. */
    AtmVerifiedPoint *p = (AtmVerifiedPoint *) atm_verified_series_point (s, 0);
    p->order = 1;
    g_assert_false (atm_verified_series_validate (s, NULL)); p->order = 0;
    p->calendar_year = 1881;
    g_assert_false (atm_verified_series_validate (s, NULL)); p->calendar_year = 1880;
    p->source_row = 3;
    g_assert_false (atm_verified_series_validate (s, NULL)); p->source_row = 2;
    p->y_status = ATM_VERIFIED_Y_MISSING;
    g_assert_false (atm_verified_series_validate (s, NULL)); p->y_status = ATM_VERIFIED_Y_NUMERIC;
    p->break_before = TRUE;
    g_assert_false (atm_verified_series_validate (s, NULL)); p->break_before = FALSE;
    p->missing_reason = g_strdup ("unavailable");
    g_assert_false (atm_verified_series_validate (s, NULL)); g_clear_pointer (&p->missing_reason, g_free);
    p->break_reason = g_strdup ("gap");
    g_assert_false (atm_verified_series_validate (s, NULL)); g_clear_pointer (&p->break_reason, g_free);
    p->exponent = -1;
    g_assert_false (atm_verified_series_validate (s, NULL)); p->exponent = -2;
    p->coefficient[2] = '8';
    g_assert_false (atm_verified_series_validate (s, NULL)); p->coefficient[2] = '7';
    /* Equal mathematical value but changed source spelling is not the pinned source. */
    char *original = p->source_decimal;
    p->source_decimal = g_strdup ("-0.17000");
    g_assert_false (atm_verified_series_validate (s, NULL));
    g_free (p->source_decimal); p->source_decimal = original;
    g_assert_true (atm_verified_series_validate (s, NULL));
    atm_verified_series_free (s);
}
static void test_support_and_identity_corruption (void)
{
    AtmVerifiedSeries *s = build ();
    AtmVerifiedPoint *p = (AtmVerifiedPoint *) atm_verified_series_point (s, 0);
    gpointer original = g_ptr_array_index (p->support, 0);
    g_ptr_array_index (p->support, 0) = NULL;
    g_assert_false (atm_verified_series_validate (s, NULL));
    g_ptr_array_index (p->support, 0) = original;
    char *id = original; char old = id[0]; id[0] = old == 'a' ? 'b' : 'a';
    g_assert_false (atm_verified_series_validate (s, NULL)); id[0] = old;
    gpointer second = g_ptr_array_index (p->support, 1);
    g_ptr_array_index (p->support, 0) = second;
    g_ptr_array_index (p->support, 1) = original;
    g_assert_false (atm_verified_series_validate (s, NULL));
    g_ptr_array_index (p->support, 0) = original;
    g_ptr_array_index (p->support, 1) = second;
    char *content_id = (char *) atm_verified_series_scientific_id (s);
    old = content_id[0]; content_id[0] = old == 'a' ? 'b' : 'a';
    g_assert_false (atm_verified_series_validate (s, NULL)); content_id[0] = old;
    char *qualified_id = (char *) atm_verified_series_qualified_id (s);
    old = qualified_id[0]; qualified_id[0] = old == 'a' ? 'b' : 'a';
    g_assert_false (atm_verified_series_validate (s, NULL)); qualified_id[0] = old;
    g_assert_true (atm_verified_series_validate (s, NULL));
    atm_verified_series_free (s);
}
static void test_rehashed_series (void)
{
    for (guint mode = 0; mode < 3; mode++) {
        AtmVerifiedSeries *s = build ();
        char *scientific = g_strdup (atm_verified_series_scientific_id (s));
        char *qualified = g_strdup (atm_verified_series_qualified_id (s));
        AtmVerifiedPoint *p = (AtmVerifiedPoint *) atm_verified_series_point (s, 0);
        if (mode == 0) {
            g_free (p->source_decimal); p->source_decimal = g_strdup ("-0.17000");
        } else if (mode == 1) {
            p->coefficient[2] = '8';
        } else {
            g_free (g_ptr_array_index (p->support, 0));
            g_ptr_array_index (p->support, 0) = g_strdup (
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        }
        /* Materialized shape and both new hashes are self-consistent. */
        g_assert_true (atm_verified_series_test_rehash (s, NULL));
        if (mode == 1)
            g_assert_cmpstr (atm_verified_series_scientific_id (s), !=, scientific);
        else
            g_assert_cmpstr (atm_verified_series_scientific_id (s), ==, scientific);
        g_assert_cmpstr (atm_verified_series_qualified_id (s), !=, qualified);
        GError *error = NULL;
        g_assert_false (atm_verified_series_validate (s, &error));
        g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
        /* Ensures the source-reconstruction guard, not a stale-hash guard, fired. */
        g_assert_cmpstr (error->message, ==, "Verified series disagrees with reconstructed source.");
        g_clear_error (&error);
        g_free (scientific); g_free (qualified);
        atm_verified_series_free (s);
    }
}
static void test_missing_boundaries (void)
{
    AtmVerifiedSeries *s = build ();
    for (guint mode = 0; mode < 3; mode++) {
        for (guint i = 0; i < 146; i++) {
            AtmVerifiedPoint *p = (AtmVerifiedPoint *) atm_verified_series_point (s, i);
            if (mode == 2 || (mode == 0 && i == 0) || (mode == 1 && i == 145))
                p->y_status = ATM_VERIFIED_Y_MISSING;
        }
        g_assert_false (atm_verified_series_test_rehash (s, NULL));
        g_assert_false (atm_verified_series_validate (s, NULL));
        for (guint i = 0; i < 146; i++)
            ((AtmVerifiedPoint *) atm_verified_series_point (s, i))->y_status = ATM_VERIFIED_Y_NUMERIC;
    }
    g_assert_true (atm_verified_series_validate (s, NULL));
    atm_verified_series_free (s);
}
static void test_arguments (void)
{
    AtmVerifiedSeries *s = NULL;
    g_assert_false (atm_verified_series_from_gistemp (NULL, &s, NULL));
    g_assert_null (s);
    AtmGistempAdmission *a = admit ();
    g_assert_false (atm_verified_series_from_gistemp (a, NULL, NULL));
    g_assert_true (atm_verified_series_from_gistemp (a, &s, NULL));
    AtmVerifiedSeries *saved = s;
    g_assert_false (atm_verified_series_from_gistemp (a, &s, NULL));
    g_assert_true (s == saved);
    atm_gistemp_admission_free (a);
    g_assert_null (atm_verified_series_point (s, 146));
    g_assert_null (atm_verified_series_support (s, 150));
    g_assert_null (atm_verified_series_point (NULL, 0));
    g_assert_null (atm_verified_series_support (NULL, 0));
    g_assert_cmpuint (atm_verified_series_count (NULL), ==, 0);
    g_assert_false (atm_verified_series_validate (NULL, NULL));
    atm_verified_series_free (s); atm_verified_series_free (NULL);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/verified-series/complete", test_complete_series);
    g_test_add_func ("/verified-series/point-corruption", test_point_corruption);
    g_test_add_func ("/verified-series/support-identity", test_support_and_identity_corruption);
    g_test_add_func ("/verified-series/arguments", test_arguments);
    g_test_add_func ("/verified-series/rehashed-series", test_rehashed_series);
    g_test_add_func ("/verified-series/missing-boundaries", test_missing_boundaries);
    return g_test_run ();
}
