#pragma once

#include "energy_institute_admission.h"
#include "series_contract.h"

G_BEGIN_DECLS

#define ATM_EI_VERIFIED_SERIES_PROFILE "atm-series/energy-institute-tes-complete-annual/1"

/*
 * Source-specific, dormant qualification object for the single reviewed EI
 * processed snapshot. It owns a pinned admission and a source-bound generic
 * series contract. It is not wired to ChartSpec, History, or the application.
 * Construction has no caller-supplied values/points/identity setters.
 */
typedef struct AtmEnergyInstituteVerifiedSeries AtmEnergyInstituteVerifiedSeries;

gboolean atm_energy_institute_verified_series_from_admission (
    const AtmEnergyInstituteAdmission *admission,
    AtmEnergyInstituteVerifiedSeries **out,
    GError **error);

/* Recheck shape, both series identities, the pinned Evidence/SRA and a fresh
 * reconstruction from owned admission bytes. */
gboolean atm_energy_institute_verified_series_validate (
    const AtmEnergyInstituteVerifiedSeries *series,
    GError **error);

gboolean atm_energy_institute_verified_series_rebuild (
    const AtmEnergyInstituteVerifiedSeries *series,
    AtmEnergyInstituteVerifiedSeries **out,
    GError **error);

void atm_energy_institute_verified_series_free (
    AtmEnergyInstituteVerifiedSeries *series);

/* Borrowed read-only objects/strings remain valid until destruction. */
const AtmSeriesContract *atm_energy_institute_verified_series_contract (
    const AtmEnergyInstituteVerifiedSeries *series);
guint atm_energy_institute_verified_series_count (
    const AtmEnergyInstituteVerifiedSeries *series);
const AtmSeriesContractPoint *atm_energy_institute_verified_series_point (
    const AtmEnergyInstituteVerifiedSeries *series,
    guint index);
const char *atm_energy_institute_verified_series_scientific_id (
    const AtmEnergyInstituteVerifiedSeries *series);
const char *atm_energy_institute_verified_series_qualified_id (
    const AtmEnergyInstituteVerifiedSeries *series);

G_END_DECLS
