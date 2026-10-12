#include "energy_institute_verified_series.h"

#include "series_adapter.h"
#include "series_evidence_adapters.h"

#include <string.h>

struct AtmEnergyInstituteVerifiedSeries {
    AtmEnergyInstituteAdmission *admission;
    AtmSeriesContract *contract;
};

static const AtmSeriesAdapterMetadata EI_SERIES_METADATA = {
    .series_semantics = "empirical_annual_observations",
    .scenario = "not_applicable",
    .time_scope = "1965-2025",
    .profile_id = ATM_EI_VERIFIED_SERIES_PROFILE,
    .profile_version = "1",
};

/* The Evidence/SRA descriptor intentionally has no output-series approval.
 * This separate, private descriptor records the reviewed native series profile
 * without widening the SRA-only public descriptor or enabling a chart caller. */
static const AtmSeriesEvidenceDescriptor *
reviewed_ei_series_descriptor (void)
{
    static AtmSeriesEvidenceDescriptor descriptor;
    static gsize initialized = 0;

    if (g_once_init_enter (&initialized)) {
        descriptor = ATM_EI_EVIDENCE_DESCRIPTOR;
        descriptor.series_semantics = "empirical_annual_observations";
        descriptor.series_scenario = "not_applicable";
        descriptor.series_epistemic_status = "empirical";
        descriptor.series_profile_id = ATM_EI_VERIFIED_SERIES_PROFILE;
        descriptor.series_profile_version = "1";
        g_once_init_leave (&initialized, 1);
    }

    return &descriptor;
}

static gboolean
reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SERIES_CONTRACT_ERROR,
                         ATM_SERIES_CONTRACT_ERROR_SHAPE, message);
    return FALSE;
}

static gboolean
valid_sha256 (const char *value)
{
    if (value == NULL || strlen (value) != 64)
        return FALSE;
    for (guint i = 0; i < 64; i++) {
        if (!g_ascii_isdigit (value[i]) &&
            !(value[i] >= 'a' && value[i] <= 'f'))
            return FALSE;
    }
    return TRUE;
}

static gboolean
series_shape_valid (const AtmEnergyInstituteVerifiedSeries *s,
                    GError **error)
{
    if (s == NULL || s->admission == NULL || s->contract == NULL)
        return reject (error, "Missing Energy Institute verified-series state.");

    const AtmSeriesContract *c = s->contract;
    const AtmEnergyInstituteCandidate *candidate =
        atm_energy_institute_admission_candidate (s->admission);
    const char *snapshot =
        atm_energy_institute_admission_snapshot (s->admission);
    const char *csv_path =
        atm_energy_institute_admission_source_path (s->admission, 0);
    char *expected_version = snapshot != NULL
        ? g_strconcat ("snapshot:", snapshot, NULL) : NULL;

    gboolean metadata_matches =
        candidate != NULL &&
        atm_energy_institute_candidate_count (candidate) == 61 &&
        c->x_kind == ATM_SERIES_X_CALENDAR_YEAR &&
        g_strcmp0 (c->subject, "global_total_energy_supply") == 0 &&
        g_strcmp0 (c->attribute, "total_energy_supply") == 0 &&
        g_strcmp0 (c->x_unit, "calendar_year") == 0 &&
        g_strcmp0 (c->x_dimension, "time") == 0 &&
        g_strcmp0 (c->y_unit, "exajoules/year") == 0 &&
        g_strcmp0 (c->y_dimension, "energy_flow_rate") == 0 &&
        g_strcmp0 (c->series_semantics, "empirical_annual_observations") == 0 &&
        g_strcmp0 (c->scenario, "not_applicable") == 0 &&
        g_strcmp0 (c->time_scope, "1965-2025") == 0 &&
        g_strcmp0 (c->epistemic_status, "empirical") == 0 &&
        g_strcmp0 (c->repository_id, "ewd") == 0 &&
        g_strcmp0 (c->repository_version, expected_version) == 0 &&
        g_strcmp0 (c->snapshot_sha, snapshot) == 0 &&
        g_strcmp0 (c->source_path, csv_path) == 0 &&
        g_strcmp0 (c->profile_id, ATM_EI_VERIFIED_SERIES_PROFILE) == 0 &&
        g_strcmp0 (c->profile_version, "1") == 0 &&
        valid_sha256 (c->scientific_content_id) &&
        valid_sha256 (c->qualified_series_id) &&
        c->points != NULL && c->points->len == 61;

    g_free (expected_version);
    if (!metadata_matches)
        return reject (error, "Energy Institute series metadata or coverage is unsupported.");

    for (guint i = 0; i < 61; i++) {
        const AtmSeriesContractPoint *p = g_ptr_array_index (c->points, i);
        const char *source_decimal =
            atm_energy_institute_candidate_decimal (candidate, i);
        const char *source_coefficient =
            atm_energy_institute_candidate_coefficient (candidate, i);
        char *expected_x = g_strdup_printf ("%u", 1965u + i);
        gboolean point_matches =
            p != NULL &&
            p->order == i &&
            g_strcmp0 (p->x, expected_x) == 0 &&
            atm_energy_institute_candidate_year (candidate, i) == 1965u + i &&
            atm_energy_institute_candidate_source_row (candidate, i) == i + 2u &&
            p->y_status == ATM_SERIES_Y_NUMERIC &&
            g_strcmp0 (p->source_decimal, source_decimal) == 0 &&
            g_strcmp0 (p->coefficient, source_coefficient) == 0 &&
            p->exponent == atm_energy_institute_candidate_exponent (candidate, i) &&
            !p->negative_zero &&
            !p->break_before &&
            p->missing_reason == NULL &&
            p->break_reason == NULL &&
            p->support != NULL &&
            p->support->len == ATM_EI_SOURCE_COUNT + 1u;

        g_free (expected_x);
        if (!point_matches)
            return reject (error, "Energy Institute point disagrees with the admitted annual source.");

        for (guint j = 0; j < p->support->len; j++) {
            const char *support = g_ptr_array_index (p->support, j);
            if (!valid_sha256 (support))
                return reject (error, "Energy Institute point has invalid qualified support.");
        }
    }

    return TRUE;
}

