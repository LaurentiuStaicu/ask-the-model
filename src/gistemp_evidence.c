#include "gistemp_evidence.h"
#include <json-glib/json-glib.h>

struct AtmGistempEvidence {
    AtmGistempAdmission *admission;
    GPtrArray *artifacts;
};
static gboolean reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SCIENTIFIC_ARTIFACT_ERROR,
                         ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY, message);
    return FALSE;
}
static void add_string (JsonBuilder *b, const char *key, const char *value)
{
    json_builder_set_member_name (b, key);
    json_builder_add_string_value (b, value);
}
static char *finish_payload (JsonBuilder *b)
{
    json_builder_end_object (b);
    JsonNode *root = json_builder_get_root (b);
    char *json = json_to_string (root, FALSE);
    json_node_free (root);
    g_object_unref (b);
    return json;
}
static char *source_payload (const AtmGistempAdmission *a, guint index)
{
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", "atm-gistemp-source-binding/1");
    add_string (b, "source_sha256", atm_gistemp_admission_source_digest (a, index));
    /* Each role is scoped to this exact four-file reviewed policy. */
    static const char *roles[] = {"observations", "provenance", "manifest", "registry"};
    add_string (b, "role", roles[index]);
    return finish_payload (b);
}
static char *point_payload (const AtmGistempAdmission *a, guint index)
{
    const AtmAnnualSeriesCandidate *c = atm_gistemp_admission_candidate (a);
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", "atm-gistemp-annual-observation/1");
    add_string (b, "subject", atm_gistemp_admission_subject (a));
    add_string (b, "attribute", atm_gistemp_admission_attribute (a));
    add_string (b, "x_semantics", atm_gistemp_admission_x_semantics (a));
    char *year = g_strdup_printf ("%u", atm_annual_series_candidate_year (c, index));
    add_string (b, "calendar_year", year);
    g_free (year);
    add_string (b, "y_status", "NUMERIC");
    add_string (b, "coefficient", atm_annual_series_candidate_coefficient (c, index));
    char *exponent = g_strdup_printf ("%" G_GINT64_FORMAT,
                                     atm_annual_series_candidate_exponent (c, index));
    add_string (b, "exponent", exponent);
    g_free (exponent);
    add_string (b, "numeric_profile", "exact_decimal_coefficient_exponent/1");
    add_string (b, "unit", atm_gistemp_admission_unit (a));
    add_string (b, "dimension", "temperature_difference");
    add_string (b, "reference_period", atm_gistemp_admission_reference_period (a));
    add_string (b, "selection", atm_gistemp_admission_selection (a));
    add_string (b, "access_vintage", atm_gistemp_admission_vintage (a));
    add_string (b, "epistemic_status", atm_gistemp_admission_epistemic_status (a));
    add_string (b, "frequency", "annual");
    add_string (b, "scenario", "not_applicable");
    return finish_payload (b);
}
static gboolean append_artifact (AtmGistempEvidence *e, guint source_index,
    const char *locator, const char *logical_id, const char *semantic_type,
    char *payload, GError **error)
{
    /* This status is minted only after the source-specific admission/rebuild.
     * No caller-supplied evidence atom, metadata or point payload is accepted. */
    AtmScientificEvidenceAtom atom = {0};
    atom.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    atom.profile_id = ATM_GISTEMP_EVIDENCE_PROFILE;
    atom.profile_version = "1";
    atom.repository_id = "ewd";
    const char *snapshot = atm_gistemp_admission_snapshot (e->admission);
    char *version = g_strconcat ("snapshot:", snapshot, NULL);
    atom.repository_version = version;
    atom.snapshot_sha = (char *) snapshot;
    atom.source_path = (char *) atm_gistemp_admission_source_path (e->admission, source_index);
    atom.locator = (char *) locator;
    atom.logical_source_id = (char *) logical_id;
    atom.semantic_type = (char *) semantic_type;
    atom.raw_payload = payload;
    AtmScientificArtifact *artifact = NULL;
    gboolean ok = atm_scientific_artifact_from_evidence (&atom, &artifact, error);
    g_free (version);
    g_free (payload);
    if (!ok) return FALSE;
    g_ptr_array_add (e->artifacts, artifact);
    return TRUE;
}
void atm_gistemp_evidence_free (AtmGistempEvidence *e)
{
    if (e == NULL) return;
    atm_gistemp_admission_free (e->admission);
    g_ptr_array_unref (e->artifacts);
    g_free (e);
}
gboolean atm_gistemp_evidence_new (const AtmGistempAdmission *admission,
    AtmGistempEvidence **out, GError **error)
{
    if (out == NULL || *out != NULL || admission == NULL)
        return reject (error, "Invalid source-evidence arguments.");
    AtmGistempEvidence *e = g_new0 (AtmGistempEvidence, 1);
    e->artifacts = g_ptr_array_new_with_free_func ((GDestroyNotify) atm_scientific_artifact_free);
    if (!atm_gistemp_admission_rebuild (admission, &e->admission, error)) goto invalid;
    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++) {
        char *logical = g_strconcat ("ewd:gistemp-source:",
            atm_gistemp_admission_source_path (e->admission, i), NULL);
        gboolean ok = append_artifact (e, i, "file:complete", logical,
            "ewd.gistemp_source_binding", source_payload (e->admission, i), error);
        g_free (logical);
        if (!ok) goto invalid;
    }
    const AtmAnnualSeriesCandidate *c = atm_gistemp_admission_candidate (e->admission);
    for (guint i = 0; i < atm_annual_series_candidate_count (c); i++) {
        char *locator = g_strdup_printf ("csv:row:%u:columns:year,temperature_anomaly_c_1951_1980",
            atm_annual_series_candidate_source_row (c, i));
        char *logical = g_strdup_printf ("ewd:gistemp-annual:%u",
            atm_annual_series_candidate_year (c, i));
        gboolean ok = append_artifact (e, 0, locator, logical,
            "ewd.gistemp_annual_observation", point_payload (e->admission, i), error);
        g_free (logical);
        g_free (locator);
        if (!ok) goto invalid;
    }
    if (e->artifacts->len != ATM_GISTEMP_EVIDENCE_COUNT) {
        reject (error, "Unexpected source-evidence coverage.");
        goto invalid;
    }
    *out = e;
    return TRUE;
