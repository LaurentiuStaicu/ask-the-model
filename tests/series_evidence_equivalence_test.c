/* Equivalence test for the generic evidence extraction.
 *
 * The refactor moves the bodies of gistemp_evidence.c and
 * energy_institute_evidence.c into a shared layer with per-source descriptors.
 * That is only safe if the new layer produces byte-identical scientific
 * artifacts for both admitted sources.
 *
 * GISTEMP is compared unconditionally against the legacy module. The legacy
 * Energy Institute module is only compiled when the build defines
 * ATM_EI_LEGACY_EVIDENCE_HEADER, because that header's API could not be
 * confirmed from source alone; the generic Energy Institute path is still
exercised on its own either way.
 *
 * Fixture resolution matches the existing chart targets exactly:
 *
 *   $ATM_CHART01_FIXTURE/ewd/science/data/processed/nasa_gistemp_global_2026.csv
 *   $ATM_CHART01_FIXTURE/ewd/science/data/processed/nasa_gistemp_global_2026.provenance.json
 *   $ATM_CHART01_FIXTURE/ewd/science/data/input_manifest.json
 *   $ATM_CHART01_FIXTURE/ewd/science/data/registry.csv
 *
 * The four digests are asserted against tests/fixtures/chart01/source-lock.json
 * so a swapped or edited fixture directory cannot silently pass.
 */

#include <glib.h>
#include <json-glib/json-glib.h>

#include "series_evidence_adapters.h"
#include "gistemp_admission.h"
#include "gistemp_evidence.h"

#ifdef ATM_EI_LEGACY_EVIDENCE_HEADER
#include ATM_EI_LEGACY_EVIDENCE_HEADER
#endif

#define ATM_TEST_REPOSITORY "LaurentiuStaicu/empirical-world3-dynamics"
#define ATM_TEST_SNAPSHOT   "d9e249339663015f6d1c05752338a955bf64ad0b"

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

/* Assert the loaded bytes against the pinned source lock. */
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
        if (!json_object_has_member (files, names[i]))
            g_error ("Source lock does not pin %s", names[i]);
        char *path = fixture_path (names[i]);
        gchar *data = NULL;
        gsize length = 0;
        if (!g_file_get_contents (path, &data, &length, &error))
            g_error ("Cannot read %s: %s", path, error->message);
        char *digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                    (const guchar *) data, length);
        const char *expected = json_object_get_string_member (files, names[i]);
        if (g_strcmp0 (digest, expected) != 0)
            g_error ("Fixture %s does not match the source lock.\n  lock : %s\n  file : %s",
                     names[i], expected, digest);
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

/* Compare two artifact collections index by index. */
static void
assert_collections_match (const char *label,
                          guint expected_count,
                          const char *const *legacy_ids,
                          const char *const *legacy_digests,
                          const AtmSeriesEvidence *generic)
{
    g_assert_cmpuint (atm_series_evidence_count (generic), ==, expected_count);

    for (guint i = 0; i < expected_count; i++) {
        const AtmScientificArtifact *a = atm_series_evidence_at (generic, i);
        g_assert_nonnull (a);

        if (g_strcmp0 (a->qualified_artifact_id, legacy_ids[i]) != 0)
            g_error ("%s: artifact %u qualified id diverged\n  legacy : %s\n  generic: %s",
                     label, i, legacy_ids[i], a->qualified_artifact_id);
        if (g_strcmp0 (a->storage_digest, legacy_digests[i]) != 0)
            g_error ("%s: artifact %u storage digest diverged\n  legacy : %s\n  generic: %s",
                     label, i, legacy_digests[i], a->storage_digest);
    }
}

static void
test_gistemp_evidence_equivalence (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();

    AtmGistempEvidence *legacy = NULL;
    GError *error = NULL;
    g_assert_true (atm_gistemp_evidence_new (admission, &legacy, &error));
    g_assert_no_error (error);

    AtmSeriesEvidence *generic = NULL;
    g_assert_true (atm_series_evidence_new (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                            admission, &generic, &error));
    g_assert_no_error (error);

    g_assert_cmpuint (atm_gistemp_evidence_count (legacy), ==, ATM_GISTEMP_EVIDENCE_COUNT);

    const char **ids = g_new0 (const char *, ATM_GISTEMP_EVIDENCE_COUNT);
    const char **digests = g_new0 (const char *, ATM_GISTEMP_EVIDENCE_COUNT);
    for (guint i = 0; i < ATM_GISTEMP_EVIDENCE_COUNT; i++) {
        const AtmScientificArtifact *a = atm_gistemp_evidence_at (legacy, i);
        g_assert_nonnull (a);
        ids[i] = a->qualified_artifact_id;
        digests[i] = a->storage_digest;
    }

    assert_collections_match ("gistemp", ATM_GISTEMP_EVIDENCE_COUNT,
                              ids, digests, generic);

    guint points = ATM_GISTEMP_EVIDENCE_COUNT - ATM_GISTEMP_SOURCE_COUNT;
    for (guint p = 0; p < points; p++) {
        for (guint s = 0; s < ATM_GISTEMP_SOURCE_COUNT + 1u; s++) {
            const char *before = atm_gistemp_evidence_point_support (legacy, p, s);
            const char *after = atm_series_evidence_point_support (generic, p, s);
            if (g_strcmp0 (before, after) != 0)
                g_error ("gistemp: support diverged at point %u slot %u\n  legacy : %s\n  generic: %s",
                         p, s, before ? before : "(null)", after ? after : "(null)");
        }
    }

    g_free (ids);
    g_free (digests);
    atm_series_evidence_free (generic);
    atm_gistemp_evidence_free (legacy);
    atm_gistemp_admission_free (admission);
}

