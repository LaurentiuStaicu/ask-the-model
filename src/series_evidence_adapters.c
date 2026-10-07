#include "series_evidence_adapters.h"

#include <json-glib/json-glib.h>

#include "gistemp_admission.h"
#include "gistemp_evidence.h"
#include "energy_institute_admission.h"
#include "energy_institute_evidence.h"

/*
 * Source-bound evidence adapters.
 *
 * One static descriptor per admitted source, plus the payload builders each
 * source needs. This replaces gistemp_evidence.c and energy_institute_evidence.c,
 * whose bodies were identical apart from the constants now held here.
 *
 * The builders are the only source-aware code left. They read the admitted
 * bundle through the source-specific admission accessors, which are stable and
 * already tested; nothing else in the evidence layer knows which source it is
 * serving.
 */

/* ---------------------------------------------------------------------------
 * Shared payload plumbing
 * ------------------------------------------------------------------------- */

static void
add_string (JsonBuilder *b, const char *key, const char *value)
{
    json_builder_set_member_name (b, key);
    json_builder_add_string_value (b, value);
}

/* A NULL value is emitted as null rather than omitted, so a payload shape never
 * changes silently when optional metadata is absent. */
static void
add_text (JsonBuilder *b, const char *key, const char *value)
{
    json_builder_set_member_name (b, key);
    if (value != NULL) json_builder_add_string_value (b, value);
    else json_builder_add_null_value (b);
}

static char *
finish_payload (JsonBuilder *b)
{
    json_builder_end_object (b);
    JsonNode *root = json_builder_get_root (b);
    char *json = json_to_string (root, FALSE);
    json_node_free (root);
    g_object_unref (b);
    return json;
}

static char *
uint_payload_value (guint value)
{
    return g_strdup_printf ("%u", value);
}

static char *
exponent_payload_value (gint64 exponent)
{
    return g_strdup_printf ("%" G_GINT64_FORMAT, exponent);
}

/* The source-binding payload. It records the role and the digest of the exact
 * retained bytes; it does not attest upstream provenance. */
static char *
source_binding_payload (const char *schema,
                        const char *roles,
                        const char *digest)
{
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", schema);
    add_string (b, "source_sha256", digest);
    add_string (b, "role", roles);
    return finish_payload (b);
}

/* ---------------------------------------------------------------------------
 * GISTEMP adapter
 * ------------------------------------------------------------------------- */

static gboolean
gistemp_rebuild (gconstpointer admission, gpointer *out, GError **error)
{
    AtmGistempAdmission *rebuilt = NULL;
    if (!atm_gistemp_admission_rebuild ((const AtmGistempAdmission *) admission,
                                        &rebuilt, error))
        return FALSE;
    *out = rebuilt;
    return TRUE;
}

static void
gistemp_free (gpointer admission)
{
    atm_gistemp_admission_free ((AtmGistempAdmission *) admission);
}

static const char *
gistemp_snapshot (gconstpointer admission)
{
    return atm_gistemp_admission_snapshot ((const AtmGistempAdmission *) admission);
}

static const char *
gistemp_source_path (gconstpointer admission, guint index)
{
    return atm_gistemp_admission_source_path ((const AtmGistempAdmission *) admission, index);
}

static const char *
gistemp_source_digest (gconstpointer admission, guint index)
{
    return atm_gistemp_admission_source_digest ((const AtmGistempAdmission *) admission, index);
}

static const AtmAnnualSeriesCandidate *
gistemp_candidate (gconstpointer admission)
{
    return atm_gistemp_admission_candidate ((const AtmGistempAdmission *) admission);
}

static char *
gistemp_source_payload (gconstpointer admission, guint index)
{
    const AtmGistempAdmission *a = admission;
    return source_binding_payload ("atm-gistemp-source-binding/1",
                                   ATM_GISTEMP_SOURCE_ROLES[index],
                                   atm_gistemp_admission_source_digest (a, index));
}

