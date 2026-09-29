#pragma once
#include "gistemp_sra.h"
G_BEGIN_DECLS

#define ATM_VERIFIED_SERIES_SCHEMA "atm-verified-series/1"
#define ATM_VERIFIED_SERIES_PROFILE "atm-series/gistemp-complete-annual/1"
/* MISSING is explicit but unsupported by the first complete-source adapter. */
typedef enum { ATM_VERIFIED_Y_NUMERIC = 1, ATM_VERIFIED_Y_MISSING = 2 } AtmVerifiedYStatus;
typedef struct {
    guint order;
    guint calendar_year;
    guint source_row;
    AtmVerifiedYStatus y_status;
    char *source_decimal;
    char *coefficient;
    gint64 exponent;
    gboolean break_before;
    char *missing_reason;
    char *break_reason;
    GPtrArray *support;
} AtmVerifiedPoint;
typedef struct AtmVerifiedSeries AtmVerifiedSeries;
/* Only reviewed GISTEMP admission is accepted; no arbitrary point constructor.
 * *out must be NULL; failure leaves it unchanged. Owns all resulting data. */
gboolean atm_verified_series_from_gistemp (const AtmGistempAdmission *admission,
    AtmVerifiedSeries **out, GError **error);
gboolean atm_verified_series_validate (const AtmVerifiedSeries *series, GError **error);
/* Validate the supplied materialization before rebuilding from owned source. */
gboolean atm_verified_series_rebuild (const AtmVerifiedSeries *series,
    AtmVerifiedSeries **out, GError **error);
void atm_verified_series_free (AtmVerifiedSeries *series);
guint atm_verified_series_count (const AtmVerifiedSeries *series);
/* Every returned pointer is borrowed read-only until series destruction.
 * Do not mutate/free points, strings or support arrays. Invalid index -> NULL. */
const AtmVerifiedPoint *atm_verified_series_point (const AtmVerifiedSeries *series, guint index);
const char *atm_verified_series_scientific_id (const AtmVerifiedSeries *series);
const char *atm_verified_series_qualified_id (const AtmVerifiedSeries *series);
const char *atm_verified_series_unit (const AtmVerifiedSeries *series);
const char *atm_verified_series_reference_period (const AtmVerifiedSeries *series);
guint atm_verified_series_support_count (const AtmVerifiedSeries *series);
const char *atm_verified_series_support (const AtmVerifiedSeries *series, guint index);
G_END_DECLS
