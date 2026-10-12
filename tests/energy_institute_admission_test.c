#include "energy_institute_admission.h"
#include <json-glib/json-glib.h>
#include <string.h>

static const char *repo = "LaurentiuStaicu/empirical-world3-dynamics";
static const char *snapshot = "d9e249339663015f6d1c05752338a955bf64ad0b";
static GBytes *inputs[4];
static char *buffers[4];
static gsize lengths[4];
static const char *current_snapshot = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
static GBytes *current_inputs[4];
static char *current_buffers[4];
static gsize current_lengths[4];

 /*
  * Independently walk the pinned CSV rows and compare every parsed candidate
  * point with the raw year and second-column token. The legacy/generic
  * equivalence test uses the same candidate parser, so it is not an independent
  * check that all 61 row-to-value mappings are correct.
  */
static void assert_all_candidate_points_match_source_csv (
    const AtmEnergyInstituteCandidate *candidate,
    const char *csv,
    gsize csv_length)
{
    g_assert_nonnull(candidate);
    g_assert_nonnull(csv);

    const char *cursor = csv;
    const char *limit = cursor + csv_length;
    const char *header_end = memchr(cursor, '\n', (gsize) (limit - cursor));
    g_assert_nonnull(header_end);
    cursor = header_end + 1;

    guint count = atm_energy_institute_candidate_count(candidate);
    g_assert_cmpuint(count, ==, 61u);

    for (guint i = 0; i < count; i++) {
        const char *row_end = memchr(cursor, '\n', (gsize) (limit - cursor));
        if (row_end == NULL)
            row_end = limit;
        g_assert_true(row_end > cursor);
        g_assert_null(memchr(cursor, '\r', (gsize) (row_end - cursor)));

        const char *first_comma =
            memchr(cursor, ',', (gsize) (row_end - cursor));
        g_assert_nonnull(first_comma);
        const char *second_comma =
            memchr(first_comma + 1, ',',
                   (gsize) (row_end - (first_comma + 1)));
        g_assert_nonnull(second_comma);

        char *source_year = g_strndup(cursor, (gsize) (first_comma - cursor));
        char *source_decimal = g_strndup(
            first_comma + 1, (gsize) (second_comma - (first_comma + 1)));
        char *expected_year = g_strdup_printf("%u", 1965u + i);

        g_assert_cmpstr(source_year, ==, expected_year);
        g_assert_cmpuint(atm_energy_institute_candidate_year(candidate, i),
                         ==, 1965u + i);
        g_assert_cmpuint(atm_energy_institute_candidate_source_row(candidate, i),
                         ==, i + 2u);
        g_assert_cmpstr(atm_energy_institute_candidate_decimal(candidate, i),
                        ==, source_decimal);

        g_free(expected_year);
        g_free(source_decimal);
        g_free(source_year);

        cursor = row_end < limit ? row_end + 1 : limit;
        if (i + 1u < count)
            g_assert_true(cursor < limit);
    }

    g_assert_true(cursor == limit);
}

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

        path = g_build_filename (root, "ewd-current", paths[i], NULL);
        g_assert_true (g_file_get_contents (path, &current_buffers[i],
                                            &current_lengths[i], NULL));
        current_inputs[i] = g_bytes_new_static (current_buffers[i],
                                                current_lengths[i]);
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
    assert_all_candidate_points_match_source_csv (c, buffers[0], lengths[0]);
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
                    "global_total_energy_supply");
    g_assert_cmpstr (atm_energy_institute_admission_attribute (a), ==,
                    "total_energy_supply");
    g_assert_cmpstr (atm_energy_institute_admission_accounting_method (a), ==,
                    "physical_energy_content");
    g_assert_cmpstr (atm_energy_institute_admission_epistemic_status (a), ==, "empirical");
    g_assert_cmpstr (atm_energy_institute_admission_x_semantics (a), ==, "calendar_year");
    for (guint i = 0; i < 4; i++)
        g_assert_nonnull (atm_energy_institute_admission_source_digest (a, i));
    g_assert_null (atm_energy_institute_admission_source_path (a, 4));
    g_assert_null (atm_energy_institute_admission_source_digest (a, G_MAXUINT));
    atm_energy_institute_admission_free (a);
}

