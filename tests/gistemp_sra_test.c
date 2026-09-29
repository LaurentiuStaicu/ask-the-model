#include "gistemp_sra.h"

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
        char *bytes;
        gsize length;
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
static AtmGistempSra *build (void)
{
    AtmGistempAdmission *a = admit ();
    AtmGistempSra *s = NULL;
    GError *error = NULL;
    g_assert_true (atm_gistemp_sra_new (a, &s, &error));
    g_assert_no_error (error);
    atm_gistemp_admission_free (a);
    return s;
}
static void test_finalized (void)
{
    AtmGistempSra *s = build ();
    AtmGistempSra *again = build ();
    const AtmSraResult *r = atm_gistemp_sra_result (s);
    g_assert_true (atm_gistemp_sra_validate (s, NULL));
    g_assert_true (atm_sra_result_validate (r, NULL));
    g_assert_true (r->finalized);
    g_assert_cmpuint (r->established_facts->len, ==, 146);
    g_assert_cmpuint (r->derived_facts->len, ==, 0);
    g_assert_cmpuint (r->qualification->evidence_atom_ids->len, ==, 150);
    g_assert_cmpuint (r->qualification->operations->len, ==, 0);
    g_assert_cmpuint (r->limitations->len, ==, 3);
    for (guint i = 0; i < 146; i++) {
        const AtmSraEstablishedFact *fact = g_ptr_array_index (r->established_facts, i);
        g_assert_cmpuint (fact->support->len, ==, 5);
    }
    const AtmSraEstablishedFact *first = g_ptr_array_index (r->established_facts, 0);
    const AtmSraEstablishedFact *last = g_ptr_array_index (r->established_facts, 145);
    g_assert_cmpstr (first->fact_id, ==, "ewd:gistemp-annual:1880");
    g_assert_cmpstr (last->fact_id, ==, "ewd:gistemp-annual:2025");
    g_assert_cmpstr (first->value, ==, "-17e-2");
    g_assert_cmpstr (first->unit, ==, "degree_Celsius_anomaly");
    g_assert_cmpstr (r->qualification->scientific_content_id, ==,
                    atm_gistemp_sra_result (again)->qualification->scientific_content_id);
    g_assert_cmpstr (r->qualification->qualified_artifact_id, ==,
                    atm_gistemp_sra_result (again)->qualification->qualified_artifact_id);
    atm_gistemp_sra_free (again);
    atm_gistemp_sra_free (s);
}
/* Deliberately violate the borrowed read-only contract to exercise rejection
 * of a forged result with correct generic SRA identities. */
static void refinalize (AtmSraResult *r)
{
    r->finalized = FALSE;
    g_clear_pointer (&r->qualification->scientific_content_id, g_free);
    g_clear_pointer (&r->qualification->qualified_artifact_id, g_free);
    g_assert_true (atm_sra_result_finalize (r, NULL));
    g_assert_true (atm_sra_result_validate (r, NULL));
}
static void test_refinalized_forgery (void)
{
    for (guint mode = 0; mode < 5; mode++) {
        AtmGistempSra *s = build ();
        AtmSraResult *r = (AtmSraResult *) atm_gistemp_sra_result (s);
        AtmSraEstablishedFact *fact = g_ptr_array_index (r->established_facts, 0);
        if (mode == 0) { g_free (fact->value); fact->value = g_strdup ("999e0"); }
        if (mode == 1) { g_free (fact->unit); fact->unit = g_strdup ("kelvin"); }
        if (mode == 2) { g_ptr_array_remove_index (fact->support, 0); }
        if (mode == 3) { g_ptr_array_remove_index (r->limitations, 0); }
        if (mode == 4) {
            g_free (r->qualification->numeric_profile);
            r->qualification->numeric_profile = g_strdup ("unreviewed-numeric-profile");
        }
        refinalize (r);
        GError *error = NULL;
        g_assert_false (atm_gistemp_sra_validate (s, &error));
        g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
        g_clear_error (&error);
        atm_gistemp_sra_free (s);
    }
}
static void test_arguments_and_unfinalized (void)
{
    AtmGistempSra *s = NULL;
    g_assert_false (atm_gistemp_sra_new (NULL, &s, NULL));
    g_assert_null (s);
    AtmGistempAdmission *a = admit ();
    g_assert_false (atm_gistemp_sra_new (a, NULL, NULL));
    g_assert_true (atm_gistemp_sra_new (a, &s, NULL));
    AtmGistempSra *saved = s;
    g_assert_false (atm_gistemp_sra_new (a, &s, NULL));
    g_assert_true (s == saved);
    atm_gistemp_admission_free (a);
    AtmSraResult *r = (AtmSraResult *) atm_gistemp_sra_result (s);
    r->finalized = FALSE;
    g_assert_false (atm_gistemp_sra_validate (s, NULL));
    r->finalized = TRUE;
    g_assert_true (atm_gistemp_sra_validate (s, NULL));
    atm_gistemp_sra_free (s);
    g_assert_false (atm_gistemp_sra_validate (NULL, NULL));
    g_assert_null (atm_gistemp_sra_result (NULL));
    atm_gistemp_sra_free (NULL);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/gistemp-sra/finalized", test_finalized);
    g_test_add_func ("/gistemp-sra/refinalized-forgery", test_refinalized_forgery);
    g_test_add_func ("/gistemp-sra/arguments", test_arguments_and_unfinalized);
    return g_test_run ();
}
