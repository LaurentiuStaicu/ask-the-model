#pragma once

#include "series_evidence.h"

G_BEGIN_DECLS

/*
 * The two admitted source descriptors.
 *
 * Each is a static const object fixed at compile time, in the same spirit as the
 * admission policies themselves. A descriptor is not configuration: it is the
 * reviewed statement of which sources a series is built from, in which order,
 * with which profile and payload vocabulary.
 *
 * Energy Institute Evidence is admitted, but SRA qualification is blocked
 * because no authoritative reconstruction obligation has been recorded for it.
 * Do not infer one by symmetry with GISTEMP.
 */

/* Fixed source order, matching the admission policy: the observations file
 * first, then its provenance, the input manifest and the registry. */
#define ATM_GISTEMP_SOURCE_ROLES \
    ((const char *const[]) { "observations", "provenance", "manifest", "registry" })

#define ATM_EI_SOURCE_ROLES \
    ((const char *const[]) { "observations", "provenance", "manifest", "registry" })

/* Scientific limitations, rendered verbatim into each finalized SRA.
 * These are claims about the source, so they are never shared between sources. */
#define ATM_GISTEMP_LIMITATIONS \
    ((const char *const[]) { \
        "Pinned processed observations; raw NASA processing not reproduced.", \
        "Source-specific annual observations only; not a World3 pollution trajectory.", \
        "No VerifiedSeries, rendering or general missing-value authority.", \
        NULL })

#define ATM_EI_LIMITATIONS \
    ((const char *const[]) { \
        "Pinned processed observations; the upstream Energy Institute calculation is not reproduced by AtM.", \
        "Total Energy Supply (TES) under Physical Energy Content; the EWD input column retains the legacy name total_primary_energy_ej.", \
        "Approved semantic precedence: the pinned tes_ej mapping and EI 2026 methodology define the metric; legacy EWD primary-energy labels remain provenance only.", \
        "This gross flow series does not directly observe EROI, net energy or a World3 resource stock.", \
        "The 2019-2023 component reconciliation residual is retained as provenance, not repaired or redistributed.", \
        "No VerifiedSeries, rendering or general missing-value authority.", \
        NULL })

/*
 * TODO(F0-19): the pinned Energy Institute provenance JSON, input manifest and
 * registry do not define an AtM canonical reconstruction obligation. Leave
 * this descriptor field NULL: the generic Evidence layer can still preserve the
 * admitted bytes and observations, while generic SRA qualification fails closed.
 * Replace NULL only after the obligation is approved and recorded authoritatively.
 */

extern const AtmSeriesEvidenceDescriptor ATM_GISTEMP_EVIDENCE_DESCRIPTOR;
extern const AtmSeriesEvidenceDescriptor ATM_EI_EVIDENCE_DESCRIPTOR;

G_END_DECLS