static char *
registry_energy_institute_row (const char *registry)
{
    gchar **lines = g_strsplit (registry, "\n", -1);
    char *found = NULL;
    for (guint i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix (lines[i], "ei_primary_energy,")) {
            found = g_strdup (lines[i]);
            break;
        }
    }
    g_strfreev (lines);
    return found;
}

static void
assert_current_snapshot_fixture_lock (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    char *lock_path = g_build_filename (root, "ei-current-snapshot-lock.json", NULL);
    JsonParser *parser = json_parser_new ();
    GError *error = NULL;
    if (!json_parser_load_from_file (parser, lock_path, &error))
        g_error ("Cannot read current EI lock %s: %s", lock_path, error->message);
    g_free (lock_path);

    JsonObject *root_object = json_node_get_object (json_parser_get_root (parser));
    g_assert_cmpstr (json_object_get_string_member (root_object, "snapshot"),
                     ==, current_snapshot);
    g_assert_cmpstr (json_object_get_string_member (root_object, "admission_profile"),
                     ==, "atm-energy-institute-pinned-admission/3");
    JsonObject *files = json_object_get_object_member (root_object, "files");
    g_assert_nonnull (files);

    const char *names[] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    for (guint i = 0; i < G_N_ELEMENTS (names); i++) {
        char *digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
            (const guchar *) current_buffers[i], current_lengths[i]);
        g_assert_cmpstr (digest, ==, json_object_get_string_member (files, names[i]));
        g_free (digest);
    }

    g_assert_true (strstr (current_buffers[2],
        "\"science/data/processed/energy_institute_global_2026.csv\": \"46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c\"") != NULL);
    g_assert_true (strstr (current_buffers[2],
        "\"science/data/processed/energy_institute_global_2026.provenance.json\": \"1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57\"") != NULL);

    char *old_row = registry_energy_institute_row (buffers[3]);
    char *current_row = registry_energy_institute_row (current_buffers[3]);
    g_assert_nonnull (old_row);
    g_assert_nonnull (current_row);
    g_assert_cmpstr (old_row, ==, current_row);
    g_free (old_row);
    g_free (current_row);
    g_object_unref (parser);
}

