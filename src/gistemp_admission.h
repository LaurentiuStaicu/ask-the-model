#pragma once
#include "annual_series_candidate.h"
G_BEGIN_DECLS

/* Independent admission policy; not the EWD evidence profile or VerifiedSeries. */
#define ATM_GISTEMP_ADMISSION_PROFILE "atm-gistemp-pinned-admission/1"
#define ATM_GISTEMP_SOURCE_COUNT 4u
#define ATM_GISTEMP_SOURCE_MAX_BYTES ((gsize) 65536)

typedef struct AtmGistempAdmission AtmGistempAdmission;
/* Fixed source order: CSV, provenance JSON, input manifest, registry CSV.
 * All four byte buffers and repository/snapshot must match the reviewed policy.
 * Copies inputs; *out must be NULL and remains unchanged on failure. */
gboolean atm_gistemp_admission_new (const char *repository, const char *snapshot,
    GBytes *const sources[ATM_GISTEMP_SOURCE_COUNT], AtmGistempAdmission **out,
    GError **error);
void atm_gistemp_admission_free (AtmGistempAdmission *admission);
/* Borrowed immutable values, valid until destruction. Invalid index -> NULL. */
const AtmAnnualSeriesCandidate *atm_gistemp_admission_candidate (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_source_path (const AtmGistempAdmission *a, guint index);
const char *atm_gistemp_admission_source_digest (const AtmGistempAdmission *a, guint index);
const char *atm_gistemp_admission_repository (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_snapshot (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_unit (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_reference_period (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_selection (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_vintage (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_subject (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_attribute (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_epistemic_status (const AtmGistempAdmission *a);
const char *atm_gistemp_admission_x_semantics (const AtmGistempAdmission *a);
G_END_DECLS
