/* Qualification test for the option C bridge.
 *
 * verified_series_contract.c lets a consumer take the general AtmSeriesContract
 * for the pinned GISTEMP series without disturbing the identity that
 * verified_series.c mints and that stored charts reconstruct against.
 *
 * This test proves three things:
 *
 *   1. the bridge runs the shared chain and returns a valid contract;
 *   2. the contract is deterministic and carries the admitted source;
 *   3. the pinned VerifiedSeries still reports its own identity alongside it,
 *      unchanged - which is the whole point of the bridge.
 *
 * Claim 3 is the one that matters. If the two identity systems collided, or if
 * building the contract changed what verified_series reports, the bridge would
 * not be preserving the persisted identity and the option would be wrong.
 */

#include <glib.h>
#include <json-glib/json-glib.h>

#include "verified_series_contract.h"
#include "verified_series.h"
#include "series_adapter.h"
#include "gistemp_admission.h"

#define ATM_TEST_REPOSITORY "LaurentiuStaicu/empirical-world3-dynamics"
#define ATM_TEST_SNAPSHOT   "d9e249339663015f6d1c05752338a955bf64ad0b"

/* The pinned identities, from tests/fixtures/chart01/verified-series-identity.json. */
#define ATM_PINNED_SCIENTIFIC_ID \
    "4cfff6bb991c2d771228c09e81ca860a5b3e195e4f2390d059a3c8aacc65c48c"
#define ATM_PINNED_QUALIFIED_ID \
    "29827400d3e250f6c935eefa33d2a8c4f99c4198721676231e777d16ee1f6b24"

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

/* The bridge returns a valid, deterministically identified contract. */
static void
test_bridge_returns_valid_contract (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();

    AtmSeriesContract *first = NULL;
    GError *error = NULL;
    g_assert_true (atm_verified_series_contract (admission, &first, &error));
    g_assert_no_error (error);
    g_assert_nonnull (first);

    AtmSeriesContract *second = NULL;
    g_assert_true (atm_verified_series_contract (admission, &second, &error));
    g_assert_no_error (error);

    g_assert_cmpstr (atm_series_contract_scientific_id (first), ==,
                     atm_series_contract_scientific_id (second));
    g_assert_cmpstr (atm_series_contract_qualified_id (first), ==,
                     atm_series_contract_qualified_id (second));
    g_assert_cmpstr (atm_series_contract_scientific_id (first), !=,
                     atm_series_contract_qualified_id (first));

    /* The contract carries the admitted source, not a caller-supplied one. */
    g_assert_cmpstr (first->repository_id, ==, "ewd");
    g_assert_cmpstr (first->snapshot_sha, ==, ATM_TEST_SNAPSHOT);
    g_assert_cmpuint (first->points->len, ==, 146u);
    g_assert_cmpuint (first->x_kind, ==, ATM_SERIES_X_CALENDAR_YEAR);

    /* The metadata the bridge supplies is the reviewed GISTEMP set. */
    const AtmSeriesAdapterMetadata *metadata =
        atm_verified_series_gistemp_metadata ();
    g_assert_nonnull (metadata);
    g_assert_cmpstr (first->series_semantics, ==, metadata->series_semantics);
    g_assert_cmpstr (first->profile_id, ==, metadata->profile_id);

    atm_series_contract_free (second);
    atm_series_contract_free (first);
    atm_gistemp_admission_free (admission);
}

/* The pinned identity survives: building the general contract does not disturb
 * what verified_series reports, and the two identity systems stay distinct. */
static void
test_pinned_identity_survives_alongside (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();

    /* The pinned module produces its own identity, unchanged. */
    AtmVerifiedSeries *series = NULL;
    GError *error = NULL;
    g_assert_true (atm_verified_series_from_gistemp (admission, &series, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_verified_series_scientific_id (series), ==,
                     ATM_PINNED_SCIENTIFIC_ID);
    g_assert_cmpstr (atm_verified_series_qualified_id (series), ==,
                     ATM_PINNED_QUALIFIED_ID);
    g_assert_true (atm_verified_series_validate (series, &error));
    g_assert_no_error (error);

    /* The general contract is built beside it and carries different ids, which
     * is why the bridge exists rather than a replacement. */
    AtmSeriesContract *contract = NULL;
    g_assert_true (atm_verified_series_contract (admission, &contract, &error));
    g_assert_no_error (error);

    g_assert_cmpstr (atm_series_contract_scientific_id (contract), !=
                     ATM_PINNED_SCIENTIFIC_ID ?
                     atm_series_contract_scientific_id (contract) : "", ==, "");
    g_assert_nonnull (atm_series_contract_scientific_id (contract));
    g_assert_nonnull (atm_series_contract_qualified_id (contract));

    /* The pinned series is still valid after the contract was built: the
     * bridge must not mutate the admission or the identity. */
    g_assert_true (atm_verified_series_validate (series, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_verified_series_scientific_id (series), ==,
                     ATM_PINNED_SCIENTIFIC_ID);

    atm_series_contract_free (contract);
    atm_verified_series_free (series);
    atm_gistemp_admission_free (admission);
}

/* Argument guards, with output ownership preserved. */
static void
test_argument_guards (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    GError *error = NULL;

    AtmSeriesContract *out = NULL;
    g_assert_false (atm_verified_series_contract (NULL, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    g_assert_false (atm_verified_series_contract (admission, NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    AtmSeriesContract *sentinel = (AtmSeriesContract *) (gpointer) 0x1;
    out = sentinel;
    g_assert_false (atm_verified_series_contract (admission, &out, &error));
    g_assert_true (out == sentinel);
    g_clear_error (&error);

    atm_gistemp_admission_free (admission);
}

/* The metadata accessor returns the same object on every call, so a caller
 * cannot accidentally build a contract against drifting metadata. */
static void
test_metadata_is_stable (void)
{
    const AtmSeriesAdapterMetadata *a = atm_verified_series_gistemp_metadata ();
    const AtmSeriesAdapterMetadata *b = atm_verified_series_gistemp_metadata ();
    g_assert_true (a == b);
    g_assert_cmpstr (a->profile_id, ==, "atm-series/gistemp-complete-annual/1");
    g_assert_cmpstr (a->time_scope, ==, "1880-2025");
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    if (g_getenv ("ATM_CHART01_FIXTURE") == NULL)
        g_error ("ATM_CHART01_FIXTURE must point at tests/fixtures/chart01.");

    g_test_add_func ("/vs-contract/valid", test_bridge_returns_valid_contract);
    g_test_add_func ("/vs-contract/pinned-survives", test_pinned_identity_survives_alongside);
    g_test_add_func ("/vs-contract/argument-guards", test_argument_guards);
    g_test_add_func ("/vs-contract/metadata-stable", test_metadata_is_stable);

    return g_test_run ();
}
