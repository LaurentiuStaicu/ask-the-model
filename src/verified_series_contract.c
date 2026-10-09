#include "verified_series_contract.h"

#include "series_evidence_adapters.h"

/*
 * Option C bridge between the pinned VerifiedSeries identity and the general
 * series contract. See the header for why the two coexist rather than one
 * replacing the other.
 *
 * Nothing here re-mints or re-homes the persisted identity: it belongs to
 * verified_series.c and stays there. This module only runs the shared chain so
 * that callers wanting an AtmSeriesContract have one.
 */

const AtmSeriesAdapterMetadata *
atm_verified_series_gistemp_metadata (void)
{
    static const AtmSeriesAdapterMetadata metadata = {
        .series_semantics = "empirical_annual_observations",
        .scenario = "not_applicable",
        .time_scope = "1880-2025",
        .profile_id = "atm-series/gistemp-complete-annual/1",
        .profile_version = "1",
    };
    return &metadata;
}

gboolean
atm_verified_series_contract (const AtmGistempAdmission *admission,
                              AtmSeriesContract **out,
                              GError **error)
{
    if (admission == NULL || out == NULL || *out != NULL) {
        g_set_error_literal (error, ATM_SERIES_CONTRACT_ERROR,
                             ATM_SERIES_CONTRACT_ERROR_SHAPE,
                             "Invalid verified-series contract arguments.");
        return FALSE;
    }

    /* The descriptor carries every GISTEMP-specific value, so this call site
     * names no source constant beyond selecting which descriptor to use. */
    return atm_series_adapter_admit (&ATM_GISTEMP_EVIDENCE_DESCRIPTOR,
                                     atm_verified_series_gistemp_metadata (),
                                     admission, out, error);
}
