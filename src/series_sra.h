#pragma once

#include "scientific_result.h"
#include "series_evidence.h"

G_BEGIN_DECLS

/*
 * Generic source-bound SRA construction.
 *
 * This replaces gistemp_sra.c. Everything that file hard-coded - the point
 * count, the support arity, the reconstruction obligation, the semantic profile
 * and the limitations - is a descriptor field, so adding a second source adds
 * no branch here.
 *
 * The result carries the same envelope the source-specific module produced:
 * the generic SRA schema obligation, the source reconstruction obligation, the
 * source semantic profile and the exact-decimal numeric profile. Facts are
 * built from each admitted observation artifact payload, which is the same
 * material the artifact identity was minted over.
 */

typedef struct AtmSeriesSra AtmSeriesSra;

/* *out must be NULL; failure leaves it unchanged. Owns all resulting data. */
gboolean atm_series_sra_new (
    const AtmSeriesEvidenceDescriptor *descriptor,
    gconstpointer admission,
    AtmSeriesSra **out,
    GError **error);

/* Rebuilds from owned evidence and compares both result identities. */
gboolean atm_series_sra_validate (
    const AtmSeriesSra *sra,
    GError **error);

void atm_series_sra_free (AtmSeriesSra *sra);

/* Borrowed read-only finalized result. Do not modify or free any fields. */
const AtmSraResult *atm_series_sra_result (const AtmSeriesSra *sra);

/* Borrowed read-only evidence this SRA was built from. */
const AtmSeriesEvidence *atm_series_sra_evidence (const AtmSeriesSra *sra);

G_END_DECLS