static char *
gistemp_point_payload (gconstpointer admission, guint index)
{
    const AtmGistempAdmission *a = admission;
    const AtmAnnualSeriesCandidate *c = atm_gistemp_admission_candidate (a);
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", "atm-gistemp-annual-observation/1");
    add_text (b, "subject", atm_gistemp_admission_subject (a));
    add_text (b, "attribute", atm_gistemp_admission_attribute (a));
    add_text (b, "x_semantics", atm_gistemp_admission_x_semantics (a));
    char *year = uint_payload_value (atm_annual_series_candidate_year (c, index));
    add_string (b, "calendar_year", year);
    g_free (year);
    add_string (b, "y_status", "NUMERIC");
    add_string (b, "coefficient", atm_annual_series_candidate_coefficient (c, index));
    char *exponent = exponent_payload_value (
        atm_annual_series_candidate_exponent (c, index));
    add_string (b, "exponent", exponent);
    g_free (exponent);
    add_string (b, "numeric_profile", "exact_decimal_coefficient_exponent/1");
    add_text (b, "unit", atm_gistemp_admission_unit (a));
    add_string (b, "dimension", "temperature_difference");
    add_text (b, "reference_period", atm_gistemp_admission_reference_period (a));
    add_text (b, "selection", atm_gistemp_admission_selection (a));
    add_text (b, "access_vintage", atm_gistemp_admission_vintage (a));
    add_text (b, "epistemic_status", atm_gistemp_admission_epistemic_status (a));
    add_string (b, "frequency", "annual");
    add_string (b, "scenario", "not_applicable");
    return finish_payload (b);
}

const AtmSeriesEvidenceDescriptor ATM_GISTEMP_EVIDENCE_DESCRIPTOR = {
    .profile_id = ATM_GISTEMP_EVIDENCE_PROFILE,
    .profile_version = "1",
    .repository_id = "ewd",
    .source_count = ATM_GISTEMP_SOURCE_COUNT,
    .evidence_count = ATM_GISTEMP_EVIDENCE_COUNT,
    .support_per_point = ATM_GISTEMP_SOURCE_COUNT + 1u,
    .roles = ATM_GISTEMP_SOURCE_ROLES,
    .source_logical_stem = "gistemp-source",
    .point_logical_prefix = "ewd:gistemp-annual:",
    .source_payload_schema = "atm-gistemp-source-binding/1",
    .source_semantic_type = "ewd.gistemp_source_binding",
    .point_payload_schema = "atm-gistemp-annual-observation/1",
    .point_semantic_type = "ewd.gistemp_annual_observation",
    /* Column spelling is fixed by the admitted CSV dialect. */
    .point_locator_columns = "year,temperature_anomaly_c_1951_1980",
    .canonical_obligation = "atm-gistemp-source-reconstruction/1",
    .semantic_profile = ATM_GISTEMP_EVIDENCE_PROFILE "@1",
    .limitations = ATM_GISTEMP_LIMITATIONS,
    .admission_rebuild = gistemp_rebuild,
    .admission_free = gistemp_free,
    .admission_snapshot = gistemp_snapshot,
    .admission_source_path = gistemp_source_path,
    .admission_source_digest = gistemp_source_digest,
    .admission_candidate = gistemp_candidate,
    .source_payload = gistemp_source_payload,
    .point_payload = gistemp_point_payload,
};

/* ---------------------------------------------------------------------------
 * Energy Institute adapter
 * ------------------------------------------------------------------------- */

static gboolean
energy_rebuild (gconstpointer admission, gpointer *out, GError **error)
{
    AtmEnergyInstituteAdmission *rebuilt = NULL;
    if (!atm_energy_institute_admission_rebuild (
            (const AtmEnergyInstituteAdmission *) admission, &rebuilt, error))
        return FALSE;
    *out = rebuilt;
    return TRUE;
}

static void
energy_free (gpointer admission)
{
    atm_energy_institute_admission_free ((AtmEnergyInstituteAdmission *) admission);
}

static const char *
energy_snapshot (gconstpointer admission)
{
    return atm_energy_institute_admission_snapshot (
        (const AtmEnergyInstituteAdmission *) admission);
}

static const char *
energy_source_path (gconstpointer admission, guint index)
{
    return atm_energy_institute_admission_source_path (
        (const AtmEnergyInstituteAdmission *) admission, index);
}

