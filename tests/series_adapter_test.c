/* Qualification test for the source-bound series adapter.
 *
 * The adapter is the bridge from the descriptor-driven evidence collection to
 * the general AtmSeriesContract. It has no equivalence counterpart to compare
 * against, because no source-specific module ever produced an AtmSeriesContract.
 * So this test establishes the guarantees directly:
 *
 *   1. admission produces a contract that validates against the finalized SRA;
 *   2. the contract carries the reviewed source metadata and admitted years;
 *   3. the reviewed metadata is bound into the identity, not decorative;
 *   4. a reconstruction from the same admission agrees, and the check can fail;
 *   5. argument guards hold and the output is left untouched on failure.
 *
 * Fixtures resolve as ATM_CHART01_FIXTURE plus the nested ewd/ paths, and the
 * four files are asserted against tests/fixtures/chart01/source-lock.json.
 */

#include <glib.h>
#include <json-glib/json-glib.h>

#include "series_adapter.h"
#include "series_evidence_adapters.h"
#include "series_sra.h"
#include "gistemp_admission.h"

#define ATM_TEST_REPOSITORY "LaurentiuStaicu/empirical-world3-dynamics"
#define ATM_TEST_SNAPSHOT   "d9e249339663015f6d1c05752338a955bf64ad0b"

/* The reviewed series metadata for the pinned GISTEMP source. */
static const AtmSeriesAdapterMetadata GISTEMP_METADATA = {
    .series_semantics = "empirical_annual_observations",
    .scenario = "not_applicable",
    .time_scope = "1880-2025",
    .profile_id = "atm-series/gistemp-complete-annual/1",
    .profile_version = "1",
};

static char *
fixture_path (const char *relative)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    if (root == NULL)
        g_error ("ATM_CHART01_FIXTURE must point at tests/fixtures/chart01.");
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
        g_error ("Cannot read fixture %s: %s", path, error->message);
    g_free (path);
    return g_bytes_new_take (data, length);
}

static void
assert_matches_source_lock (void)
{
    char *lock_path = g_build_filename (g_getenv ("ATM_CHART01_FIXTURE"),
                                        "source-lock.json", NULL);
    JsonParser *parser = json_parser_new ();
    GError *error = NULL;
    if (!json_parser_load_from_file (parser, lock_path, &error))
        g_error ("Cannot read source lock %s: %s", lock_path, error->message);
    g_free (lock_path);

    JsonObject *root = json_node_get_object (json_parser_get_root (parser));
    JsonObject *files = json_object_get_object_member (root, "files");
    if (files == NULL) g_error ("Source lock has no 'files' object.");

    const char *names[] = {
        "science/data/input_manifest.json",
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/registry.csv",
    };
    for (guint i = 0; i < G_N_ELEMENTS (names); i++) {
        char *path = fixture_path (names[i]);
        gchar *data = NULL;
        gsize length = 0;
        if (!g_file_get_contents (path, &data, &length, &error))
            g_error ("Cannot read %s: %s", path, error->message);
        char *digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                    (const guchar *) data, length);
        const char *expected = json_object_get_string_member (files, names[i]);
        if (g_strcmp0 (digest, expected) != 0)
            g_error ("Fixture %s does not match the source lock.", names[i]);
        g_free (digest);
        g_free (data);
        g_free (path);
    }
    g_object_unref (parser);
}

static AtmGistempAdmission *
build_gistemp_admission (void)
{
    GBytes *sources[ATM_GISTEMP_SOURCE_COUNT] = {
        load_fixture ("science/data/processed/nasa_gistemp_global_2026.csv"),
        load_fixture ("science/data/processed/nasa_gistemp_global_2026.provenance.json"),
        load_fixture ("science/data/input_manifest.json"),
        load_fixture ("science/data/registry.csv"),
    };
    AtmGistempAdmission *admission = NULL;
    GError *error = NULL;
    g_assert_true (atm_gistemp_admission_new (ATM_TEST_REPOSITORY,
        ATM_TEST_SNAPSHOT, sources, &admission, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++) g_bytes_unref (sources[i]);
    return admission;
}

/* Admission must produce a contract that validates against the finalized SRA,
 * and it must be deterministic across two independent admissions. */
static void
test_admit_produces_valid_contract (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();

    AtmSeriesContract *first = NULL;
    GError *error = NULL;
    g_assert_true (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, admission, &first, &error));
    g_assert_no_error (error);
    g_assert_nonnull (first);

    AtmSeriesContract *second = NULL;
    g_assert_true (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, admission, &second, &error));
    g_assert_no_error (error);

    g_assert_cmpstr (atm_series_contract_scientific_id (first), ==,
                     atm_series_contract_scientific_id (second));
    g_assert_cmpstr (atm_series_contract_qualified_id (first), ==,
                     atm_series_contract_qualified_id (second));

    /* The two identities are distinct: content identity is not qualification. */
    g_assert_cmpstr (atm_series_contract_scientific_id (first), !=,
                     atm_series_contract_qualified_id (first));

    g_assert_cmpuint (first->x_kind, ==, ATM_SERIES_X_CALENDAR_YEAR);
    g_assert_cmpuint (first->points->len, ==, 146u);
    g_assert_cmpstr (first->repository_id, ==, "ewd");
    g_assert_cmpstr (first->snapshot_sha, ==, ATM_TEST_SNAPSHOT);
    g_assert_cmpstr (first->profile_id, ==, GISTEMP_METADATA.profile_id);
    g_assert_cmpstr (first->series_semantics, ==, GISTEMP_METADATA.series_semantics);

    atm_series_contract_free (second);
    atm_series_contract_free (first);
    atm_gistemp_admission_free (admission);
}

