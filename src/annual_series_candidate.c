#include "annual_series_candidate.h"
#include "scientific_canonical.h"

#include <string.h>

typedef struct {
    guint year;
    guint source_row;
    char *decimal;
    AtmScientificDecimal *canonical;
} AnnualPoint;

struct AtmAnnualSeriesCandidate {
    GBytes *source;
    char *source_sha256;
    GPtrArray *points;
};

GQuark
atm_annual_series_error_quark (void)
{
    return g_quark_from_static_string ("atm-annual-series-error-quark");
}

static void
point_free (gpointer data)
{
    AnnualPoint *point = data;
    g_free (point->decimal);
    atm_scientific_decimal_free (point->canonical);
    g_free (point);
}

void
atm_annual_series_candidate_free (AtmAnnualSeriesCandidate *candidate)
{
    if (candidate == NULL) return;
    g_bytes_unref (candidate->source);
    g_free (candidate->source_sha256);
    g_ptr_array_unref (candidate->points);
    g_free (candidate);
}

static gboolean
fail (GError **error, AtmAnnualSeriesError code, const char *message)
{
    g_set_error_literal (error, ATM_ANNUAL_SERIES_ERROR, code, message);
    return FALSE;
}

static gboolean
decimal_shape (const char *text)
{
    const char *p = text;
    if (*p == '-') p++;
    if (!g_ascii_isdigit (*p)) return FALSE;
    if (*p == '0' && g_ascii_isdigit (p[1])) return FALSE;
    while (g_ascii_isdigit (*p)) p++;
    if (*p != '.') return FALSE;
    p++;
    for (guint i = 0; i < 4; i++) {
        if (!g_ascii_isdigit (*p)) return FALSE;
        p++;
    }
    return *p == '\0';
}

gboolean
atm_annual_series_candidate_parse (
    GBytes *source,
    AtmAnnualSeriesCandidate **out_candidate,
    GError **error
)
{
    static const char header[] = "year,temperature_anomaly_c_1951_1980\n";
    if (source == NULL || out_candidate == NULL || *out_candidate != NULL)
        return fail (error, ATM_ANNUAL_SERIES_ERROR_ARGUMENT, "Invalid candidate arguments.");

    gsize size = 0;
    const guint8 *bytes = g_bytes_get_data (source, &size);
    if (size > ATM_ANNUAL_SERIES_MAX_BYTES)
        return fail (error, ATM_ANNUAL_SERIES_ERROR_LIMIT, "Annual source exceeds byte ceiling.");
    if (size <= sizeof header - 1 || memchr (bytes, '\0', size) != NULL ||
        memcmp (bytes, header, sizeof header - 1) != 0)
        return fail (error, ATM_ANNUAL_SERIES_ERROR_SHAPE, "Invalid annual source header or bytes.");

    AtmAnnualSeriesCandidate *candidate = g_new0 (AtmAnnualSeriesCandidate, 1);
    /* Deep-copy even GBytes backed by caller-owned/static memory. */
    candidate->source = g_bytes_new (bytes, size);
    bytes = g_bytes_get_data (candidate->source, NULL);
    candidate->source_sha256 = g_compute_checksum_for_data (G_CHECKSUM_SHA256, bytes, size);
    candidate->points = g_ptr_array_new_with_free_func (point_free);
    gsize offset = sizeof header - 1;
    guint previous = 0;
    while (offset < size) {
        if (candidate->points->len >= ATM_ANNUAL_SERIES_MAX_POINTS) {
            fail (error, ATM_ANNUAL_SERIES_ERROR_LIMIT, "Annual source exceeds point ceiling.");
            goto invalid;
        }
        const guint8 *line = bytes + offset;
        const guint8 *newline = memchr (line, '\n', size - offset);
        gsize length = newline != NULL ? (gsize) (newline - line) : size - offset;
        if (length > 5 + ATM_ANNUAL_SERIES_MAX_DECIMAL_BYTES) {
            fail (error, ATM_ANNUAL_SERIES_ERROR_LIMIT, "Annual numeric token exceeds ceiling.");
            goto invalid;
        }
        if (length < 11 || line[4] != ',') {
            fail (error, ATM_ANNUAL_SERIES_ERROR_SHAPE, "Invalid annual row shape.");
            goto invalid;
        }
        guint year = 0;
        for (guint i = 0; i < 4; i++) {
            if (!g_ascii_isdigit (line[i])) {
                fail (error, ATM_ANNUAL_SERIES_ERROR_SHAPE, "Year must have four ASCII digits.");
                goto invalid;
            }
            year = year * 10 + (line[i] - '0');
        }
        if (year == 0 || (candidate->points->len > 0 && year != previous + 1)) {
            fail (error, ATM_ANNUAL_SERIES_ERROR_ORDER, "Annual years must be positive and consecutive.");
            goto invalid;
        }
        AnnualPoint *point = g_new0 (AnnualPoint, 1);
        point->decimal = g_strndup ((const char *) line + 5, length - 5);
        if (!decimal_shape (point->decimal) ||
            !atm_scientific_decimal_parse (point->decimal, &point->canonical, NULL)) {
            point_free (point);
            fail (error, ATM_ANNUAL_SERIES_ERROR_NUMBER, "Unsupported annual decimal value.");
            goto invalid;
        }
        point->year = year;
        point->source_row = candidate->points->len + 2;
        g_ptr_array_add (candidate->points, point);
        previous = year;
        offset += length + (newline != NULL ? 1 : 0);
    }
    *out_candidate = candidate;
    return TRUE;
invalid:
    atm_annual_series_candidate_free (candidate);
    return FALSE;
}

static const AnnualPoint *
point_at (const AtmAnnualSeriesCandidate *candidate, guint index)
{
    return candidate != NULL && index < candidate->points->len
        ? g_ptr_array_index (candidate->points, index) : NULL;
}

guint atm_annual_series_candidate_count (const AtmAnnualSeriesCandidate *c)
{ return c != NULL ? c->points->len : 0; }
const char *atm_annual_series_candidate_source_sha256 (const AtmAnnualSeriesCandidate *c)
{ return c != NULL ? c->source_sha256 : NULL; }
guint atm_annual_series_candidate_year (const AtmAnnualSeriesCandidate *c, guint i)
{ const AnnualPoint *p = point_at (c, i); return p != NULL ? p->year : 0; }
guint atm_annual_series_candidate_source_row (const AtmAnnualSeriesCandidate *c, guint i)
{ const AnnualPoint *p = point_at (c, i); return p != NULL ? p->source_row : 0; }
const char *atm_annual_series_candidate_decimal (const AtmAnnualSeriesCandidate *c, guint i)
{ const AnnualPoint *p = point_at (c, i); return p != NULL ? p->decimal : NULL; }
const char *atm_annual_series_candidate_coefficient (const AtmAnnualSeriesCandidate *c, guint i)
{ const AnnualPoint *p = point_at (c, i); return p != NULL ? p->canonical->coefficient : NULL; }
gint64 atm_annual_series_candidate_exponent (const AtmAnnualSeriesCandidate *c, guint i)
{ const AnnualPoint *p = point_at (c, i); return p != NULL ? p->canonical->exponent : 0; }
