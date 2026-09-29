#include "gistemp_evidence.h"
#include <json-glib/json-glib.h>

static AtmGistempEvidence *build (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json", "science/data/registry.csv"
    };
    GBytes *sources[4];
    char *buffers[4];
    g_assert_nonnull (root);
    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (root, "ewd", paths[i], NULL);
        gsize length;
        g_assert_true (g_file_get_contents (path, &buffers[i], &length, NULL));
        sources[i] = g_bytes_new_static (buffers[i], length);
        g_free (path);
    }
    AtmGistempAdmission *a = NULL;
    GError *error = NULL;
    g_assert_true (atm_gistemp_admission_new ("LaurentiuStaicu/empirical-world3-dynamics",
        "d9e249339663015f6d1c05752338a955bf64ad0b", sources, &a, &error));
    g_assert_no_error (error);
    AtmGistempEvidence *e = NULL;
    g_assert_true (atm_gistemp_evidence_new (a, &e, &error));
    g_assert_no_error (error);
    atm_gistemp_admission_free (a);
    for (guint i = 0; i < 4; i++) {
        buffers[i][0] ^= 1;
        g_bytes_unref (sources[i]);
        g_free (buffers[i]);
    }
    return e;
}
static void test_identity_support (void)
{
    AtmGistempEvidence *e = build ();
    AtmGistempEvidence *again = build ();
    GError *error = NULL;
    g_assert_true (atm_gistemp_evidence_validate (e, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (atm_gistemp_evidence_count (e), ==, 150);
    GHashTable *ids = g_hash_table_new (g_str_hash, g_str_equal);
    for (guint i = 0; i < 150; i++) {
        const AtmScientificArtifact *a = atm_gistemp_evidence_at (e, i);
        const AtmScientificArtifact *b = atm_gistemp_evidence_at (again, i);
        g_assert_true (atm_scientific_artifact_validate (a, &error));
        g_assert_no_error (error);
        g_assert_cmpstr (a->scientific_content_id, ==, b->scientific_content_id);
        g_assert_cmpstr (a->qualified_artifact_id, ==, b->qualified_artifact_id);
        g_assert_cmpstr (a->storage_digest, ==, b->storage_digest);
        g_assert_true (g_hash_table_add (ids, a->qualified_artifact_id));
        g_assert_cmpstr (a->profile_id, ==, ATM_GISTEMP_EVIDENCE_PROFILE);
    }
    for (guint point = 0; point < 146; point++)
        for (guint support = 0; support < 5; support++)
            g_assert_cmpstr (atm_gistemp_evidence_point_support (e, point, support), ==,
                atm_gistemp_evidence_at (e, support < 4 ? support : point + 4)->qualified_artifact_id);
    const AtmScientificArtifact *first = atm_gistemp_evidence_at (e, 4);
    JsonParser *parser = json_parser_new ();
    g_assert_true (json_parser_load_from_data (parser, first->payload, -1, NULL));
    JsonObject *object = json_node_get_object (json_parser_get_root (parser));
    g_assert_cmpstr (json_object_get_string_member (object, "coefficient"), ==, "-17");
    g_assert_cmpstr (json_object_get_string_member (object, "exponent"), ==, "-2");
    g_assert_cmpstr (json_object_get_string_member (object, "calendar_year"), ==, "1880");
    g_assert_cmpstr (json_object_get_string_member (object, "reference_period"), ==, "1951-1980");
    g_assert_cmpstr (first->locator, ==, "csv:row:2:columns:year,temperature_anomaly_c_1951_1980");
    g_assert_cmpstr (atm_gistemp_evidence_at (e, 149)->locator, ==,
                    "csv:row:147:columns:year,temperature_anomaly_c_1951_1980");
    g_object_unref (parser);
    g_hash_table_unref (ids);
    atm_gistemp_evidence_free (again);
    atm_gistemp_evidence_free (e);
}
static AtmScientificArtifact *forge (const AtmScientificArtifact *a, const char *key, const char *value)
{
    JsonParser *parser = json_parser_new ();
    g_assert_true (json_parser_load_from_data (parser, a->payload, -1, NULL));
    if (key != NULL)
        json_object_set_string_member (json_node_get_object (json_parser_get_root (parser)), key, value);
    char *payload = json_to_string (json_parser_get_root (parser), FALSE);
    AtmScientificEvidenceAtom atom = {0};
    atom.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    atom.profile_id = a->profile_id;
    atom.profile_version = a->profile_version;
    atom.repository_id = a->repository_id;
    atom.repository_version = a->repository_version;
    atom.snapshot_sha = a->snapshot_sha;
    atom.source_path = a->source_path;
    atom.locator = key != NULL ? a->locator : "csv:row:3:columns:year,temperature_anomaly_c_1951_1980";
    atom.logical_source_id = a->logical_source_id;
    atom.semantic_type = a->artifact_class;
    atom.raw_payload = payload;
    AtmScientificArtifact *forged = NULL;
    g_assert_true (atm_scientific_artifact_from_evidence (&atom, &forged, NULL));
    g_assert_true (atm_scientific_artifact_validate (forged, NULL));
    g_free (payload);
    g_object_unref (parser);
    return forged;
}
static void test_rehashed_forgery (void)
{
    AtmGistempEvidence *e = build ();
    const char *keys[] = {"coefficient", "unit", "reference_period", "calendar_year", NULL, "source_sha256"};
    const char *values[] = {"999", "kelvin", "1901-1930", "1881", NULL,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (guint i = 0; i < G_N_ELEMENTS (keys); i++) {
        /* Adversarial mutation only: production callers must treat these as read-only. */
        AtmScientificArtifact *target = (AtmScientificArtifact *) atm_gistemp_evidence_at (e, i == 5 ? 0 : 4);
        AtmScientificArtifact *forged = forge (target, keys[i], values[i]);
        AtmScientificArtifact original = *target;
        *target = *forged;
        *forged = original;
        g_assert_true (atm_scientific_artifact_validate (target, NULL));
        GError *error = NULL;
        g_assert_false (atm_gistemp_evidence_validate (e, &error));
        g_assert_error (error, ATM_SCIENTIFIC_ARTIFACT_ERROR, ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY);
        g_clear_error (&error);
        AtmScientificArtifact tampered = *target;
        *target = *forged;
        *forged = tampered;
        atm_scientific_artifact_free (forged);
    }
    g_assert_true (atm_gistemp_evidence_validate (e, NULL));
    atm_gistemp_evidence_free (e);
}
static void test_order_and_digest (void)
{
    AtmGistempEvidence *e = build ();
    AtmScientificArtifact *a = (AtmScientificArtifact *) atm_gistemp_evidence_at (e, 4);
    AtmScientificArtifact *b = (AtmScientificArtifact *) atm_gistemp_evidence_at (e, 5);
    AtmScientificArtifact saved = *a; *a = *b; *b = saved;
    g_assert_true (atm_scientific_artifact_validate (a, NULL));
    g_assert_false (atm_gistemp_evidence_validate (e, NULL));
    saved = *a; *a = *b; *b = saved;
    char first = a->qualified_artifact_id[0];
    a->qualified_artifact_id[0] = first == 'a' ? 'b' : 'a';
    g_assert_false (atm_gistemp_evidence_validate (e, NULL));
    a->qualified_artifact_id[0] = first;
    g_assert_true (atm_gistemp_evidence_validate (e, NULL));
    atm_gistemp_evidence_free (e);
}
static void test_arguments (void)
{
    AtmGistempEvidence *e = NULL;
    g_assert_false (atm_gistemp_evidence_new (NULL, &e, NULL));
    g_assert_null (e);
    g_assert_false (atm_gistemp_evidence_validate (NULL, NULL));
    g_assert_cmpuint (atm_gistemp_evidence_count (NULL), ==, 0);
    g_assert_null (atm_gistemp_evidence_at (NULL, 0));
    g_assert_null (atm_gistemp_evidence_point_support (NULL, 0, 0));
    e = build ();
    g_assert_null (atm_gistemp_evidence_at (e, 150));
    g_assert_null (atm_gistemp_evidence_point_support (e, 146, 0));
    g_assert_null (atm_gistemp_evidence_point_support (e, 0, 5));
    AtmGistempEvidence *saved = e;
    g_assert_false (atm_gistemp_evidence_new (NULL, &e, NULL));
    g_assert_true (e == saved);
    atm_gistemp_evidence_free (e);
    atm_gistemp_evidence_free (NULL);
    AtmGistempAdmission *a = NULL;
    g_assert_false (atm_gistemp_admission_rebuild (NULL, &a, NULL));
    g_assert_null (a);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/gistemp-evidence/identity-support", test_identity_support);
    g_test_add_func ("/gistemp-evidence/rehashed-forgery", test_rehashed_forgery);
    g_test_add_func ("/gistemp-evidence/order-digest", test_order_and_digest);
    g_test_add_func ("/gistemp-evidence/arguments", test_arguments);
    return g_test_run ();
}
