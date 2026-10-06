#pragma once
#include "energy_institute_admission.h"
#include "scientific_artifact.h"

G_BEGIN_DECLS

#define ATM_EI_EVIDENCE_PROFILE "atm-profile/ewd-energy-institute-pinned/v1"
#define ATM_EI_EVIDENCE_COUNT 65u

typedef struct AtmEnergyInstituteEvidence AtmEnergyInstituteEvidence;

gboolean atm_energy_institute_evidence_new (
    const AtmEnergyInstituteAdmission *admission,
    AtmEnergyInstituteEvidence **out,
    GError **error
);
gboolean atm_energy_institute_evidence_validate (
    const AtmEnergyInstituteEvidence *evidence,
    GError **error
);
void atm_energy_institute_evidence_free (
    AtmEnergyInstituteEvidence *evidence
);
guint atm_energy_institute_evidence_count (
    const AtmEnergyInstituteEvidence *evidence
);
const AtmScientificArtifact *atm_energy_institute_evidence_at (
    const AtmEnergyInstituteEvidence *evidence, guint index
);
const char *atm_energy_institute_evidence_point_support (
    const AtmEnergyInstituteEvidence *evidence,
    guint point,
    guint support_index
);

G_END_DECLS
