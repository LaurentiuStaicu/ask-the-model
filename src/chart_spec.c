#include "chart_spec.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct AtmChartSpec {
    AtmChartKind kind;
    guint count;
    AtmVerifiedSeries *series[ATM_CHART_SPEC_MAX_SERIES];
    guint x_min, x_max;
    AtmScientificDecimal y_min, y_max;
    char *id;
};
static gboolean reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}
void atm_chart_spec_free (AtmChartSpec *s)
{
    if (s == NULL) return;
    for (guint i = 0; i < s->count; i++) atm_verified_series_free (s->series[i]);
    g_free (s->y_min.coefficient); g_free (s->y_max.coefficient);
    g_free (s->id); g_free (s);
}
/* Compare canonical decimals without binary64, powers of ten, or signed
 * exponent arithmetic. Called only on source-validated canonical points. */
static gint compare (const AtmVerifiedPoint *a, const AtmVerifiedPoint *b)
{
    const char *x = a->coefficient, *y = b->coefficient;
    gint sx = !strcmp (x, "0") ? 0 : (*x == '-' ? -1 : 1);
    gint sy = !strcmp (y, "0") ? 0 : (*y == '-' ? -1 : 1);
    if (sx != sy) return sx < sy ? -1 : 1;
    if (sx == 0) return 0;
    if (sx < 0) { x++; y++; }
    gsize nx = strlen (x), ny = strlen (y);
    gint magnitude = 0;
    if (a->exponent >= b->exponent) {
        guint64 delta = (guint64) a->exponent - (guint64) b->exponent;
        if (delta > ny) magnitude = 1;
        else magnitude = nx + delta < ny ? -1 : (nx + delta > ny ? 1 : 0);
    } else {
        guint64 delta = (guint64) b->exponent - (guint64) a->exponent;
        if (delta > nx) magnitude = -1;
        else magnitude = nx < ny + delta ? -1 : (nx > ny + delta ? 1 : 0);
    }
    if (magnitude == 0) {
        for (gsize i = 0; i < MAX (nx, ny); i++) {
            char dx = i < nx ? x[i] : '0', dy = i < ny ? y[i] : '0';
            if (dx != dy) { magnitude = dx < dy ? -1 : 1; break; }
        }
    }
    return sx * magnitude;
}
static void text (JsonBuilder *b, const char *key, const char *value)
{
    json_builder_set_member_name (b, key); json_builder_add_string_value (b, value);
}
static void decimal (JsonBuilder *b, const char *key, const AtmScientificDecimal *d)
{
    json_builder_set_member_name (b, key); json_builder_begin_object (b);
    text (b, "coefficient", d->coefficient);
    char *e = g_strdup_printf ("%" G_GINT64_FORMAT, d->exponent);
    text (b, "exponent", e); g_free (e); json_builder_end_object (b);
}
static gboolean identity (const AtmChartSpec *s, char **out, GError **error)
{
    JsonBuilder *b = json_builder_new (); json_builder_begin_object (b);
    text (b, "schema", ATM_CHART_SPEC_SCHEMA);
    text (b, "series_profile", ATM_VERIFIED_SERIES_PROFILE);
    text (b, "kind", s->kind == ATM_CHART_LINE ? "LINE" : "SCATTER");
    text (b, "x_semantics", "calendar_year");
    text (b, "x_scale", "linear"); text (b, "y_scale", "linear");
    text (b, "missing_policy", "GAP"); text (b, "extent_policy", "exact_data_non_degenerate/1");
    text (b, "unit", atm_verified_series_unit (s->series[0]));
    text (b, "reference_period", atm_verified_series_reference_period (s->series[0]));
    json_builder_set_member_name (b, "x_min"); json_builder_add_int_value (b, s->x_min);
    json_builder_set_member_name (b, "x_max"); json_builder_add_int_value (b, s->x_max);
    decimal (b, "y_min", &s->y_min); decimal (b, "y_max", &s->y_max);
    json_builder_set_member_name (b, "series"); json_builder_begin_array (b);
    for (guint i = 0; i < s->count; i++)
        json_builder_add_string_value (b, atm_verified_series_qualified_id (s->series[i]));
    json_builder_end_array (b); json_builder_end_object (b);
    JsonNode *root = json_builder_get_root (b);
    char *json = json_to_string (root, FALSE);
    gboolean ok = atm_scientific_content_id_json ("atm.chart-spec.v1", json, out, error);
    g_free (json); json_node_free (root); g_object_unref (b); return ok;
}
gboolean atm_chart_spec_new (AtmChartKind kind, const AtmVerifiedSeries *const *input,
    gsize count, AtmChartSpec **out, GError **error)
{
    if (out == NULL || *out != NULL || input == NULL || count == 0 ||
        count > ATM_CHART_SPEC_MAX_SERIES || (kind != ATM_CHART_LINE && kind != ATM_CHART_SCATTER))
        return reject (error, "Unsupported ChartSpec arguments or kind.");
    AtmChartSpec *s = g_new0 (AtmChartSpec, 1); s->kind = kind;
    const AtmVerifiedPoint *low = NULL, *high = NULL;
    s->x_min = G_MAXUINT;
    for (guint i = 0; i < count; i++) {
        if (!atm_verified_series_rebuild (input[i], &s->series[i], error)) goto invalid;
        s->count++;
        /* The only constructor accepts the pinned complete annual profile.
         * Other dimensions/coordinate semantics require a new admission policy. */
        for (guint j = 0; j < i; j++) {
            if (!strcmp (atm_verified_series_qualified_id (s->series[i]),
                         atm_verified_series_qualified_id (s->series[j]))) {
                reject (error, "Duplicate qualified series."); goto invalid;
            }
        }
        if (strcmp (atm_verified_series_unit (s->series[0]), atm_verified_series_unit (s->series[i])) ||
            strcmp (atm_verified_series_reference_period (s->series[0]),
                    atm_verified_series_reference_period (s->series[i]))) {
            reject (error, "Incompatible series axes."); goto invalid;
        }
        for (guint j = 0; j < atm_verified_series_count (s->series[i]); j++) {
            const AtmVerifiedPoint *p = atm_verified_series_point (s->series[i], j);
            s->x_min = MIN (s->x_min, p->calendar_year); s->x_max = MAX (s->x_max, p->calendar_year);
            if (low == NULL || compare (p, low) < 0) low = p;
            if (high == NULL || compare (p, high) > 0) high = p;
        }
    }
    if (low == NULL || s->x_min >= s->x_max || compare (low, high) >= 0) {
        reject (error, "Empty or degenerate data extents."); goto invalid;
    }
    s->y_min.coefficient = g_strdup (low->coefficient); s->y_min.exponent = low->exponent;
    s->y_max.coefficient = g_strdup (high->coefficient); s->y_max.exponent = high->exponent;
    if (!identity (s, &s->id, error)) goto invalid;
    *out = s; return TRUE;
invalid:
    atm_chart_spec_free (s); return FALSE;
}
gboolean atm_chart_spec_validate (const AtmChartSpec *s, GError **error)
{
    if (s == NULL || s->count == 0 || s->count > ATM_CHART_SPEC_MAX_SERIES)
        return reject (error, "Invalid ChartSpec coverage.");
    const AtmVerifiedSeries *inputs[ATM_CHART_SPEC_MAX_SERIES];
    for (guint i = 0; i < s->count; i++) inputs[i] = s->series[i];
    AtmChartSpec *expected = NULL;
    if (!atm_chart_spec_new (s->kind, inputs, s->count, &expected, error)) return FALSE;
    gboolean matches = s->x_min == expected->x_min && s->x_max == expected->x_max &&
        s->y_min.exponent == expected->y_min.exponent && s->y_max.exponent == expected->y_max.exponent &&
        g_strcmp0 (s->y_min.coefficient, expected->y_min.coefficient) == 0 &&
        g_strcmp0 (s->y_max.coefficient, expected->y_max.coefficient) == 0 &&
        g_strcmp0 (s->id, expected->id) == 0;
    atm_chart_spec_free (expected);
    return matches || reject (error, "ChartSpec disagrees with reconstructed series.");
}
const char *atm_chart_spec_id (const AtmChartSpec *s) { return s ? s->id : NULL; }
AtmChartKind atm_chart_spec_kind (const AtmChartSpec *s) { return s ? s->kind : 0; }
guint atm_chart_spec_count (const AtmChartSpec *s) { return s ? s->count : 0; }
const AtmVerifiedSeries *atm_chart_spec_series (const AtmChartSpec *s, guint i)
{ return s && i < s->count ? s->series[i] : NULL; }
guint atm_chart_spec_x_min (const AtmChartSpec *s) { return s ? s->x_min : 0; }
guint atm_chart_spec_x_max (const AtmChartSpec *s) { return s ? s->x_max : 0; }
const AtmScientificDecimal *atm_chart_spec_y_min (const AtmChartSpec *s) { return s ? &s->y_min : NULL; }
const AtmScientificDecimal *atm_chart_spec_y_max (const AtmChartSpec *s) { return s ? &s->y_max : NULL; }

gboolean atm_chart_spec_rebuild (const AtmChartSpec *s, AtmChartSpec **out, GError **error)
{
    if (out == NULL || *out != NULL) return reject (error, "Invalid ChartSpec rebuild output.");
    if (!atm_chart_spec_validate (s, error)) return FALSE;
    const AtmVerifiedSeries *inputs[ATM_CHART_SPEC_MAX_SERIES];
    for (guint i = 0; i < s->count; i++) inputs[i] = s->series[i];
    return atm_chart_spec_new (s->kind, inputs, s->count, out, error);
}
