#include "energy_institute_evidence.h"
#include <json-glib/json-glib.h>

struct AtmEnergyInstituteEvidence {
    AtmEnergyInstituteAdmission *admission;
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

static char *source_payload (const AtmEnergyInstituteAdmission *a, guint index)
{
    static const char *roles[] = {"observations", "provenance", "manifest", "registry"};
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", "atm-energy-institute-source-binding/1");
    add_string (b, "source_sha256",
                atm_energy_institute_admission_source_digest (a, index));
    add_string (b, "role", roles[index]);
    return finish_payload (b);
}

static char *point_payload (const AtmEnergyInstituteAdmission *a, guint index)
{
    const AtmEnergyInstituteCandidate *c =
        atm_energy_institute_admission_candidate (a);
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", "atm-energy-institute-annual-observation/1");
    add_string (b, "dataset", atm_energy_institute_admission_dataset (a));
    add_string (b, "geography", atm_energy_institute_admission_geography (a));
    add_string (b, "subject", atm_energy_institute_admission_subject (a));
    add_string (b, "attribute", atm_energy_institute_admission_attribute (a));
    add_string (b, "x_semantics", atm_energy_institute_admission_x_semantics (a));
    char *year = g_strdup_printf ("%u",
        atm_energy_institute_candidate_year (c, index));
    add_string (b, "calendar_year", year);
    g_free (year);
    add_string (b, "y_status", "NUMERIC");
    add_string (b, "coefficient",
        atm_energy_institute_candidate_coefficient (c, index));
    char *exponent = g_strdup_printf ("%" G_GINT64_FORMAT,
        atm_energy_institute_candidate_exponent (c, index));
    add_string (b, "exponent", exponent);
    g_free (exponent);
    add_string (b, "numeric_profile", "exact_decimal_coefficient_exponent/1");
    add_string (b, "unit", atm_energy_institute_admission_unit (a));
    add_string (b, "dimension", "energy_flow_rate");
    add_string (b, "frequency", "annual");
    add_string (b, "epistemic_status",
        atm_energy_institute_admission_epistemic_status (a));
    add_string (b, "scenario", "not_applicable");
    return finish_payload (b);
}

static gboolean append_artifact (
    AtmEnergyInstituteEvidence *e,
    guint source_index,
    const char *locator,
    const char *logical_id,
    const char *semantic_type,
    char *payload,
    GError **error)
{
    AtmScientificEvidenceAtom atom = {0};
    atom.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    atom.profile_id = ATM_EI_EVIDENCE_PROFILE;
    atom.profile_version = "1";
    atom.repository_id = "ewd";
    const char *snapshot = atm_energy_institute_admission_snapshot (e->admission);
    char *version = g_strconcat ("snapshot:", snapshot, NULL);
    atom.repository_version = version;
    atom.snapshot_sha = (char *) snapshot;
    atom.source_path = (char *)
        atm_energy_institute_admission_source_path (e->admission, source_index);
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

void atm_energy_institute_evidence_free (AtmEnergyInstituteEvidence *e)
{
    if (e == NULL) return;
    atm_energy_institute_admission_free (e->admission);
    g_ptr_array_unref (e->artifacts);
    g_free (e);
}

gboolean atm_energy_institute_evidence_new (
    const AtmEnergyInstituteAdmission *admission,
    AtmEnergyInstituteEvidence **out,
    GError **error)
{
    if (out == NULL || *out != NULL || admission == NULL)
        return reject (error, "Invalid source-evidence arguments.");

    AtmEnergyInstituteEvidence *e = g_new0 (AtmEnergyInstituteEvidence, 1);
    e->artifacts = g_ptr_array_new_with_free_func(
        (GDestroyNotify) atm_scientific_artifact_free);

    if (!atm_energy_institute_admission_rebuild(admission, &e->admission, error))
        goto invalid;

    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *logical = g_strconcat("ewd:energy-institute-source:",
            atm_energy_institute_admission_source_path(e->admission, i), NULL);
        gboolean ok = append_artifact(e, i, "file:complete", logical,
            "ewd.energy_institute_source_binding",
            source_payload(e->admission, i), error);
        g_free(logical);
        if (!ok) goto invalid;
    }

    const AtmEnergyInstituteCandidate *c =
        atm_energy_institute_admission_candidate(e->admission);
    for (guint i = 0; i < atm_energy_institute_candidate_count(c); i++) {
        char *locator = g_strdup_printf(
            "csv:row:%u:columns:year,total_primary_energy_ej",
            atm_energy_institute_candidate_source_row(c, i));
        char *logical = g_strdup_printf("ewd:energy-institute-annual:%u",
            atm_energy_institute_candidate_year(c, i));
        gboolean ok = append_artifact(e, 0, locator, logical,
            "ewd.energy_institute_annual_observation",
            point_payload(e->admission, i), error);
        g_free(logical);
        g_free(locator);
        if (!ok) goto invalid;
    }

    if (e->artifacts->len != ATM_EI_EVIDENCE_COUNT) {
        reject(error, "Unexpected source-evidence coverage.");
        goto invalid;
    }

    *out = e;
    return TRUE;

invalid:
    atm_energy_institute_evidence_free(e);
    return FALSE;
}

gboolean atm_energy_institute_evidence_validate (
    const AtmEnergyInstituteEvidence *e,
    GError **error)
{
    if (e == NULL || e->artifacts->len != ATM_EI_EVIDENCE_COUNT)
        return reject(error, "Invalid source-evidence shape.");

    AtmEnergyInstituteEvidence *expected = NULL;
    if (!atm_energy_institute_evidence_new(e->admission, &expected, error))
        return FALSE;

    gboolean ok = TRUE;
    for (guint i = 0; i < e->artifacts->len; i++) {
        const AtmScientificArtifact *actual = g_ptr_array_index(e->artifacts, i);
        const AtmScientificArtifact *rebuilt = g_ptr_array_index(expected->artifacts, i);
        if (!atm_scientific_artifact_validate(actual, error)) {
            ok = FALSE;
            break;
        }
        if (g_strcmp0(actual->storage_digest, rebuilt->storage_digest) != 0) {
            ok = reject(error,
                "Scientific artifact disagrees with admitted source reconstruction.");
            break;
        }
    }

    atm_energy_institute_evidence_free(expected);
    return ok;
}

guint atm_energy_institute_evidence_count (
    const AtmEnergyInstituteEvidence *e)
{
    return e != NULL ? e->artifacts->len : 0;
}

const AtmScientificArtifact *atm_energy_institute_evidence_at (
    const AtmEnergyInstituteEvidence *e, guint i)
{
    return e != NULL && i < e->artifacts->len
        ? g_ptr_array_index(e->artifacts, i) : NULL;
}

const char *atm_energy_institute_evidence_point_support (
    const AtmEnergyInstituteEvidence *e, guint point, guint support)
{
    if (point >= 61 || support >= 5) return NULL;
    const AtmScientificArtifact *a =
        atm_energy_institute_evidence_at(e, support < 4 ? support : point + 4);
    return a != NULL ? a->qualified_artifact_id : NULL;
}
