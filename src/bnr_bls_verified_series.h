#pragma once

#include "bnr_bls_series_contract.h"

G_BEGIN_DECLS

/*
 * Source-specific qualification boundary for the one admitted BNR BLS C01
 * calendar-quarter profile. This wrapper is native/test-only: it does not
 * authorize ChartSpec construction, display projection, rendering, History,
 * or an application route.
 */
#define ATM_BNR_BLS_VERIFIED_SERIES_PROFILE ATM_BNR_BLS_SERIES_PROFILE
#define ATM_BNR_BLS_VERIFIED_SERIES_ERROR (atm_bnr_bls_verified_series_error_quark ())

typedef enum {
    ATM_BNR_BLS_VERIFIED_SERIES_ERROR_ARGUMENT,
    ATM_BNR_BLS_VERIFIED_SERIES_ERROR_PROFILE,
    ATM_BNR_BLS_VERIFIED_SERIES_ERROR_IDENTITY
} AtmBnrBlsVerifiedSeriesError;

typedef struct AtmBnrBlsVerifiedSeries AtmBnrBlsVerifiedSeries;

GQuark atm_bnr_bls_verified_series_error_quark (void);

/* Rebuilds the source-bound panel/SRA/SeriesContract chain before owning it.
 * Only the pinned C01 tuple and its single authorized MISSING quarter pass.
 * *out must be NULL; failure leaves it unchanged. */
gboolean atm_bnr_bls_verified_series_from_contract (
    const AtmBnrBlsSeriesContract *contract,
    AtmBnrBlsVerifiedSeries **out,
    GError **error
);

gboolean atm_bnr_bls_verified_series_validate (
    const AtmBnrBlsVerifiedSeries *series,
    GError **error
);

gboolean atm_bnr_bls_verified_series_rebuild (
    const AtmBnrBlsVerifiedSeries *series,
    AtmBnrBlsVerifiedSeries **out,
    GError **error
);

void atm_bnr_bls_verified_series_free (AtmBnrBlsVerifiedSeries *series);

/* Borrowed read-only accessors; returned material remains valid until free. */
const AtmBnrBlsSeriesContract *atm_bnr_bls_verified_series_contract (
    const AtmBnrBlsVerifiedSeries *series
);
guint atm_bnr_bls_verified_series_count (
    const AtmBnrBlsVerifiedSeries *series
);
const AtmSeriesContractPoint *atm_bnr_bls_verified_series_point (
    const AtmBnrBlsVerifiedSeries *series,
    guint index
);
/* Numeric observations map to physical source CSV rows; the MISSING quarter
 * and invalid indices return zero because no source data row exists. */
guint atm_bnr_bls_verified_series_source_row (
    const AtmBnrBlsVerifiedSeries *series,
    guint index
);
const char *atm_bnr_bls_verified_series_scientific_id (
    const AtmBnrBlsVerifiedSeries *series
);
const char *atm_bnr_bls_verified_series_qualified_id (
    const AtmBnrBlsVerifiedSeries *series
);

G_END_DECLS
