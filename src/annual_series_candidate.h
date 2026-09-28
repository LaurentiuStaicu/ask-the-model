#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Allocation ceilings, not claims of performance or scientific coverage. */
#define ATM_ANNUAL_SERIES_MAX_BYTES ((gsize) 64 * 1024)
#define ATM_ANNUAL_SERIES_MAX_POINTS 256u
#define ATM_ANNUAL_SERIES_MAX_DECIMAL_BYTES 64u

typedef enum {
    ATM_ANNUAL_SERIES_ERROR_ARGUMENT,
    ATM_ANNUAL_SERIES_ERROR_LIMIT,
    ATM_ANNUAL_SERIES_ERROR_SHAPE,
    ATM_ANNUAL_SERIES_ERROR_ORDER,
    ATM_ANNUAL_SERIES_ERROR_NUMBER
} AtmAnnualSeriesError;

#define ATM_ANNUAL_SERIES_ERROR (atm_annual_series_error_quark ())
GQuark atm_annual_series_error_quark (void);

/* An immutable parsed candidate, NOT qualified scientific evidence.
 * No arbitrary metadata, qualified identity or rendering authority is accepted.
 * Dialect: exact GISTEMP processed header, YYYY, fixed four-place decimals,
 * consecutive ascending years, LF lines, optional final LF. */
typedef struct AtmAnnualSeriesCandidate AtmAnnualSeriesCandidate;

gboolean atm_annual_series_candidate_parse (
    GBytes *source,
    AtmAnnualSeriesCandidate **out_candidate,
    GError **error
);

void atm_annual_series_candidate_free (AtmAnnualSeriesCandidate *candidate);
guint atm_annual_series_candidate_count (const AtmAnnualSeriesCandidate *candidate);

/* All returned strings are borrowed until candidate_free(). Out-of-range
 * access returns NULL/0. Row numbers are one-based, including the header. */
const char *atm_annual_series_candidate_source_sha256 (const AtmAnnualSeriesCandidate *candidate);
guint atm_annual_series_candidate_year (const AtmAnnualSeriesCandidate *candidate, guint index);
guint atm_annual_series_candidate_source_row (const AtmAnnualSeriesCandidate *candidate, guint index);
const char *atm_annual_series_candidate_decimal (const AtmAnnualSeriesCandidate *candidate, guint index);
const char *atm_annual_series_candidate_coefficient (const AtmAnnualSeriesCandidate *candidate, guint index);
gint64 atm_annual_series_candidate_exponent (const AtmAnnualSeriesCandidate *candidate, guint index);

G_END_DECLS
