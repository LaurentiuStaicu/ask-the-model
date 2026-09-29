#pragma once
#include "gistemp_admission.h"
#include "scientific_artifact.h"
G_BEGIN_DECLS

#define ATM_GISTEMP_EVIDENCE_PROFILE "atm-profile/ewd-gistemp-pinned/v1"
#define ATM_GISTEMP_EVIDENCE_COUNT 150u
/* Dormant deterministic source adapter, not a VerifiedSeries or SRA result.
 * Artifacts 0..3 bind the four source files; 4..149 are annual observations.
 * Every observation requires all four source bindings as aggregate support. */
typedef struct AtmGistempEvidence AtmGistempEvidence;
gboolean atm_gistemp_evidence_new (const AtmGistempAdmission *admission,
    AtmGistempEvidence **out, GError **error);
gboolean atm_gistemp_evidence_validate (const AtmGistempEvidence *evidence, GError **error);
void atm_gistemp_evidence_free (AtmGistempEvidence *evidence);
guint atm_gistemp_evidence_count (const AtmGistempEvidence *evidence);
/* Borrowed read-only artifact. Do not modify its fields or free it. */
const AtmScientificArtifact *atm_gistemp_evidence_at (const AtmGistempEvidence *evidence, guint index);
/* Five support IDs per point: four bundle bindings plus its observation artifact.
 * A returned string is borrowed. Invalid point/support indices return NULL. */
const char *atm_gistemp_evidence_point_support (const AtmGistempEvidence *evidence,
    guint point, guint support_index);
G_END_DECLS
