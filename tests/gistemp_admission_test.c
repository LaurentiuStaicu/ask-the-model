#include "gistemp_admission.h"
#include <json-glib/json-glib.h>
#include <string.h>

static const char *repo = "LaurentiuStaicu/empirical-world3-dynamics";
static const char *snapshot = "d9e249339663015f6d1c05752338a955bf64ad0b";
static GBytes *inputs[4];
static char *buffers[4];
static gsize lengths[4];
static JsonParser *lock_parser;

static void load_sources (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    char *lock_path = g_build_filename (root, "source-lock.json", NULL);
    lock_parser = json_parser_new ();
    g_assert_true (json_parser_load_from_file (lock_parser, lock_path, NULL));
    g_free (lock_path);
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json", "science/data/registry.csv"
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
    AtmGistempAdmission *a = NULL;
    GError *error = NULL;
    g_assert_false (atm_gistemp_admission_new (r, s, b, &a, &error));
    g_assert_error (error, ATM_ANNUAL_SERIES_ERROR, code);
    g_assert_null (a);
    g_clear_error (&error);
}
static void test_policy (void)
{
    AtmGistempAdmission *a = NULL;
    g_assert_true (atm_gistemp_admission_new (repo, snapshot, inputs, &a, NULL));
    JsonObject *lock = json_node_get_object (json_parser_get_root (lock_parser));
    JsonObject *files = json_object_get_object_member (lock, "files");
    g_assert_cmpstr (atm_gistemp_admission_repository (a), ==,
                    json_object_get_string_member (lock, "repository"));
    g_assert_cmpstr (atm_gistemp_admission_snapshot (a), ==,
                    json_object_get_string_member (lock, "snapshot_sha"));
    for (guint i = 0; i < 4; i++)
        g_assert_cmpstr (atm_gistemp_admission_source_digest (a, i), ==,
            json_object_get_string_member (files, atm_gistemp_admission_source_path (a, i)));
    const AtmAnnualSeriesCandidate *c = atm_gistemp_admission_candidate (a);
    g_assert_cmpuint (atm_annual_series_candidate_count (c), ==, 146);
    g_assert_cmpuint (atm_annual_series_candidate_source_row (c, 145), ==, 147);
    g_assert_cmpstr (atm_gistemp_admission_unit (a), ==, "degree_Celsius_anomaly");
    g_assert_cmpstr (atm_gistemp_admission_reference_period (a), ==, "1951-1980");
    g_assert_cmpstr (atm_gistemp_admission_selection (a), ==,
                    "Global annual J-D Land-Ocean Temperature Index");
    g_assert_cmpstr (atm_gistemp_admission_vintage (a), ==, "2026-08-31");
    g_assert_cmpstr (atm_gistemp_admission_subject (a), ==, "global_surface_temperature");
    g_assert_cmpstr (atm_gistemp_admission_attribute (a), ==, "annual_temperature_anomaly");
    g_assert_cmpstr (atm_gistemp_admission_epistemic_status (a), ==, "empirical");
    g_assert_cmpstr (atm_gistemp_admission_x_semantics (a), ==, "calendar_year");
    g_assert_null (atm_gistemp_admission_source_path (a, 4));
    g_assert_null (atm_gistemp_admission_source_digest (a, G_MAXUINT));
    g_assert_false (atm_gistemp_admission_new (repo, snapshot, inputs, &a, NULL));
    g_assert_true (c == atm_gistemp_admission_candidate (a));
    atm_gistemp_admission_free (a);
}
static void test_tamper (void)
{
    for (guint i = 0; i < 4; i++) {
        GBytes *modified[4];
        memcpy (modified, inputs, sizeof modified);
        char *bytes = g_memdup2 (buffers[i], lengths[i]);
        bytes[lengths[i] / 2] ^= 1;
        modified[i] = g_bytes_new_take (bytes, lengths[i]);
        reject_bundle (repo, snapshot, modified, ATM_ANNUAL_SERIES_ERROR_SHAPE);
        g_bytes_unref (modified[i]);
        modified[i] = NULL;
        reject_bundle (repo, snapshot, modified, ATM_ANNUAL_SERIES_ERROR_ARGUMENT);
        modified[i] = g_bytes_new_take (g_malloc0 (65537), 65537);
        reject_bundle (repo, snapshot, modified, ATM_ANNUAL_SERIES_ERROR_LIMIT);
        g_bytes_unref (modified[i]);
        /* Exact allocation boundary: admissible size, inadmissible content. */
        modified[i] = g_bytes_new_take (g_malloc0 (65536), 65536);
        reject_bundle (repo, snapshot, modified, ATM_ANNUAL_SERIES_ERROR_SHAPE);
        g_bytes_unref (modified[i]);
    }
    GBytes *swapped[] = {inputs[1], inputs[0], inputs[2], inputs[3]};
    reject_bundle (repo, snapshot, swapped, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    reject_bundle ("ewd", snapshot, inputs, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    reject_bundle (repo, "v0.1.0", inputs, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    reject_bundle (repo, NULL, inputs, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    reject_bundle (NULL, snapshot, inputs, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    reject_bundle (repo, snapshot, NULL, ATM_ANNUAL_SERIES_ERROR_ARGUMENT);
    g_assert_false (atm_gistemp_admission_new (repo, snapshot, inputs, NULL, NULL));
    g_assert_null (atm_gistemp_admission_candidate (NULL));
    atm_gistemp_admission_free (NULL);
}
static void test_ownership (void)
{
    AtmGistempAdmission *a = NULL;
    g_assert_true (atm_gistemp_admission_new (repo, snapshot, inputs, &a, NULL));
    /* Caller mutation after admission cannot change admitted points/metadata. */
    for (guint i = 0; i < 4; i++) buffers[i][0] ^= 1;
    g_assert_cmpstr (atm_annual_series_candidate_decimal (
        atm_gistemp_admission_candidate (a), 0), ==, "-0.1700");
    g_assert_cmpstr (atm_gistemp_admission_reference_period (a), ==, "1951-1980");
    reject_bundle (repo, snapshot, inputs, ATM_ANNUAL_SERIES_ERROR_SHAPE);
    for (guint i = 0; i < 4; i++) buffers[i][0] ^= 1;
    atm_gistemp_admission_free (a);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    load_sources ();
    g_test_add_func ("/gistemp-admission/policy", test_policy);
    g_test_add_func ("/gistemp-admission/tamper", test_tamper);
    g_test_add_func ("/gistemp-admission/ownership", test_ownership);
    int result = g_test_run ();
    for (guint i = 0; i < 4; i++) {
        g_bytes_unref (inputs[i]);
        g_free (buffers[i]);
    }
    g_object_unref (lock_parser);
    return result;
}
