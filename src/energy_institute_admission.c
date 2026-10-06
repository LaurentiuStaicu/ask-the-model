#include "energy_institute_admission.h"

static const char repository_id[] = "LaurentiuStaicu/empirical-world3-dynamics";
static const char snapshot_id[] = "d9e249339663015f6d1c05752338a955bf64ad0b";
static const struct { const char *path; const char *digest; } policy[] = {
    {"science/data/processed/energy_institute_global_2026.csv", "46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c"},
    {"science/data/processed/energy_institute_global_2026.provenance.json", "1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57"},
    {"science/data/input_manifest.json", "0637a1842706d2303750d6591851830987f3899de681e432c32b2be7b71adf4d"},
    {"science/data/registry.csv", "0637a1842706d2303750d6591851830987f3899de681e432c32b2be7b71adf4d"},
};

struct AtmEnergyInstituteAdmission {
    GBytes *sources[ATM_EI_SOURCE_COUNT];
    AtmEnergyInstituteCandidate *candidate;
};

static gboolean reject (GError **error, AtmEnergyInstituteCandidateError code, const char *message)
{
    g_set_error_literal (error, ATM_EI_CANDIDATE_ERROR, code, message);
    return FALSE;
}

void atm_energy_institute_admission_free (AtmEnergyInstituteAdmission *a)
{
    if (a == NULL) return;
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_clear_pointer (&a->sources[i], g_bytes_unref);
    atm_energy_institute_candidate_free (a->candidate);
    g_free (a);
}

gboolean atm_energy_institute_admission_new (
    const char *repository, const char *snapshot,
    GBytes *const sources[ATM_EI_SOURCE_COUNT],
    AtmEnergyInstituteAdmission **out, GError **error)
{
    if (out == NULL || *out != NULL || sources == NULL)
        return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Invalid admission arguments.");
    if (g_strcmp0 (repository, repository_id) != 0 || g_strcmp0 (snapshot, snapshot_id) != 0)
        return reject (error, ATM_EI_CANDIDATE_ERROR_SHAPE, "Source snapshot is not admitted.");

    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        if (sources[i] == NULL)
            return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Missing admission source.");
        if (g_bytes_get_size (sources[i]) > ATM_EI_SOURCE_MAX_BYTES)
            return reject (error, ATM_EI_CANDIDATE_ERROR_LIMIT, "Admission source exceeds byte ceiling.");
    }

    AtmEnergyInstituteAdmission *a = g_new0 (AtmEnergyInstituteAdmission, 1);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        gsize size;
        const guint8 *bytes = g_bytes_get_data (sources[i], &size);
        a->sources[i] = g_bytes_new (bytes, size);
        char *digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256, bytes, size);
        gboolean matches = g_str_equal (digest, policy[i].digest);
        g_free (digest);
        if (!matches) {
            reject (error, ATM_EI_CANDIDATE_ERROR_SHAPE, "Source bytes do not match admitted Energy Institute policy.");
            goto invalid;
        }
    }
    if (!atm_energy_institute_candidate_parse (a->sources[0], &a->candidate, error))
        goto invalid;
    *out = a;
    return TRUE;

invalid:
    atm_energy_institute_admission_free (a);
    return FALSE;
}

gboolean atm_energy_institute_admission_rebuild (
    const AtmEnergyInstituteAdmission *source,
    AtmEnergyInstituteAdmission **out, GError **error)
{
    if (source == NULL)
        return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Missing admitted source.");
    return atm_energy_institute_admission_new (repository_id, snapshot_id, source->sources, out, error);
}

const AtmEnergyInstituteCandidate *atm_energy_institute_admission_candidate (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? a->candidate : NULL; }
const char *atm_energy_institute_admission_source_path (const AtmEnergyInstituteAdmission *a, guint i)
{ return a != NULL && i < ATM_EI_SOURCE_COUNT ? policy[i].path : NULL; }
const char *atm_energy_institute_admission_source_digest (const AtmEnergyInstituteAdmission *a, guint i)
{ return a != NULL && i < ATM_EI_SOURCE_COUNT ? policy[i].digest : NULL; }
const char *atm_energy_institute_admission_repository (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? repository_id : NULL; }
const char *atm_energy_institute_admission_snapshot (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? snapshot_id : NULL; }
const char *atm_energy_institute_admission_unit (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "exajoules/year" : NULL; }
const char *atm_energy_institute_admission_geography (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "Total World" : NULL; }
const char *atm_energy_institute_admission_dataset (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "Statistical Review of World Energy 2026" : NULL; }
const char *atm_energy_institute_admission_subject (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "global_primary_energy_supply" : NULL; }
const char *atm_energy_institute_admission_attribute (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "total_primary_energy" : NULL; }
const char *atm_energy_institute_admission_epistemic_status (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "empirical" : NULL; }
const char *atm_energy_institute_admission_x_semantics (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "calendar_year" : NULL; }
