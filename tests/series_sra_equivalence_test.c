/* Equivalence test for the generic SRA extraction.
 *
 * series_sra.c replaces gistemp_sra.c. The evidence-layer test already proves
 * the artifact collections are identical; this one proves the stronger claim,
 * that the finalized scientific and qualified result identities are identical
 * too. Those identities are what every downstream layer binds to, so a
 * divergence here would be invisible until a chart or a History reconstruction
 * refused to match.
 *
 * Both SRAs are built from the same admitted bundle, then compared on:
 *   - the finalized scientific content id and qualified artifact id
 *   - the qualification envelope: schema, numeric profile, canonical
 *     obligations, repository snapshots, evidence atom ids, control evidence
 *     ids, semantic profiles, operations and dependency lists
 *   - every established fact: identity, value, unit, dimension, subject,
 *     attribute, qualifiers and support order
 *   - the limitation set, verbatim
 *
 * Fixture resolution matches the existing chart targets, and the four source
 * digests are asserted against source-lock.json before any SRA is built.
 *
 * Run before gistemp_sra.c leaves the build.
 */

#include <glib.h>
#include <json-glib/json-glib.h>
#include <string.h>

#include "series_sra.h"
#include "series_evidence_adapters.h"
#include "gistemp_admission.h"
#include "gistemp_sra.h"
#include "energy_institute_admission.h"
#include "energy_institute_evidence.h"

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

