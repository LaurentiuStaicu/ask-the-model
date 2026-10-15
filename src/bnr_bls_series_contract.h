#pragma once

#include <glib.h>

#include "bnr_bls_panel.h"
#include "bnr_bls_sra.h"
#include "series_contract.h"

G_BEGIN_DECLS

#define ATM_BNR_BLS_SERIES_PROFILE "atm-series/bnr-bls-c01-calendar-quarter/1"

/*
 * Native/test-only source-specific adapter. It creates the generic
 * SeriesContract from the exact pinned BNR panel and its finalized SRA.
 * This is not the legacy AtmVerifiedSeries type and grants no application,
 * ChartSpec, History or release authority.
 */
typedef enum {
    ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
    ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SOURCE,
    ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
    ATM_BNR_BLS_SERIES_CONTRACT_ERROR_IDENTITY
} AtmBnrBlsSeriesContractError;

#define ATM_BNR_BLS_SERIES_CONTRACT_ERROR \
    (atm_bnr_bls_series_contract_error_quark ())

typedef struct AtmBnrBlsSeriesContract AtmBnrBlsSeriesContract;

GQuark atm_bnr_bls_series_contract_error_quark (void);

/* Requires an already finalized SRA that reconstructs from the same panel.
 * The new object owns independent panel/SRA/contract material. *out must be
 * NULL; failure leaves it unchanged. */
gboolean atm_bnr_bls_series_contract_from_sra (
    const AtmBnrBlsPanel *panel,
    const AtmBnrBlsSra *sra,
    AtmBnrBlsSeriesContract **out,
    GError **error
);

/* Revalidates the pinned panel, SRA, tuple binding and every contract field by
 * independently reconstructing the source -> SRA -> SeriesContract chain. */
gboolean atm_bnr_bls_series_contract_validate (
    const AtmBnrBlsSeriesContract *series,
    GError **error
);

gboolean atm_bnr_bls_series_contract_rebuild (
    const AtmBnrBlsSeriesContract *series,
    AtmBnrBlsSeriesContract **out,
    GError **error
);

void atm_bnr_bls_series_contract_free (
    AtmBnrBlsSeriesContract *series
);

/* Borrowed read-only objects remain valid until destruction. */
const AtmSeriesContract *atm_bnr_bls_series_contract_value (
    const AtmBnrBlsSeriesContract *series
);

guint atm_bnr_bls_series_contract_count (
    const AtmBnrBlsSeriesContract *series
);

const AtmSeriesContractPoint *atm_bnr_bls_series_contract_point (
    const AtmBnrBlsSeriesContract *series,
    guint index
);
/* Returns the physical source CSV row for numeric observations; returns zero
 * for the qualified missing quarter or an invalid/unavailable mapping. */
guint atm_bnr_bls_series_contract_source_row (
    const AtmBnrBlsSeriesContract *series,
    guint index
);

const char *atm_bnr_bls_series_contract_scientific_id (
    const AtmBnrBlsSeriesContract *series
);

const char *atm_bnr_bls_series_contract_qualified_id (
    const AtmBnrBlsSeriesContract *series
);

G_END_DECLS