static const char *
energy_source_digest (gconstpointer admission, guint index)
{
    return atm_energy_institute_admission_source_digest (
        (const AtmEnergyInstituteAdmission *) admission, index);
}

static const AtmAnnualSeriesCandidate *
energy_candidate (gconstpointer admission)
{
    return (const AtmAnnualSeriesCandidate *)
        atm_energy_institute_admission_candidate (
            (const AtmEnergyInstituteAdmission *) admission);
}

static char *
energy_source_payload (gconstpointer admission, guint index)
{
    const AtmEnergyInstituteAdmission *a = admission;
    return source_binding_payload ("atm-energy-institute-source-binding/1",
                                   ATM_EI_SOURCE_ROLES[index],
                                   atm_energy_institute_admission_source_digest (a, index));
}

static char *
energy_point_payload (gconstpointer admission, guint index)
{
    const AtmEnergyInstituteAdmission *a = admission;
    const AtmEnergyInstituteCandidate *c =
        atm_energy_institute_admission_candidate (a);
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    add_string (b, "schema", "atm-energy-institute-annual-observation/1");
    add_text (b, "dataset", atm_energy_institute_admission_dataset (a));
    add_text (b, "geography", atm_energy_institute_admission_geography (a));
    add_text (b, "subject", atm_energy_institute_admission_subject (a));
    add_text (b, "attribute", atm_energy_institute_admission_attribute (a));
    add_text (b, "x_semantics", atm_energy_institute_admission_x_semantics (a));
    char *year = uint_payload_value (atm_energy_institute_candidate_year (c, index));
    add_string (b, "calendar_year", year);
    g_free (year);
    add_string (b, "y_status", "NUMERIC");
    add_string (b, "coefficient",
        atm_energy_institute_candidate_coefficient (c, index));
    char *exponent = exponent_payload_value (
        atm_energy_institute_candidate_exponent (c, index));
    add_string (b, "exponent", exponent);
    g_free (exponent);
    add_string (b, "numeric_profile", "exact_decimal_coefficient_exponent/1");
    add_text (b, "unit", atm_energy_institute_admission_unit (a));
    add_string (b, "dimension", "energy_flow_rate");
    add_string (b, "frequency", "annual");
    add_text (b, "epistemic_status",
        atm_energy_institute_admission_epistemic_status (a));
    add_string (b, "scenario", "not_applicable");
    return finish_payload (b);
}

const AtmSeriesEvidenceDescriptor ATM_EI_EVIDENCE_DESCRIPTOR = {
    .profile_id = ATM_EI_EVIDENCE_PROFILE,
    .profile_version = "1",
    .repository_id = "ewd",
    .source_count = ATM_EI_SOURCE_COUNT,
    .evidence_count = ATM_EI_EVIDENCE_COUNT,
    .support_per_point = ATM_EI_SOURCE_COUNT + 1u,
    .roles = ATM_EI_SOURCE_ROLES,
    .source_logical_stem = "energy-institute-source",
    .point_logical_prefix = "ewd:energy-institute-annual:",
    .source_payload_schema = "atm-energy-institute-source-binding/1",
    .source_semantic_type = "ewd.energy_institute_source_binding",
    .point_payload_schema = "atm-energy-institute-annual-observation/1",
    .point_semantic_type = "ewd.energy_institute_annual_observation",
    /* Column spelling is fixed by the admitted CSV dialect. */
    .point_locator_columns = "year,total_primary_energy_ej",
    .canonical_obligation = ATM_EI_RECONSTRUCTION_OBLIGATION,
    .semantic_profile = ATM_EI_EVIDENCE_PROFILE "@1",
    .limitations = ATM_EI_LIMITATIONS,
    .admission_rebuild = energy_rebuild,
    .admission_free = energy_free,
    .admission_snapshot = energy_snapshot,
    .admission_source_path = energy_source_path,
    .admission_source_digest = energy_source_digest,
    .admission_candidate = energy_candidate,
    .source_payload = energy_source_payload,
    .point_payload = energy_point_payload,
};