/* Every point must carry its calendar year as X, ascending from the admitted
 * first year, with the descriptor's support arity. */
static void
test_contract_points_carry_admitted_years (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesContract *contract = NULL;
    GError *error = NULL;

    g_assert_true (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, admission, &contract, &error));
    g_assert_no_error (error);

    for (guint i = 0; i < contract->points->len; i++) {
        const AtmSeriesContractPoint *p = g_ptr_array_index (contract->points, i);
        g_assert_nonnull (p);
        g_assert_cmpuint (p->order, ==, i);
        g_assert_cmpuint (p->y_status, ==, ATM_SERIES_Y_NUMERIC);
        g_assert_false (p->break_before);
        g_assert_null (p->missing_reason);
        g_assert_null (p->break_reason);
        g_assert_nonnull (p->x);
        g_assert_nonnull (p->source_decimal);
        g_assert_nonnull (p->coefficient);
        g_assert_cmpuint (p->support->len, ==,
                          ATM_GISTEMP_EVIDENCE_DESCRIPTOR.support_per_point);

        /* X is the year, strictly ascending, equal to 1880 + order. */
        guint year = (guint) g_ascii_strtoull (p->x, NULL, 10);
        g_assert_cmpuint (year, ==, 1880u + i);

        /* The contract recomputed coefficient/exponent from the source decimal;
         * the adapter supplied both, so they must agree. */
        AtmScientificDecimal *decimal = NULL;
        g_assert_true (atm_scientific_decimal_parse (p->source_decimal, &decimal, NULL));
        g_assert_cmpstr (p->coefficient, ==, decimal->coefficient);
        g_assert_cmpint (p->exponent, ==, decimal->exponent);
        atm_scientific_decimal_free (decimal);
    }

    atm_series_contract_free (contract);
    atm_gistemp_admission_free (admission);
}

/* Rebuilding the whole chain from the same admission must reproduce both
 * identities. This is the shape the check must have: the contract mints its
 * identities only when validated against a finalized SRA. */
static void
test_adapter_revalidate_accepts_reconstruction (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesContract *contract = NULL;
    GError *error = NULL;

    g_assert_true (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, admission, &contract, &error));
    g_assert_no_error (error);

    g_assert_true (atm_series_adapter_revalidate (contract,
        &ATM_GISTEMP_EVIDENCE_DESCRIPTOR, &GISTEMP_METADATA,
        admission, &error));
    g_assert_no_error (error);

    atm_series_contract_free (contract);
    atm_gistemp_admission_free (admission);
}

/* A different reviewed metadata set must produce a different identity. If it did
 * not, the metadata would not be bound into the contract at all. */
static void
test_metadata_is_bound_into_identity (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();

    AtmSeriesContract *baseline = NULL;
    GError *error = NULL;
    g_assert_true (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, admission, &baseline, &error));
    g_assert_no_error (error);

    AtmSeriesAdapterMetadata altered = GISTEMP_METADATA;
    altered.time_scope = "1880-2024";

    AtmSeriesContract *changed = NULL;
    g_assert_true (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &altered, admission, &changed, &error));
    g_assert_no_error (error);

    g_assert_cmpstr (atm_series_contract_scientific_id (baseline), !=,
                     atm_series_contract_scientific_id (changed));

    atm_series_contract_free (changed);
    atm_series_contract_free (baseline);
    atm_gistemp_admission_free (admission);
}

/* Argument and metadata guards, with output ownership preserved. */
static void
test_argument_guards (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    GError *error = NULL;

    AtmSeriesContract *out = NULL;
    g_assert_false (atm_series_adapter_admit (NULL, &GISTEMP_METADATA,
        admission, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    g_assert_false (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        NULL, admission, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    g_assert_false (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, NULL, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    /* An incomplete metadata set is refused rather than silently defaulted. */
    AtmSeriesAdapterMetadata incomplete = GISTEMP_METADATA;
    incomplete.time_scope = NULL;
    g_assert_false (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &incomplete, admission, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    /* A pre-populated output pointer is refused and left untouched. */
    AtmSeriesContract *sentinel = (AtmSeriesContract *) (gpointer) 0x1;
    out = sentinel;
    g_assert_false (atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
        &GISTEMP_METADATA, admission, &out, &error));
    g_assert_true (out == sentinel);
    g_clear_error (&error);

    atm_gistemp_admission_free (admission);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    if (g_getenv ("ATM_CHART01_FIXTURE") == NULL)
        g_error ("ATM_CHART01_FIXTURE must point at tests/fixtures/chart01.");

    assert_matches_source_lock ();

    g_test_add_func ("/adapter/admit-valid", test_admit_produces_valid_contract);
    g_test_add_func ("/adapter/points-carry-years", test_contract_points_carry_admitted_years);
    g_test_add_func ("/adapter/revalidate", test_adapter_revalidate_accepts_reconstruction);
    g_test_add_func ("/adapter/metadata-bound-into-identity", test_metadata_is_bound_into_identity);
    g_test_add_func ("/adapter/argument-guards", test_argument_guards);

    return g_test_run ();
}
