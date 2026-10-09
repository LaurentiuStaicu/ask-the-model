#include "energy_institute_admission.h"
#include <string.h>

static const char *repo = "LaurentiuStaicu/empirical-world3-dynamics";
static const char *snapshot = "d9e249339663015f6d1c05752338a955bf64ad0b";
static GBytes *inputs[4];
static char *buffers[4];
static gsize lengths[4];

static void load_sources (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    const char *paths[] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (root, "ewd", paths[i], NULL);
        g_assert_true (g_file_get_contents (path, &buffers[i], &lengths[i], NULL));
        inputs[i] = g_bytes_new_static (buffers[i], lengths[i]);
        g_free (path);
    }
}

static void reject_bundle (const char *r, const char *s, GBytes **b, gint code)
{
    AtmEnergyInstituteAdmission *a = NULL;
    GError *error = NULL;
    g_assert_false (atm_energy_institute_admission_new (r, s, b, &a, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, code);
    g_assert_null (a);
    g_clear_error (&error);
}

static void test_policy (void)
{
    AtmEnergyInstituteAdmission *a = NULL;
    g_assert_true (atm_energy_institute_admission_new (repo, snapshot, inputs, &a, NULL));
    const AtmEnergyInstituteCandidate *c = atm_energy_institute_admission_candidate (a);
    g_assert_cmpuint (atm_energy_institute_candidate_count (c), ==, 61);
    g_assert_cmpuint (atm_energy_institute_candidate_year (c, 0), ==, 1965);
    g_assert_cmpuint (atm_energy_institute_candidate_year (c, 60), ==, 2025);
    g_assert_cmpuint (atm_energy_institute_candidate_source_row (c, 60), ==, 62);
    g_assert_cmpstr (atm_energy_institute_candidate_decimal (c, 0), ==, "149.6960972");
    g_assert_cmpstr (atm_energy_institute_candidate_decimal (c, 54), ==, "556.6345305");
    g_assert_cmpstr (atm_energy_institute_candidate_decimal (c, 60), ==, "600.3129128");
    g_assert_cmpstr (atm_energy_institute_admission_unit (a), ==, "exajoules/year");
    g_assert_cmpstr (atm_energy_institute_admission_geography (a), ==, "Total World");
    g_assert_cmpstr (atm_energy_institute_admission_dataset (a), ==,
                    "Statistical Review of World Energy 2026");
    g_assert_cmpstr (atm_energy_institute_admission_subject (a), ==,
                    "global_primary_energy_supply");
    g_assert_cmpstr (atm_energy_institute_admission_attribute (a), ==,
                    "total_primary_energy");
    g_assert_cmpstr (atm_energy_institute_admission_epistemic_status (a), ==, "empirical");
    g_assert_cmpstr (atm_energy_institute_admission_x_semantics (a), ==, "calendar_year");
    for (guint i = 0; i < 4; i++)
        g_assert_nonnull (atm_energy_institute_admission_source_digest (a, i));
    g_assert_null (atm_energy_institute_admission_source_path (a, 4));
    g_assert_null (atm_energy_institute_admission_source_digest (a, G_MAXUINT));
    atm_energy_institute_admission_free (a);
}

static void test_tamper_and_shape (void)
{
    for (guint i = 0; i < 4; i++) {
        GBytes *modified[4];
        memcpy (modified, inputs, sizeof modified);
        char *bytes = g_memdup2 (buffers[i], lengths[i]);
        bytes[lengths[i] / 2] ^= 1;
        modified[i] = g_bytes_new_take (bytes, lengths[i]);
        reject_bundle (repo, snapshot, modified, ATM_EI_CANDIDATE_ERROR_SHAPE);
        g_bytes_unref (modified[i]);
    }
    GBytes *swapped[] = {inputs[1], inputs[0], inputs[2], inputs[3]};
    reject_bundle (repo, snapshot, swapped, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle ("ewd", snapshot, inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (repo, "v0.1.0", inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (repo, NULL, inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (NULL, snapshot, inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (repo, snapshot, NULL, ATM_EI_CANDIDATE_ERROR_ARGUMENT);
}

static void test_candidate_parser_boundaries (void)
{
    const char *valid = "year,total_primary_energy_ej,fossil_energy_ej,oil_ej,gas_ej,coal_ej,nuclear_ej,hydro_ej,renewables_ej,non_fossil_energy_ej,fossil_share,non_fossil_share,component_reconciliation_error_ej\n1965,149.6960972,0,0,0,0,0,0,0,0,0,0,0\n";
    GBytes *source = g_bytes_new_static (valid, strlen (valid));
    AtmEnergyInstituteCandidate *candidate = NULL;
    GError *error = NULL;
    g_assert_false (atm_energy_institute_candidate_parse (source, &candidate, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, ATM_EI_CANDIDATE_ERROR_SHAPE);
    g_clear_error (&error);
    g_assert_null (candidate);
    g_bytes_unref (source);

    const char *short_year =
        "year,total_primary_energy_ej,fossil_energy_ej,oil_ej,gas_ej,coal_ej,nuclear_ej,hydro_ej,renewables_ej,non_fossil_energy_ej,fossil_share,non_fossil_share,component_reconciliation_error_ej\\n"
        "196,149.6960972,0,0,0,0,0,0,0,0,0,0,0\\n";
    source = g_bytes_new_static (short_year, strlen (short_year));
    g_assert_false (atm_energy_institute_candidate_parse (source, &candidate, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, ATM_EI_CANDIDATE_ERROR_SHAPE);
    g_clear_error (&error);
    g_assert_null (candidate);
    g_bytes_unref (source);

    const char *extra_column =
        "year,total_primary_energy_ej,fossil_energy_ej,oil_ej,gas_ej,coal_ej,nuclear_ej,hydro_ej,renewables_ej,non_fossil_energy_ej,fossil_share,non_fossil_share,component_reconciliation_error_ej\\n"
        "1965,149.6960972,0,0,0,0,0,0,0,0,0,0,0,unexpected\\n";
    source = g_bytes_new_static (extra_column, strlen (extra_column));
    g_assert_false (atm_energy_institute_candidate_parse (source, &candidate, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, ATM_EI_CANDIDATE_ERROR_SHAPE);
    g_clear_error (&error);
    g_assert_null (candidate);
    g_bytes_unref (source);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    load_sources ();
    g_test_add_func ("/energy-institute-admission/policy", test_policy);
    g_test_add_func ("/energy-institute-admission/tamper", test_tamper_and_shape);
    g_test_add_func ("/energy-institute-admission/parser-boundaries", test_candidate_parser_boundaries);
    int result = g_test_run ();
    for (guint i = 0; i < 4; i++) {
        g_bytes_unref (inputs[i]);
        g_free (buffers[i]);
    }
    return result;
}