static AtmEnergyInstituteAdmission *
build_energy_admission (void)
{
    GBytes *sources[ATM_EI_SOURCE_COUNT] = {
        load_fixture ("science/data/processed/energy_institute_global_2026.csv"),
        load_fixture ("science/data/processed/energy_institute_global_2026.provenance.json"),
        load_fixture ("science/data/input_manifest.json"),
        load_fixture ("science/data/registry.csv"),
    };
    AtmEnergyInstituteAdmission *admission = NULL;
    GError *error = NULL;
    g_assert_true (atm_energy_institute_admission_new (ATM_TEST_REPOSITORY,
        ATM_TEST_SNAPSHOT, sources, &admission, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) g_bytes_unref (sources[i]);
    return admission;
}

static void
assert_id_list_matches (const char *label,
                        const GPtrArray *before,
                        const GPtrArray *after)
{
    if (before == NULL || after == NULL)
        g_error ("%s: identity list is NULL", label);
    g_assert_cmpuint (before->len, ==, after->len);
    for (guint i = 0; i < before->len; i++) {
        if (g_strcmp0 (g_ptr_array_index (before, i),
                       g_ptr_array_index (after, i)) != 0)
            g_error ("%s: entry %u diverged\n  legacy : %s\n  generic: %s",
                     label, i, (const char *) g_ptr_array_index (before, i),
                     (const char *) g_ptr_array_index (after, i));
    }
}

static void
assert_envelopes_match (const AtmSraQualificationEnvelope *before,
                        const AtmSraQualificationEnvelope *after)
{
    g_assert_cmpstr (before->schema, ==, after->schema);
    g_assert_cmpstr (before->numeric_profile, ==, after->numeric_profile);
    g_assert_cmpstr (before->scientific_content_id, ==, after->scientific_content_id);
    g_assert_cmpstr (before->qualified_artifact_id, ==, after->qualified_artifact_id);

    assert_id_list_matches ("canonical obligations",
        before->canonical_obligations, after->canonical_obligations);
    assert_id_list_matches ("repository snapshots",
        before->repository_snapshots, after->repository_snapshots);
    assert_id_list_matches ("evidence atom ids",
        before->evidence_atom_ids, after->evidence_atom_ids);
    assert_id_list_matches ("control evidence ids",
        before->control_evidence_ids, after->control_evidence_ids);
    assert_id_list_matches ("semantic profiles",
        before->semantic_profiles, after->semantic_profiles);
    assert_id_list_matches ("operations",
        before->operations, after->operations);
    assert_id_list_matches ("temporal dependencies",
        before->temporal_dependencies, after->temporal_dependencies);
    assert_id_list_matches ("stochastic dependencies",
        before->stochastic_dependencies, after->stochastic_dependencies);
}

static void
assert_facts_match (const AtmSraResult *before, const AtmSraResult *after)
{
    g_assert_cmpuint (before->established_facts->len, ==,
                      after->established_facts->len);

    for (guint i = 0; i < before->established_facts->len; i++) {
        const AtmSraEstablishedFact *a = g_ptr_array_index (before->established_facts, i);
        const AtmSraEstablishedFact *b = g_ptr_array_index (after->established_facts, i);

        g_assert_cmpstr (a->fact_id, ==, b->fact_id);
        g_assert_cmpstr (a->fact_type, ==, b->fact_type);
        g_assert_cmpstr (a->subject_id, ==, b->subject_id);
        g_assert_cmpstr (a->attribute, ==, b->attribute);
        g_assert_cmpstr (a->value, ==, b->value);
        g_assert_cmpstr (a->unit, ==, b->unit);
        g_assert_cmpstr (a->dimension, ==, b->dimension);
        g_assert_cmpstr (a->qualifiers_json, ==, b->qualifiers_json);
        g_assert_cmpstr (a->qualifiers_content_id, ==, b->qualifiers_content_id);

        g_assert_cmpuint (a->support->len, ==, b->support->len);
        for (guint j = 0; j < a->support->len; j++) {
            g_assert_cmpstr (g_ptr_array_index (a->support, j), ==,
                             g_ptr_array_index (b->support, j));
        }
    }
}

static void
test_gistemp_sra_equivalence (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();

    AtmGistempSra *legacy = NULL;
    GError *error = NULL;
    g_assert_true (atm_gistemp_sra_new (admission, &legacy, &error));
    g_assert_no_error (error);
    g_assert_true (atm_gistemp_sra_validate (legacy, &error));
    g_assert_no_error (error);

    AtmSeriesSra *generic = NULL;
    g_assert_true (atm_series_sra_new (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                       admission, &generic, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_sra_validate (generic, &error));
    g_assert_no_error (error);

    const AtmSraResult *before = atm_gistemp_sra_result (legacy);
    const AtmSraResult *after = atm_series_sra_result (generic);

    g_assert_nonnull (before);
    g_assert_nonnull (after);
    g_assert_true (before->finalized);
    g_assert_true (after->finalized);
    g_assert_cmpint (before->answerability, ==, after->answerability);

    assert_envelopes_match (before->qualification, after->qualification);
    assert_facts_match (before, after);

    /* A source-bound extraction produces no derived facts, constraints or
     * conflicts; both must be empty, not merely equal. */
    g_assert_cmpuint (after->derived_facts->len, ==, 0);
    g_assert_cmpuint (after->constraint_results->len, ==, 0);
    g_assert_cmpuint (after->conflicts->len, ==, 0);

    assert_id_list_matches ("limitations", before->limitations, after->limitations);

    atm_series_sra_free (generic);
    atm_gistemp_sra_free (legacy);
    atm_gistemp_admission_free (admission);
}

static void
test_generic_sra_rejects_bad_descriptor (void)
{
    AtmSeriesEvidenceDescriptor broken = ATM_GISTEMP_EVIDENCE_DESCRIPTOR;
    broken.evidence_count = broken.evidence_count + 1u;

    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesSra *sentinel = (AtmSeriesSra *) (gpointer) 0x1;
    AtmSeriesSra *out = sentinel;
    GError *error = NULL;

    gboolean ok = atm_series_sra_new (&broken, admission, &out, &error);
    g_assert_false (ok);
    g_assert_nonnull (error);
    g_assert_true (out == sentinel);

    g_clear_error (&error);
    atm_gistemp_admission_free (admission);
}

static void
test_generic_sra_argument_guards (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    GError *error = NULL;

    AtmSeriesSra *out = NULL;
    g_assert_false (atm_series_sra_new (NULL, admission, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    g_assert_false (atm_series_sra_new (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                        NULL, &out, &error));
    g_assert_nonnull (error);
    g_assert_null (out);
    g_clear_error (&error);

    AtmSeriesSra *sentinel = (AtmSeriesSra *) (gpointer) 0x1;
    out = sentinel;
    g_assert_false (atm_series_sra_new (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                        admission, &out, &error));
    g_assert_true (out == sentinel);
    g_clear_error (&error);

    g_assert_false (atm_series_sra_validate (NULL, &error));
    g_clear_error (&error);
    g_assert_null (atm_series_sra_result (NULL));
    g_assert_null (atm_series_sra_evidence (NULL));
    atm_series_sra_free (NULL);

    atm_gistemp_admission_free (admission);
}

static void
test_generic_sra_validate_is_reconstruction_guard (void)
{
    AtmGistempAdmission *admission = build_gistemp_admission ();
    AtmSeriesSra *sra = NULL;
    GError *error = NULL;

    g_assert_true (atm_series_sra_new (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                       admission, &sra, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_sra_validate (sra, &error));
    g_assert_no_error (error);

    const AtmSraResult *r = atm_series_sra_result (sra);
    g_assert_nonnull (r);
    g_assert_true (r->finalized);
    g_assert_nonnull (atm_series_sra_evidence (sra));

    atm_series_sra_free (sra);
    atm_gistemp_admission_free (admission);
}

static gboolean
string_array_contains (const GPtrArray *array, const char *value)
{
    if (array == NULL || value == NULL) return FALSE;
    for (guint i = 0; i < array->len; i++) {
        if (g_strcmp0 (g_ptr_array_index (array, i), value) == 0)
            return TRUE;
    }
    return FALSE;
}

static gboolean
fact_supports_belong_to_evidence (const AtmSraEstablishedFact *fact,
                                  const AtmSeriesEvidence *evidence,
                                  guint point)
{
    if (fact == NULL || fact->support == NULL || evidence == NULL ||
        fact->support->len != ATM_EI_SOURCE_COUNT + 1u)
        return FALSE;

    /* The SRA must bind exactly the expected source set for this point, once
     * each. Merely finding all IDs somewhere in the evidence would let a
     * duplicated ID conceal an omitted support or support from another year. */
    gboolean matched[ATM_EI_SOURCE_COUNT + 1u] = { FALSE };
    for (guint i = 0; i < fact->support->len; i++) {
        const char *support_id = g_ptr_array_index (fact->support, i);
        gboolean found = FALSE;
        for (guint j = 0; j < ATM_EI_SOURCE_COUNT + 1u; j++) {
            const char *expected =
                atm_series_evidence_point_support (evidence, point, j);
            if (!matched[j] && expected != NULL &&
                g_strcmp0 (support_id, expected) == 0) {
                matched[j] = TRUE;
                found = TRUE;
                break;
            }
        }
        if (!found) return FALSE;
    }
    for (guint j = 0; j < ATM_EI_SOURCE_COUNT + 1u; j++)
        if (!matched[j]) return FALSE;
    return TRUE;
}

static void
test_energy_sra_qualifies_approved_snapshot (void)
{
    AtmEnergyInstituteAdmission *admission = build_energy_admission ();
    AtmSeriesEvidence *evidence = NULL;
    AtmSeriesSra *sra = NULL;
    GError *error = NULL;

    g_assert_cmpstr (ATM_EI_EVIDENCE_DESCRIPTOR.canonical_obligation, ==,
        "atm-energy-institute-ewd-tes-pinned-snapshot-reconstruction/1");

    /* Evidence remains reconstructable byte-for-byte under the approved scope. */
    g_assert_true (atm_series_evidence_new (&ATM_EI_EVIDENCE_DESCRIPTOR,
        admission, &evidence, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_evidence_validate (evidence, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (atm_series_evidence_count (evidence), ==, 65u);
    g_assert_cmpstr (atm_series_evidence_point_source_decimal (evidence, 0), ==,
                     "149.6960972");
    g_assert_cmpstr (atm_series_evidence_point_source_decimal (evidence, 60), ==,
                     "600.3129128");

    g_assert_true (atm_series_sra_new (&ATM_EI_EVIDENCE_DESCRIPTOR,
        admission, &sra, &error));
    g_assert_no_error (error);
    g_assert_true (atm_series_sra_validate (sra, &error));
    g_assert_no_error (error);

    const AtmSraResult *result = atm_series_sra_result (sra);
    g_assert_nonnull (result);
    g_assert_true (result->finalized);
    g_assert_cmpint (result->answerability, ==, ATM_SRA_ANSWERED);
    g_assert_cmpstr (result->qualification->schema, ==, ATM_SRA_SCHEMA_ID);
    g_assert_cmpstr (result->qualification->numeric_profile, ==,
                     "exact_decimal_coefficient_exponent/1");

    const AtmSraQualificationEnvelope *q = result->qualification;
    g_assert_cmpuint (q->canonical_obligations->len, ==, 2u);
    g_assert_true (string_array_contains (q->canonical_obligations, ATM_SRA_SCHEMA_ID));
    g_assert_true (string_array_contains (q->canonical_obligations,
        "atm-energy-institute-ewd-tes-pinned-snapshot-reconstruction/1"));
    g_assert_cmpuint (q->repository_snapshots->len, ==, 1u);
    g_assert_cmpstr (g_ptr_array_index (q->repository_snapshots, 0), ==,
        "ewd@snapshot:d9e249339663015f6d1c05752338a955bf64ad0b#d9e249339663015f6d1c05752338a955bf64ad0b");
    g_assert_cmpuint (q->evidence_atom_ids->len, ==, 65u);
    g_assert_cmpuint (q->semantic_profiles->len, ==, 1u);
    g_assert_cmpstr (g_ptr_array_index (q->semantic_profiles, 0), ==,
        "atm-profile/ewd-energy-institute-tes-physical-content/v2@2");

    g_assert_cmpuint (result->established_facts->len, ==, 61u);
    g_assert_cmpuint (result->derived_facts->len, ==, 0u);
    g_assert_cmpuint (result->constraint_results->len, ==, 0u);
    g_assert_cmpuint (result->conflicts->len, ==, 0u);

    for (guint i = 0; i < result->established_facts->len; i++) {
        const AtmSraEstablishedFact *fact =
            g_ptr_array_index (result->established_facts, i);
        g_assert_cmpstr (fact->subject_id, ==, "global_total_energy_supply");
        g_assert_cmpstr (fact->attribute, ==, "total_energy_supply");
        g_assert_cmpstr (fact->unit, ==, "exajoules/year");
        g_assert_cmpstr (fact->dimension, ==, "energy_flow_rate");
        g_assert_true (fact_supports_belong_to_evidence (fact, evidence, i));
        g_assert_nonnull (strstr (fact->qualifiers_json, "physical_energy_content"));
    }
    const AtmSraEstablishedFact *first =
        g_ptr_array_index (result->established_facts, 0);
    const AtmSraEstablishedFact *last =
        g_ptr_array_index (result->established_facts, 60);
    g_assert_cmpstr (first->value, ==, "1496960972e-7");
    g_assert_cmpstr (last->value, ==, "6003129128e-7");

    g_assert_true (string_array_contains (result->limitations,
        "Approved semantic precedence: the pinned tes_ej mapping and EI 2026 methodology define the metric; legacy EWD primary-energy labels remain provenance only."));
    g_assert_true (string_array_contains (result->limitations,
        "The 2019-2023 component reconciliation residual is retained as provenance, not repaired or redistributed."));
    g_assert_true (string_array_contains (result->limitations,
        "Pinned processed observations; the upstream Energy Institute calculation is not reproduced by AtM."));

    /* The finalized result cannot be edited and then validated as if unchanged. */
    AtmSraResult *mutable_result = (AtmSraResult *) result;
    AtmSraEstablishedFact *mutable_first =
        g_ptr_array_index (mutable_result->established_facts, 0);
    char *original_unit = mutable_first->unit;
    mutable_first->unit = g_strdup ("wrong-unit");
    g_assert_false (atm_series_sra_validate (sra, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_free (mutable_first->unit);
    mutable_first->unit = original_unit;
    g_assert_true (atm_series_sra_validate (sra, &error));
    g_assert_no_error (error);

    atm_series_sra_free (sra);
    atm_series_evidence_free (evidence);
    atm_energy_institute_admission_free (admission);
}

static void
test_energy_sra_fails_closed_without_approved_obligation (void)
{
    AtmEnergyInstituteAdmission *admission = build_energy_admission ();
    AtmSeriesEvidenceDescriptor unapproved = ATM_EI_EVIDENCE_DESCRIPTOR;
    unapproved.canonical_obligation = NULL;
    AtmSeriesEvidence *evidence = NULL;
    AtmSeriesSra *sra = NULL;
    GError *error = NULL;

    /* Even with source bytes that are valid, no descriptor-level obligation
     * means Evidence-only: SRA promotion must fail closed. */
    g_assert_true (atm_series_evidence_new (&unapproved, admission, &evidence, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (atm_series_evidence_count (evidence), ==, 65u);
    g_assert_false (atm_series_sra_new (&unapproved, admission, &sra, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_nonnull (g_strstr_len (error->message, -1,
                                    "canonical reconstruction obligation is approved"));
    g_assert_null (sra);
    g_clear_error (&error);

    atm_series_evidence_free (evidence);
    atm_energy_institute_admission_free (admission);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    if (g_getenv ("ATM_CHART01_FIXTURE") == NULL)
        g_error ("ATM_CHART01_FIXTURE must point at tests/fixtures/chart01.");

    assert_matches_source_lock ();

    g_test_add_func ("/sra/gistemp-equivalence", test_gistemp_sra_equivalence);
    g_test_add_func ("/sra/rejects-bad-descriptor", test_generic_sra_rejects_bad_descriptor);
    g_test_add_func ("/sra/argument-guards", test_generic_sra_argument_guards);
    g_test_add_func ("/sra/validate-reconstruction", test_generic_sra_validate_is_reconstruction_guard);
    g_test_add_func ("/sra/ei-approved-snapshot-qualification",
                     test_energy_sra_qualifies_approved_snapshot);
    g_test_add_func ("/sra/ei-fails-closed-without-obligation",
                     test_energy_sra_fails_closed_without_approved_obligation);

    return g_test_run ();
}
