#pragma once

#include "verified_series.h"
#include "series_adapter.h"

G_BEGIN_DECLS

/*
 * Option C bridge: expose an admitted GISTEMP VerifiedSeries as the general
 * AtmSeriesContract, without changing the identities the existing module mints.
 *
 * Why this shape, and not a rewrite of verified_series.c onto the contract:
 *
 * verified_series.c produces two identities that are pinned in
 * tests/fixtures/chart01/verified-series-identity.json and compared by
 * chart_history_reconstruct against rows already persisted by users:
 *
 *   scientific_id : 4cfff6bb991c2d771228c09e81ca860a5b3e195e4f2390d059a3c8aacc65c48c
 *   qualified_id  : 29827400d3e250f6c935eefa33d2a8c4f99c4198721676231e777d16ee1f6b24
 *
 * series_contract.c serialises a different member set with different value
 * types, so a contract built from it mints different ids. Replacing the module
 * outright would invalidate every stored chart.
 *
 * Under this bridge the persisted identity keeps its owner and the general
 * contract serves the consumers that want typed series material: downstream
 * code can move to AtmSeriesContract while stored charts stay reconstructible.
 *
 * If the decision later becomes A (widen the general serialisation so it
 * reproduces the pinned ids) or B (accept new ids and re-record the fixture),
 * this file is the only thing that changes.
 */

/* Borrowed read-only metadata for the pinned GISTEMP series, suitable for
 * passing to atm_series_adapter_admit(). The strings are static. */
const AtmSeriesAdapterMetadata *atm_verified_series_gistemp_metadata (void);

/*
 * Build the general contract for an already-admitted GISTEMP bundle.
 *
 * This is independent of atm_verified_series_from_gistemp: it runs the shared
 * evidence/SRA/adapter chain and returns the contract, which carries general
 * identities, not the pinned ones. Callers that need the persisted identity
 * read it from the VerifiedSeries; callers that need typed series material take
 * the contract.
 *
 * *out must be NULL; failure leaves it unchanged.
 */
gboolean atm_verified_series_contract (
    const AtmGistempAdmission *admission,
    AtmSeriesContract **out,
    GError **error);

G_END_DECLS
