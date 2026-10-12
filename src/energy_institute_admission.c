#include "energy_institute_admission.h"

static const char repository_id[] = "LaurentiuStaicu/empirical-world3-dynamics";

typedef struct {
    const char *path;
    const char *digest;
} AtmEnergyInstituteSourcePolicy;

typedef struct {
    const char *snapshot_id;
    AtmEnergyInstituteSourcePolicy files[ATM_EI_SOURCE_COUNT];
} AtmEnergyInstituteSnapshotPolicy;

/* Each accepted commit is an exact four-file bundle, not a set of independent
 * file substitutions. The EI observation CSV and provenance are shared blobs
 * across these two snapshots; the complete manifest and registry are not. */
static const AtmEnergyInstituteSnapshotPolicy snapshot_policies[] = {
    {
        .snapshot_id = "d9e249339663015f6d1c05752338a955bf64ad0b",
        .files = {
            {"science/data/processed/energy_institute_global_2026.csv", "46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c"},
            {"science/data/processed/energy_institute_global_2026.provenance.json", "1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57"},
            {"science/data/input_manifest.json", "887bfec97515472e22084a6e28655b1d644d86949dc59b5a506801346a1fce8e"},
            {"science/data/registry.csv", "0637a1842706d2303750d6591851830987f3899de681e432c32b2be7b71adf4d"},
        },
    },
    {
        .snapshot_id = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb",
        .files = {
            {"science/data/processed/energy_institute_global_2026.csv", "46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c"},
            {"science/data/processed/energy_institute_global_2026.provenance.json", "1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57"},
            {"science/data/input_manifest.json", "ece72aed0d444484635e38171df703a775e5922df95b8783385cbfed7e2ffbd6"},
            {"science/data/registry.csv", "6ff4e0bffe03cce368dcd278d6ca57b6e8f181aa241dea7206508499ec3fa3b8"},
        },
    },
};

struct AtmEnergyInstituteAdmission {
    GBytes *sources[ATM_EI_SOURCE_COUNT];
    const AtmEnergyInstituteSnapshotPolicy *policy;
    AtmEnergyInstituteCandidate *candidate;
};

GQuark
atm_energy_institute_admission_error_quark (void)
{
    return g_quark_from_static_string ("atm-energy-institute-admission-error-quark");
}

static gboolean reject (GError **error, AtmEnergyInstituteCandidateError code,
                        const char *message)
{
    g_set_error_literal (error, ATM_EI_CANDIDATE_ERROR, code, message);
    return FALSE;
}

static const AtmEnergyInstituteSnapshotPolicy *
find_policy (const char *repository, const char *snapshot)
{
    if (g_strcmp0 (repository, repository_id) != 0 || snapshot == NULL)
        return NULL;
    for (guint i = 0; i < G_N_ELEMENTS (snapshot_policies); i++)
        if (g_strcmp0 (snapshot, snapshot_policies[i].snapshot_id) == 0)
            return &snapshot_policies[i];
    return NULL;
}

void
atm_energy_institute_admission_free (AtmEnergyInstituteAdmission *a)
{
    if (a == NULL) return;
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_clear_pointer (&a->sources[i], g_bytes_unref);
    atm_energy_institute_candidate_free (a->candidate);
    g_free (a);
}

gboolean
atm_energy_institute_admission_new (const char *repository, const char *snapshot,
                                    GBytes *const sources[ATM_EI_SOURCE_COUNT],
                                    AtmEnergyInstituteAdmission **out, GError **error)
{
    if (out == NULL || *out != NULL || sources == NULL)
        return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Invalid admission arguments.");

    const AtmEnergyInstituteSnapshotPolicy *policy = find_policy (repository, snapshot);
    if (policy == NULL)
        return reject (error, ATM_EI_CANDIDATE_ERROR_SHAPE, "Source snapshot is not admitted.");

    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        if (sources[i] == NULL)
            return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Missing admission source.");
        if (g_bytes_get_size (sources[i]) > ATM_EI_SOURCE_MAX_BYTES)
            return reject (error, ATM_EI_CANDIDATE_ERROR_LIMIT, "Admission source exceeds byte ceiling.");
    }

    AtmEnergyInstituteAdmission *a = g_new0 (AtmEnergyInstituteAdmission, 1);
    a->policy = policy;
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        gsize size;
        const guint8 *bytes = g_bytes_get_data (sources[i], &size);
        a->sources[i] = g_bytes_new (bytes, size);
        char *digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256, bytes, size);
        gboolean matches = g_str_equal (digest, policy->files[i].digest);
        g_free (digest);
        if (!matches) {
            reject (error, ATM_EI_CANDIDATE_ERROR_SHAPE,
                    "Source bytes do not match the complete admitted Energy Institute snapshot bundle.");
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

gboolean
atm_energy_institute_admission_rebuild (const AtmEnergyInstituteAdmission *source,
                                        AtmEnergyInstituteAdmission **out, GError **error)
{
    if (source == NULL)
        return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Missing admitted source.");
    if (out == NULL || *out != NULL)
        return reject (error, ATM_EI_CANDIDATE_ERROR_ARGUMENT, "Invalid rebuilt admission output.");
    return atm_energy_institute_admission_new (repository_id, source->policy->snapshot_id,
                                               source->sources, out, error);
}

const AtmEnergyInstituteCandidate *
atm_energy_institute_admission_candidate (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? a->candidate : NULL; }

const char *
atm_energy_institute_admission_source_path (const AtmEnergyInstituteAdmission *a, guint i)
{ return a != NULL && i < ATM_EI_SOURCE_COUNT ? a->policy->files[i].path : NULL; }

const char *
atm_energy_institute_admission_source_digest (const AtmEnergyInstituteAdmission *a, guint i)
{ return a != NULL && i < ATM_EI_SOURCE_COUNT ? a->policy->files[i].digest : NULL; }

const char *
atm_energy_institute_admission_repository (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? repository_id : NULL; }

const char *
atm_energy_institute_admission_snapshot (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? a->policy->snapshot_id : NULL; }

const char *
atm_energy_institute_admission_unit (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "exajoules/year" : NULL; }

const char *
atm_energy_institute_admission_geography (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "Total World" : NULL; }

const char *
atm_energy_institute_admission_dataset (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "Statistical Review of World Energy 2026" : NULL; }

const char *
atm_energy_institute_admission_subject (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "global_total_energy_supply" : NULL; }

const char *
atm_energy_institute_admission_attribute (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "total_energy_supply" : NULL; }

const char *
atm_energy_institute_admission_accounting_method (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "physical_energy_content" : NULL; }

const char *
atm_energy_institute_admission_epistemic_status (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "empirical" : NULL; }

const char *
atm_energy_institute_admission_x_semantics (const AtmEnergyInstituteAdmission *a)
{ return a != NULL ? "calendar_year" : NULL; }
