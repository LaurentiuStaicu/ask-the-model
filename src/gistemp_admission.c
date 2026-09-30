#include "gistemp_admission.h"

static const char repository_id[] = ATM_GISTEMP_ADMITTED_REPOSITORY;
static const char snapshot_id[] = ATM_GISTEMP_ADMITTED_SNAPSHOT;
static const struct { const char *path; const char *digest; } policy[] = {
    {ATM_GISTEMP_PRIMARY_SOURCE_PATH, "c03e15198201c491cfbd665ad655f72c54f2df19db9c93614b5a2fb4ee5590fb"},
    {"science/data/processed/nasa_gistemp_global_2026.provenance.json", "2ac56f0f060fbba0b15f483a2510b293fe5cfbebb2a8e39816f3b73270496ed9"},
    {"science/data/input_manifest.json", "887bfec97515472e22084a6e28655b1d644d86949dc59b5a506801346a1fce8e"},
    {"science/data/registry.csv", "0637a1842706d2303750d6591851830987f3899de681e432c32b2be7b71adf4d"},
};
struct AtmGistempAdmission {
    GBytes *sources[ATM_GISTEMP_SOURCE_COUNT];
    AtmAnnualSeriesCandidate *candidate;
};

static gboolean reject (GError **error, AtmAnnualSeriesError code, const char *message)
{
    g_set_error_literal (error, ATM_ANNUAL_SERIES_ERROR, code, message);
    return FALSE;
}
void atm_gistemp_admission_free (AtmGistempAdmission *a)
{
    if (a == NULL) return;
    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++)
        g_clear_pointer (&a->sources[i], g_bytes_unref);
    atm_annual_series_candidate_free (a->candidate);
    g_free (a);
}
gboolean atm_gistemp_admission_new (const char *repository, const char *snapshot,
    GBytes *const sources[ATM_GISTEMP_SOURCE_COUNT], AtmGistempAdmission **out,
    GError **error)
{
    if (out == NULL || *out != NULL || sources == NULL)
        return reject (error, ATM_ANNUAL_SERIES_ERROR_ARGUMENT, "Invalid admission arguments.");
    if (g_strcmp0 (repository, repository_id) != 0 || g_strcmp0 (snapshot, snapshot_id) != 0)
        return reject (error, ATM_ANNUAL_SERIES_ERROR_SHAPE, "Source snapshot is not admitted.");
    /* Bound the complete bundle before any source allocation. */
    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++) {
        if (sources[i] == NULL)
            return reject (error, ATM_ANNUAL_SERIES_ERROR_ARGUMENT, "Missing admission source.");
        if (g_bytes_get_size (sources[i]) > ATM_GISTEMP_SOURCE_MAX_BYTES)
            return reject (error, ATM_ANNUAL_SERIES_ERROR_LIMIT, "Admission source exceeds byte ceiling.");
    }
    AtmGistempAdmission *a = g_new0 (AtmGistempAdmission, 1);
    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++) {
        gsize size;
        const guint8 *bytes = g_bytes_get_data (sources[i], &size);
        a->sources[i] = g_bytes_new (bytes, size);
        bytes = g_bytes_get_data (a->sources[i], &size);
        char *digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256, bytes, size);
        gboolean matches = g_str_equal (digest, policy[i].digest);
        g_free (digest);
        if (!matches) {
            reject (error, ATM_ANNUAL_SERIES_ERROR_SHAPE, "Source bytes do not match admitted policy.");
            goto invalid;
        }
    }
    if (!atm_annual_series_candidate_parse (a->sources[0], &a->candidate, error)) goto invalid;
    if (atm_annual_series_candidate_count (a->candidate) != 146 ||
        atm_annual_series_candidate_year (a->candidate, 0) != 1880 ||
        atm_annual_series_candidate_year (a->candidate, 145) != 2025) {
        reject (error, ATM_ANNUAL_SERIES_ERROR_SHAPE, "Admitted coverage mismatch.");
        goto invalid;
    }
    *out = a;
    return TRUE;
invalid:
    atm_gistemp_admission_free (a);
    return FALSE;
}
const AtmAnnualSeriesCandidate *atm_gistemp_admission_candidate (const AtmGistempAdmission *a)
{ return a != NULL ? a->candidate : NULL; }
const char *atm_gistemp_admission_source_path (const AtmGistempAdmission *a, guint i)
{ return a != NULL && i < ATM_GISTEMP_SOURCE_COUNT ? policy[i].path : NULL; }
const char *atm_gistemp_admission_source_digest (const AtmGistempAdmission *a, guint i)
{ return a != NULL && i < ATM_GISTEMP_SOURCE_COUNT ? policy[i].digest : NULL; }
const char *atm_gistemp_admission_repository (const AtmGistempAdmission *a)
{ return a != NULL ? repository_id : NULL; }
const char *atm_gistemp_admission_snapshot (const AtmGistempAdmission *a)
{ return a != NULL ? snapshot_id : NULL; }
/* Reviewed interpretation of the exact retained provenance and registry bytes.
 * Any source revision needs an explicit policy review, not caller setters. */
const char *atm_gistemp_admission_unit (const AtmGistempAdmission *a)
{ return a != NULL ? "degree_Celsius_anomaly" : NULL; }
const char *atm_gistemp_admission_reference_period (const AtmGistempAdmission *a)
{ return a != NULL ? "1951-1980" : NULL; }
const char *atm_gistemp_admission_selection (const AtmGistempAdmission *a)
{ return a != NULL ? "Global annual J-D Land-Ocean Temperature Index" : NULL; }
const char *atm_gistemp_admission_vintage (const AtmGistempAdmission *a)
{ return a != NULL ? "2026-08-31" : NULL; }
const char *atm_gistemp_admission_subject (const AtmGistempAdmission *a)
{ return a != NULL ? "global_surface_temperature" : NULL; }
const char *atm_gistemp_admission_attribute (const AtmGistempAdmission *a)
{ return a != NULL ? "annual_temperature_anomaly" : NULL; }
const char *atm_gistemp_admission_epistemic_status (const AtmGistempAdmission *a)
{ return a != NULL ? "empirical" : NULL; }
const char *atm_gistemp_admission_x_semantics (const AtmGistempAdmission *a)
{ return a != NULL ? "calendar_year" : NULL; }

gboolean atm_gistemp_admission_rebuild (const AtmGistempAdmission *source,
    AtmGistempAdmission **out, GError **error)
{
    if (source == NULL)
        return reject (error, ATM_ANNUAL_SERIES_ERROR_ARGUMENT, "Missing admitted source.");
    return atm_gistemp_admission_new (repository_id, snapshot_id, source->sources, out, error);
}