static void
test_current_snapshot_policy_and_reconstruction (void)
{
    AtmEnergyInstituteAdmission *current = NULL;
    GError *error = NULL;
    assert_current_snapshot_fixture_lock ();

    g_assert_cmpstr (ATM_EI_ADMISSION_PROFILE, ==,
                     "atm-energy-institute-pinned-admission/3");
    g_assert_true (atm_energy_institute_admission_new (repo, current_snapshot,
        current_inputs, &current, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_energy_institute_admission_snapshot (current), ==,
                     current_snapshot);
    g_assert_cmpuint (atm_energy_institute_candidate_count (
        atm_energy_institute_admission_candidate (current)), ==, 61u);
    assert_all_candidate_points_match_source_csv (
        atm_energy_institute_admission_candidate (current),
        current_buffers[0], current_lengths[0]);

    const char *expected_digests[] = {
        "46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c",
        "1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57",
        "ece72aed0d444484635e38171df703a775e5922df95b8783385cbfed7e2ffbd6",
        "6ff4e0bffe03cce368dcd278d6ca57b6e8f181aa241dea7206508499ec3fa3b8"
    };
    for (guint i = 0; i < 4; i++)
        g_assert_cmpstr (atm_energy_institute_admission_source_digest (current, i),
                         ==, expected_digests[i]);

    AtmEnergyInstituteAdmission *rebuilt = NULL;
    g_assert_true (atm_energy_institute_admission_rebuild (current, &rebuilt, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_energy_institute_admission_snapshot (rebuilt), ==,
                     current_snapshot);
    g_assert_cmpstr (atm_energy_institute_admission_source_digest (rebuilt, 2), ==,
                     expected_digests[2]);
    atm_energy_institute_admission_free (rebuilt);

    /* Exact bundle binding: a current manifest/registry cannot be mixed with
     * the older bundle, and the old bundle cannot be relabeled as current. */
    GBytes *current_bundle_with_old_manifest[4] = {
        current_inputs[0], current_inputs[1], inputs[2], current_inputs[3]
    };
    reject_bundle (repo, current_snapshot, current_bundle_with_old_manifest,
                   ATM_EI_CANDIDATE_ERROR_SHAPE);
    GBytes *old_bundle_with_current_manifest[4] = {
        inputs[0], inputs[1], current_inputs[2], inputs[3]
    };
    reject_bundle (repo, snapshot, old_bundle_with_current_manifest,
                   ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (repo, "dc710d8e604c0fff713a3acfdb18ab47a1a902bc",
                   current_inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (repo, current_snapshot, inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);
    reject_bundle (repo, snapshot, current_inputs, ATM_EI_CANDIDATE_ERROR_SHAPE);

    atm_energy_institute_admission_free (current);
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

    for (guint i = 0; i < 4; i++) {
        GBytes *modified[4];
        memcpy (modified, current_inputs, sizeof modified);
        char *bytes = g_memdup2 (current_buffers[i], current_lengths[i]);
        bytes[current_lengths[i] / 2] ^= 1;
        modified[i] = g_bytes_new_take (bytes, current_lengths[i]);
        reject_bundle (repo, current_snapshot, modified, ATM_EI_CANDIDATE_ERROR_SHAPE);
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
        "year,total_primary_energy_ej,fossil_energy_ej,oil_ej,gas_ej,coal_ej,nuclear_ej,hydro_ej,renewables_ej,non_fossil_energy_ej,fossil_share,non_fossil_share,component_reconciliation_error_ej\n"
        "196,149.6960972,0,0,0,0,0,0,0,0,0,0,0\n";
    source = g_bytes_new_static (short_year, strlen (short_year));
    g_assert_false (atm_energy_institute_candidate_parse (source, &candidate, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, ATM_EI_CANDIDATE_ERROR_SHAPE);
    g_clear_error (&error);
    g_assert_null (candidate);
    g_bytes_unref (source);

    const char *extra_column =
        "year,total_primary_energy_ej,fossil_energy_ej,oil_ej,gas_ej,coal_ej,nuclear_ej,hydro_ej,renewables_ej,non_fossil_energy_ej,fossil_share,non_fossil_share,component_reconciliation_error_ej\n"
        "1965,149.6960972,0,0,0,0,0,0,0,0,0,0,0,unexpected\n";
    source = g_bytes_new_static (extra_column, strlen (extra_column));
    g_assert_false (atm_energy_institute_candidate_parse (source, &candidate, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, ATM_EI_CANDIDATE_ERROR_SHAPE);
    g_clear_error (&error);
    g_assert_null (candidate);
    g_bytes_unref (source);
}

static void test_nonconsecutive_years_rejected (void)
{
    const char *source_text =
        "year,total_primary_energy_ej,fossil_energy_ej,oil_ej,gas_ej,coal_ej,nuclear_ej,hydro_ej,renewables_ej,non_fossil_energy_ej,fossil_share,non_fossil_share,component_reconciliation_error_ej\n"
        "1965,149.6960972,0,0,0,0,0,0,0,0,0,0,0\n"
        "1967,150.0000000,0,0,0,0,0,0,0,0,0,0,0\n";
    GBytes *source = g_bytes_new_static (source_text, strlen (source_text));
    AtmEnergyInstituteCandidate *candidate = NULL;
    GError *error = NULL;

    g_assert_false (atm_energy_institute_candidate_parse (source, &candidate, &error));
    g_assert_error (error, ATM_EI_CANDIDATE_ERROR, ATM_EI_CANDIDATE_ERROR_ORDER);
    g_assert_null (candidate);

    g_clear_error (&error);
    g_bytes_unref (source);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    load_sources ();
    g_test_add_func ("/energy-institute-admission/policy", test_policy);
    g_test_add_func ("/energy-institute-admission/current-snapshot",
                     test_current_snapshot_policy_and_reconstruction);
    g_test_add_func ("/energy-institute-admission/tamper", test_tamper_and_shape);
    g_test_add_func ("/energy-institute-admission/parser-boundaries", test_candidate_parser_boundaries);
    g_test_add_func ("/energy-institute-admission/nonconsecutive-years", test_nonconsecutive_years_rejected);
    int result = g_test_run ();
    for (guint i = 0; i < 4; i++) {
        g_bytes_unref (inputs[i]);
        g_free (buffers[i]);
        g_bytes_unref (current_inputs[i]);
        g_free (current_buffers[i]);
    }
    return result;
}