void
atm_energy_institute_verified_series_free (AtmEnergyInstituteVerifiedSeries *s)
{
    if (s == NULL)
        return;
    atm_energy_institute_admission_free (s->admission);
    atm_series_contract_free (s->contract);
    g_free (s);
}

gboolean
atm_energy_institute_verified_series_from_admission (
    const AtmEnergyInstituteAdmission *admission,
    AtmEnergyInstituteVerifiedSeries **out,
    GError **error)
{
    if (admission == NULL || out == NULL || *out != NULL)
        return reject (error, "Invalid Energy Institute verified-series arguments.");

    AtmEnergyInstituteVerifiedSeries *s =
        g_new0 (AtmEnergyInstituteVerifiedSeries, 1);
    if (!atm_energy_institute_admission_rebuild (
            admission, &s->admission, error))
        goto invalid;

    if (!atm_series_adapter_admit (reviewed_ei_series_descriptor (),
                                   &EI_SERIES_METADATA,
                                   s->admission,
                                   &s->contract,
                                   error))
        goto invalid;

    if (!series_shape_valid (s, error))
        goto invalid;

    /* Repeat the full source -> Evidence -> finalized SRA -> series check.
     * This is deliberately source-specific and dormant; it does not widen the
     * legacy GISTEMP type or create a ChartSpec/History route. */
    if (!atm_series_adapter_revalidate (s->contract,
                                        reviewed_ei_series_descriptor (),
                                        &EI_SERIES_METADATA,
                                        s->admission,
                                        error))
        goto invalid;

    *out = s;
    return TRUE;

invalid:
    atm_energy_institute_verified_series_free (s);
    return FALSE;
}

gboolean
atm_energy_institute_verified_series_validate (
    const AtmEnergyInstituteVerifiedSeries *s,
    GError **error)
{
    if (!series_shape_valid (s, error))
        return FALSE;

    return atm_series_adapter_revalidate (s->contract,
                                          reviewed_ei_series_descriptor (),
                                          &EI_SERIES_METADATA,
                                          s->admission,
                                          error);
}

gboolean
atm_energy_institute_verified_series_rebuild (
    const AtmEnergyInstituteVerifiedSeries *s,
    AtmEnergyInstituteVerifiedSeries **out,
    GError **error)
{
    if (out == NULL || *out != NULL)
        return reject (error, "Invalid Energy Institute verified-series rebuild output.");
    if (!atm_energy_institute_verified_series_validate (s, error))
        return FALSE;
    return atm_energy_institute_verified_series_from_admission (s->admission,
                                                                 out, error);
}

const AtmSeriesContract *
atm_energy_institute_verified_series_contract (
    const AtmEnergyInstituteVerifiedSeries *s)
{
    return s != NULL ? s->contract : NULL;
}

guint
atm_energy_institute_verified_series_count (
    const AtmEnergyInstituteVerifiedSeries *s)
{
    return s != NULL && s->contract != NULL && s->contract->points != NULL
        ? s->contract->points->len : 0;
}

const AtmSeriesContractPoint *
atm_energy_institute_verified_series_point (
    const AtmEnergyInstituteVerifiedSeries *s,
    guint index)
{
    return s != NULL && s->contract != NULL && s->contract->points != NULL &&
           index < s->contract->points->len
        ? g_ptr_array_index (s->contract->points, index) : NULL;
}

const char *
atm_energy_institute_verified_series_scientific_id (
    const AtmEnergyInstituteVerifiedSeries *s)
{
    return s != NULL && s->contract != NULL
        ? atm_series_contract_scientific_id (s->contract) : NULL;
}

const char *
atm_energy_institute_verified_series_qualified_id (
    const AtmEnergyInstituteVerifiedSeries *s)
{
    return s != NULL && s->contract != NULL
        ? atm_series_contract_qualified_id (s->contract) : NULL;
}
