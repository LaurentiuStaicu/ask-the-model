#pragma once
#include "energy_institute_candidate.h"

G_BEGIN_DECLS

#define ATM_EI_ADMISSION_PROFILE "atm-energy-institute-pinned-admission/1"
#define ATM_EI_SOURCE_COUNT 4u
#define ATM_EI_SOURCE_MAX_BYTES ((gsize) 65536)

typedef struct AtmEnergyInstituteAdmission AtmEnergyInstituteAdmission;

gboolean atm_energy_institute_admission_new (
    const char *repository,
    const char *snapshot,
    GBytes *const sources[ATM_EI_SOURCE_COUNT],
    AtmEnergyInstituteAdmission **out,
    GError **error
);
gboolean atm_energy_institute_admission_rebuild (
    const AtmEnergyInstituteAdmission *source,
    AtmEnergyInstituteAdmission **out,
    GError **error
);
void atm_energy_institute_admission_free (AtmEnergyInstituteAdmission *admission);

const AtmEnergyInstituteCandidate *atm_energy_institute_admission_candidate (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_source_path (
    const AtmEnergyInstituteAdmission *admission, guint index
);
const char *atm_energy_institute_admission_source_digest (
    const AtmEnergyInstituteAdmission *admission, guint index
);
const char *atm_energy_institute_admission_repository (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_snapshot (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_unit (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_geography (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_dataset (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_subject (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_attribute (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_epistemic_status (
    const AtmEnergyInstituteAdmission *admission
);
const char *atm_energy_institute_admission_x_semantics (
    const AtmEnergyInstituteAdmission *admission
);

G_END_DECLS
