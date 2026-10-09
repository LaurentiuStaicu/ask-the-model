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
 * One open value: the Energy Institute canonical reconstruction obligation is
 * not yet fixed. It is declared below in exactly one place, so the decision
 * lands as a one-line change and nothing else in the chain depends on it.
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
        "Pinned processed observations; the upstream Energy Institute calculation is not reproduced.", \
        "Gross primary-energy supply observations; these do not observe EROI, net energy or a World3 resource stock.", \
        "The 2019-2023 component reconciliation residual is retained as provenance, not repaired or redistributed.", \
        "No VerifiedSeries, rendering or general missing-value authority.", \
        NULL })

/*
 * TODO(F0-19): Energy Institute canonical reconstruction obligation.
 *
 * GISTEMP fixes "atm-gistemp-source-reconstruction/1". The symmetric value is
 * used below so the chain is complete and testable; confirm it against the
 * Energy Institute qualification record before this leaves review, because a
 * wrong obligation string produces identities that are merely different rather
 * than detectably wrong.
 */
#define ATM_EI_RECONSTRUCTION_OBLIGATION \
    "atm-energy-institute-source-reconstruction/1"

extern const AtmSeriesEvidenceDescriptor ATM_GISTEMP_EVIDENCE_DESCRIPTOR;
extern const AtmSeriesEvidenceDescriptor ATM_EI_EVIDENCE_DESCRIPTOR;

G_END_DECLS