invalid:
    atm_gistemp_evidence_free (e);
    return FALSE;
}
gboolean atm_gistemp_evidence_validate (const AtmGistempEvidence *e, GError **error)
{
    if (e == NULL || e->artifacts->len != ATM_GISTEMP_EVIDENCE_COUNT)
        return reject (error, "Invalid source-evidence shape.");
    AtmGistempEvidence *expected = NULL;
    if (!atm_gistemp_evidence_new (e->admission, &expected, error)) return FALSE;
    gboolean ok = TRUE;
    for (guint i = 0; i < e->artifacts->len; i++) {
        const AtmScientificArtifact *actual = g_ptr_array_index (e->artifacts, i);
        const AtmScientificArtifact *rebuilt = g_ptr_array_index (expected->artifacts, i);
        if (!atm_scientific_artifact_validate (actual, error)) { ok = FALSE; break; }
        if (g_strcmp0 (actual->storage_digest, rebuilt->storage_digest) != 0) {
            ok = reject (error, "Scientific artifact disagrees with admitted source reconstruction.");
            break;
        }
    }
    atm_gistemp_evidence_free (expected);
    return ok;
}
guint atm_gistemp_evidence_count (const AtmGistempEvidence *e)
{ return e != NULL ? e->artifacts->len : 0; }
const AtmScientificArtifact *atm_gistemp_evidence_at (const AtmGistempEvidence *e, guint i)
{ return e != NULL && i < e->artifacts->len ? g_ptr_array_index (e->artifacts, i) : NULL; }
const char *atm_gistemp_evidence_point_support (const AtmGistempEvidence *e, guint point, guint support)
{
    if (point >= 146 || support >= 5) return NULL;
    const AtmScientificArtifact *a = atm_gistemp_evidence_at (e, support < 4 ? support : point + 4);
    return a != NULL ? a->qualified_artifact_id : NULL;
}
