#pragma once
#include <glib.h>
#include "scientific_canonical.h"

G_BEGIN_DECLS

#define ATM_EI_CANDIDATE_MAX_BYTES ((gsize) 64 * 1024)
#define ATM_EI_CANDIDATE_MAX_POINTS 128u
#define ATM_EI_CANDIDATE_MAX_DECIMAL_BYTES 64u

typedef enum {
    ATM_EI_CANDIDATE_ERROR_ARGUMENT,
    ATM_EI_CANDIDATE_ERROR_LIMIT,
    ATM_EI_CANDIDATE_ERROR_SHAPE,
    ATM_EI_CANDIDATE_ERROR_ORDER,
    ATM_EI_CANDIDATE_ERROR_NUMBER
} AtmEnergyInstituteCandidateError;

#define ATM_EI_CANDIDATE_ERROR (atm_energy_institute_candidate_error_quark ())

typedef struct AtmEnergyInstituteCandidate AtmEnergyInstituteCandidate;

GQuark atm_energy_institute_candidate_error_quark (void);

gboolean atm_energy_institute_candidate_parse (
    GBytes *source,
    AtmEnergyInstituteCandidate **out_candidate,
    GError **error
);
void atm_energy_institute_candidate_free (
    AtmEnergyInstituteCandidate *candidate
);
guint atm_energy_institute_candidate_count (
    const AtmEnergyInstituteCandidate *candidate
);
const char *atm_energy_institute_candidate_source_sha256 (
    const AtmEnergyInstituteCandidate *candidate
);
guint atm_energy_institute_candidate_year (
    const AtmEnergyInstituteCandidate *candidate, guint index
);
guint atm_energy_institute_candidate_source_row (
    const AtmEnergyInstituteCandidate *candidate, guint index
);
const char *atm_energy_institute_candidate_decimal (
    const AtmEnergyInstituteCandidate *candidate, guint index
);
const char *atm_energy_institute_candidate_coefficient (
    const AtmEnergyInstituteCandidate *candidate, guint index
);
gint64 atm_energy_institute_candidate_exponent (
    const AtmEnergyInstituteCandidate *candidate, guint index
);

G_END_DECLS
