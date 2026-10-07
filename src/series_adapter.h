#pragma once

#include "series_contract.h"
#include "series_evidence.h"

G_BEGIN_DECLS

/*
 * Source-bound adapter from an admitted bundle to the general series contract.
 *
 * This is the bridge the CHART-M1c migration needs: series_contract.h is the
 * destination for downstream consumers, while the per-source adapter owns the
 * source-specific presentation rules.
 *
 * The adapter does not mint identities by itself. It materialises an
 * AtmSeriesContract from the descriptor-driven evidence collection and hands it
 * to atm_series_contract_validate() together with the finalized SRA. Identity is
 * derived from contract material, never supplied by a caller.
 *
 * Point X is the admitted calendar year, which is the admitted form for both
 * current sources. A source whose X is not a calendar year needs its own
 * mapping rather than a guess.
 */

typedef struct {
    /*
     * Bounded metadata bound into the general contract. These are the reviewed
     * scientific statements about the series, and they are deliberately not
     * derived from artifact payloads: the contract carries them so that identity
     * is a function of reviewed inputs.
     */

    const char *series_semantics;   /* "empirical_annual_observations" */
    const char *scenario;           /* "not_applicable" */
    const char *time_scope;         /* e.g. "1880-2025" */
    const char *profile_id;         /* the general contract's series profile */
    const char *profile_version;    /* "1" */
} AtmSeriesAdapterMetadata;

/* *out must be NULL; failure leaves it unchanged. Owns all resulting data. */
gboolean atm_series_adapter_from_evidence (
    const AtmSeriesEvidenceDescriptor *descriptor,
    const AtmSeriesAdapterMetadata *metadata,
    const AtmSeriesEvidence *evidence,
    AtmSeriesContract **out,
    GError **error);

/* Admit, build evidence, build the contract, validate against the finalized
 * SRA. The whole path in one call, for callers that do not need the
 * intermediate evidence object. */
gboolean atm_series_adapter_admit (
    const AtmSeriesEvidenceDescriptor *descriptor,
    const AtmSeriesAdapterMetadata *metadata,
    gconstpointer admission,
    AtmSeriesContract **out_contract,
    GError **error);

/* Rebuild the contract from the owned evidence and compare both identities.
 * FALSE when the materialised contract disagrees with its own reconstruction. */
gboolean atm_series_adapter_validate (
    const AtmSeriesContract *contract,
    const AtmSeriesEvidenceDescriptor *descriptor,
    const AtmSeriesAdapterMetadata *metadata,
    const AtmSeriesEvidence *evidence,
    GError **error);

G_END_DECLS
