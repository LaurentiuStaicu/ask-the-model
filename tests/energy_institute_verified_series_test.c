#include "energy_institute_verified_series.h"

#include "series_adapter.h"
#include "series_evidence.h"
#include "series_evidence_adapters.h"
#include "series_sra.h"

#include <json-glib/json-glib.h>
#include <string.h>

#define TEST_REPOSITORY "LaurentiuStaicu/empirical-world3-dynamics"
#define TEST_SNAPSHOT "d9e249339663015f6d1c05752338a955bf64ad0b"

static char *
fixture_path (const char *relative)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    return g_build_filename (root, "ewd", relative, NULL);
}

static GBytes *
load_fixture (const char *relative)
{
    char *path = fixture_path (relative);
    gchar *data = NULL;
    gsize length = 0;
    GError *error = NULL;
    if (!g_file_get_contents (path, &data, &length, &error))
        g_error ("Cannot load fixture %s: %s", path, error->message);
    g_free (path);
    return g_bytes_new_take (data, length);
}

static AtmEnergyInstituteAdmission *
build_admission (void)
{
    GBytes *sources[ATM_EI_SOURCE_COUNT] = {
        load_fixture ("science/data/processed/energy_institute_global_2026.csv"),
        load_fixture ("science/data/processed/energy_institute_global_2026.provenance.json"),
        load_fixture ("science/data/input_manifest.json"),
        load_fixture ("science/data/registry.csv"),
    };
    AtmEnergyInstituteAdmission *admission = NULL;
    GError *error = NULL;
    g_assert_true (atm_energy_institute_admission_new (
        TEST_REPOSITORY, TEST_SNAPSHOT, sources, &admission, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);
    return admission;
}

static AtmEnergyInstituteVerifiedSeries *
build_series (void)
{
    AtmEnergyInstituteAdmission *admission = build_admission ();
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    GError *error = NULL;
    g_assert_true (atm_energy_institute_verified_series_from_admission (
        admission, &series, &error));
    g_assert_no_error (error);
    atm_energy_institute_admission_free (admission);
    return series;
}

static void
assert_rejected_then_restored (AtmEnergyInstituteVerifiedSeries *series)
{
    GError *error = NULL;
    g_assert_false (atm_energy_institute_verified_series_validate (series, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_true (atm_energy_institute_verified_series_validate (series, &error));
    g_assert_no_error (error);
}

static void
test_complete_series_and_reconstruction (void)
{
    AtmEnergyInstituteVerifiedSeries *first = build_series ();
    AtmEnergyInstituteVerifiedSeries *second = build_series ();
    GError *error = NULL;

    g_assert_true (atm_energy_institute_verified_series_validate (first, &error));
    g_assert_no_error (error);
    g_assert_true (atm_energy_institute_verified_series_validate (second, &error));
    g_assert_no_error (error);

    const AtmSeriesContract *c =
        atm_energy_institute_verified_series_contract (first);
    g_assert_nonnull (c);
    g_assert_cmpuint (atm_energy_institute_verified_series_count (first), ==, 61u);
    g_assert_cmpstr (c->subject, ==, "global_total_energy_supply");
    g_assert_cmpstr (c->attribute, ==, "total_energy_supply");
    g_assert_cmpstr (c->y_unit, ==, "exajoules/year");
    g_assert_cmpstr (c->y_dimension, ==, "energy_flow_rate");
    g_assert_cmpstr (c->time_scope, ==, "1965-2025");
    g_assert_cmpstr (c->series_semantics, ==, "empirical_annual_observations");
    g_assert_cmpstr (c->scenario, ==, "not_applicable");
    g_assert_cmpstr (c->epistemic_status, ==, "empirical");
    g_assert_cmpstr (c->snapshot_sha, ==, TEST_SNAPSHOT);
    g_assert_cmpstr (c->profile_id, ==, ATM_EI_VERIFIED_SERIES_PROFILE);
    g_assert_cmpstr (c->profile_version, ==, "1");

    g_assert_cmpstr (atm_energy_institute_verified_series_scientific_id (first),
                     ==,
                     atm_energy_institute_verified_series_scientific_id (second));
    g_assert_cmpstr (atm_energy_institute_verified_series_qualified_id (first),
                     ==,
                     atm_energy_institute_verified_series_qualified_id (second));
    g_assert_cmpstr (atm_energy_institute_verified_series_scientific_id (first),
                     !=,
                     atm_energy_institute_verified_series_qualified_id (first));

    const AtmSeriesContractPoint *begin =
        atm_energy_institute_verified_series_point (first, 0);
    const AtmSeriesContractPoint *end =
        atm_energy_institute_verified_series_point (first, 60);
    g_assert_nonnull (begin);
    g_assert_nonnull (end);
    g_assert_cmpuint (begin->order, ==, 0);
    g_assert_cmpstr (begin->x, ==, "1965");
    g_assert_cmpstr (begin->source_decimal, ==, "149.6960972");
    g_assert_cmpuint (begin->y_status, ==, ATM_SERIES_Y_NUMERIC);
    g_assert_cmpuint (begin->support->len, ==, 5u);
    g_assert_cmpuint (end->order, ==, 60);
    g_assert_cmpstr (end->x, ==, "2025");
    g_assert_cmpstr (end->source_decimal, ==, "600.3129128");
    g_assert_cmpuint (end->support->len, ==, 5u);

    AtmEnergyInstituteVerifiedSeries *rebuilt = NULL;
    g_assert_true (atm_energy_institute_verified_series_rebuild (
        first, &rebuilt, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_energy_institute_verified_series_scientific_id (first),
                     ==,
                     atm_energy_institute_verified_series_scientific_id (rebuilt));
    g_assert_cmpstr (atm_energy_institute_verified_series_qualified_id (first),
                     ==,
                     atm_energy_institute_verified_series_qualified_id (rebuilt));

    atm_energy_institute_verified_series_free (rebuilt);
    atm_energy_institute_verified_series_free (second);
    atm_energy_institute_verified_series_free (first);
}

static void
test_each_point_uses_exact_sra_support (void)
{
    AtmEnergyInstituteAdmission *admission = build_admission ();
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    AtmSeriesSra *sra = NULL;
    GError *error = NULL;

    g_assert_true (atm_energy_institute_verified_series_from_admission (
        admission, &series, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_sra_new (&ATM_EI_EVIDENCE_DESCRIPTOR,
                                       admission, &sra, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_sra_validate (sra, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (atm_series_evidence_count (
        atm_series_sra_evidence (sra)), ==, 65u);

    for (guint i = 0; i < 61; i++) {
        const AtmSeriesContractPoint *point =
            atm_energy_institute_verified_series_point (series, i);
        g_assert_nonnull (point);
        g_assert_cmpuint (point->support->len, ==, 5u);
        for (guint j = 0; j < 5; j++) {
            const char *expected = atm_series_evidence_point_support (
                atm_series_sra_evidence (sra), i, j);
            g_assert_nonnull (expected);
            g_assert_cmpstr (g_ptr_array_index (point->support, j), ==, expected);
        }
    }

    atm_series_sra_free (sra);
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
}

/* The generic SRA descriptor is intentionally not an output-series approval.
 * Only the dedicated source-bound constructor carries the reviewed series
 * profile; caller metadata alone cannot unlock the base descriptor. */
static void
test_base_evidence_descriptor_still_fails_closed (void)
{
    AtmEnergyInstituteAdmission *admission = build_admission ();
    const AtmSeriesAdapterMetadata metadata = {
        .series_semantics = "empirical_annual_observations",
        .scenario = "not_applicable",
        .time_scope = "1965-2025",
        .profile_id = ATM_EI_VERIFIED_SERIES_PROFILE,
        .profile_version = "1",
    };
    AtmSeriesContract *contract = NULL;
    GError *error = NULL;
    g_assert_false (atm_series_adapter_admit (&ATM_EI_EVIDENCE_DESCRIPTOR,
        &metadata, admission, &contract, &error));
    g_assert_error (error, ATM_SERIES_CONTRACT_ERROR,
                    ATM_SERIES_CONTRACT_ERROR_SHAPE);
    g_assert_cmpstr (error->message, ==,
        "Source descriptor has no reviewed generic series profile.");
    g_assert_null (contract);
    g_clear_error (&error);
    atm_energy_institute_admission_free (admission);
}

static void
test_material_mutations_fail_reconstruction (void)
{
    AtmEnergyInstituteVerifiedSeries *series = build_series ();
    AtmSeriesContract *contract = (AtmSeriesContract *)
        atm_energy_institute_verified_series_contract (series);
    AtmSeriesContractPoint *point =
        (AtmSeriesContractPoint *) atm_energy_institute_verified_series_point (
            series, 0);

    char *original_x = point->x;
    point->x = g_strdup ("1966");
    g_assert_false (atm_energy_institute_verified_series_validate (series, NULL));
    g_free (point->x);
    point->x = original_x;
    g_assert_true (atm_energy_institute_verified_series_validate (series, NULL));

    char *original_decimal = point->source_decimal;
    point->source_decimal = g_strdup ("149.6960973");
    g_assert_false (atm_energy_institute_verified_series_validate (series, NULL));
    g_free (point->source_decimal);
    point->source_decimal = original_decimal;
    g_assert_true (atm_energy_institute_verified_series_validate (series, NULL));

    char *original_profile = contract->profile_id;
    contract->profile_id = g_strdup ("atm-series/unreviewed/1");
    assert_rejected_then_restored (series);
    g_free (contract->profile_id);
    contract->profile_id = original_profile;

    char *qualified_id = (char *) atm_series_contract_qualified_id (contract);
    char original_char = qualified_id[0];
    qualified_id[0] = original_char == 'a' ? 'b' : 'a';
    g_assert_false (atm_energy_institute_verified_series_validate (series, NULL));
    qualified_id[0] = original_char;

    char *support = g_ptr_array_index (point->support, 0);
    g_ptr_array_index (point->support, 0) =
        g_strdup ("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    g_assert_false (atm_energy_institute_verified_series_validate (series, NULL));
    g_free (g_ptr_array_index (point->support, 0));
    g_ptr_array_index (point->support, 0) = support;

    g_assert_true (atm_energy_institute_verified_series_validate (series, NULL));
    atm_energy_institute_verified_series_free (series);
}

static void
test_arguments (void)
{
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    g_assert_false (atm_energy_institute_verified_series_from_admission (
        NULL, &series, NULL));
    g_assert_null (series);
    g_assert_false (atm_energy_institute_verified_series_from_admission (
        NULL, NULL, NULL));
    g_assert_false (atm_energy_institute_verified_series_validate (NULL, NULL));
    g_assert_false (atm_energy_institute_verified_series_rebuild (NULL, NULL, NULL));
    g_assert_null (atm_energy_institute_verified_series_contract (NULL));
    g_assert_null (atm_energy_institute_verified_series_point (NULL, 0));
    g_assert_cmpuint (atm_energy_institute_verified_series_count (NULL), ==, 0u);
    g_assert_null (atm_energy_institute_verified_series_scientific_id (NULL));
    g_assert_null (atm_energy_institute_verified_series_qualified_id (NULL));

    AtmEnergyInstituteAdmission *admission = build_admission ();
    GError *error = NULL;
    g_assert_true (atm_energy_institute_verified_series_from_admission (
        admission, &series, &error));
    g_assert_no_error (error);
    AtmEnergyInstituteVerifiedSeries *saved = series;
    g_assert_false (atm_energy_institute_verified_series_from_admission (
        admission, &series, NULL));
    g_assert_true (series == saved);
    g_assert_null (atm_energy_institute_verified_series_point (series, 61));
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/energy-institute-verified-series/complete",
                     test_complete_series_and_reconstruction);
    g_test_add_func ("/energy-institute-verified-series/exact-support",
                     test_each_point_uses_exact_sra_support);
    g_test_add_func ("/energy-institute-verified-series/base-fails-closed",
                     test_base_evidence_descriptor_still_fails_closed);
    g_test_add_func ("/energy-institute-verified-series/mutation",
                     test_material_mutations_fail_reconstruction);
    g_test_add_func ("/energy-institute-verified-series/arguments",
                     test_arguments);
    return g_test_run ();
}