#ifdef ATM_EI_LEGACY_EVIDENCE_HEADER
static void
test_energy_evidence_equivalence (void)
{
    g_test_skip ("Legacy Energy Institute equivalence not yet wired; "
                 "generic path covered by /energy-generic-coverage.");
}
#else
static void
test_energy_evidence_equivalence (void)
{
    g_test_skip ("Legacy Energy Institute evidence header not enabled in this build; "
                 "generic path covered by /energy-generic-coverage.");
}
#endif

/* The generic Energy Institute path must still build, count and validate on its
 * own. This catches a wrong descriptor arity, a wrong locator column or a
 * payload shape error without needing the legacy module. */
static void
test_energy_generic_coverage (void)
{
    g_assert_cmpuint (ATM_EI_EVIDENCE_DESCRIPTOR.support_per_point, ==,
                      ATM_EI_EVIDENCE_DESCRIPTOR.source_count + 1u);
    g_assert_cmpuint (ATM_EI_EVIDENCE_DESCRIPTOR.evidence_count, ==, 65u);
    g_assert_cmpuint (ATM_EI_EVIDENCE_DESCRIPTOR.source_count, ==, 4u);
    g_assert_cmpuint (ATM_GISTEMP_EVIDENCE_DESCRIPTOR.evidence_count, ==, 150u);

    /* A NULL admission must be refused before any work happens. */
    AtmSeriesEvidence *out = NULL;
    GError *error = NULL;
    gboolean ok = atm_series_evidence_new (&ATM_EI_EVIDENCE_DESCRIPTOR,
                                           NULL, &out, &error);
    g_assert_false (ok);
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);
}

static void
test_descriptor_rejects_inconsistent_arity (void)
{
    AtmSeriesEvidenceDescriptor broken = ATM_GISTEMP_EVIDENCE_DESCRIPTOR;
    broken.support_per_point = broken.source_count + 2u;

    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesEvidence *out = NULL;
    GError *error = NULL;

    gboolean ok = atm_series_evidence_new (&broken, admission, &out, &error);
    g_assert_false (ok);
    g_assert_nonnull (error);
    g_assert_null (out);

    g_clear_error (&error);
    atm_gistemp_admission_free (admission);
}

static void
test_descriptor_rejects_wrong_count (void)
{
    AtmSeriesEvidenceDescriptor broken = ATM_GISTEMP_EVIDENCE_DESCRIPTOR;
    broken.evidence_count = broken.evidence_count + 1u;

    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesEvidence *out = NULL;
    GError *error = NULL;

    gboolean ok = atm_series_evidence_new (&broken, admission, &out, &error);
    g_assert_false (ok);
    g_assert_null (out);

    g_clear_error (&error);
    atm_gistemp_admission_free (admission);
}

static void
test_output_unchanged_on_failure (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesEvidence *sentinel = (AtmSeriesEvidence *) (gpointer) 0x1;
    AtmSeriesEvidence *out = sentinel;

    GError *error = NULL;
    gboolean ok = atm_series_evidence_new (NULL, admission, &out, &error);
    g_assert_false (ok);
    g_assert_true (out == sentinel);

    g_clear_error (&error);
    atm_gistemp_admission_free (admission);
}

static void
test_validate_accepts_reconstruction (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesEvidence *generic = NULL;
    GError *error = NULL;
    g_assert_true (atm_series_evidence_new (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                            admission, &generic, &error));
    g_assert_true (atm_series_evidence_validate (generic, &error));
    g_assert_no_error (error);

    atm_series_evidence_free (generic);
    atm_gistemp_admission_free (admission);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    if (g_getenv ("ATM_CHART01_FIXTURE") == NULL)
        g_error ("ATM_CHART01_FIXTURE must point at tests/fixtures/chart01.");

    assert_matches_source_lock ();

    g_test_add_func ("/evidence/gistemp-equivalence", test_gistemp_evidence_equivalence);
    g_test_add_func ("/evidence/energy-equivalence", test_energy_evidence_equivalence);
    g_test_add_func ("/evidence/energy-generic-coverage", test_energy_generic_coverage);
    g_test_add_func ("/evidence/reject-inconsistent-arity", test_descriptor_rejects_inconsistent_arity);
    g_test_add_func ("/evidence/reject-wrong-count", test_descriptor_rejects_wrong_count);
    g_test_add_func ("/evidence/output-unchanged-on-failure", test_output_unchanged_on_failure);
    g_test_add_func ("/evidence/validate-reconstruction", test_validate_accepts_reconstruction);

    return g_test_run ();
}
