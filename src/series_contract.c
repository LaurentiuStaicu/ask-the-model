#include "series_contract.h"
#include "scientific_canonical.h"
#include <json-glib/json-glib.h>

#include <errno.h>
#include <string.h>

static gboolean fail (GError **error, AtmSeriesContractError code, const char *message)
{
    g_set_error_literal (error, ATM_SERIES_CONTRACT_ERROR, code, message);
    return FALSE;
}

GQuark
atm_series_contract_error_quark (void)
{
    return g_quark_from_static_string ("atm-series-contract-error-quark");
}

static gboolean bounded (const char *text, gsize max)
{
    if (text == NULL) return FALSE;
    gsize n = 0;
    while (n <= max && text[n] != '\0') n++;
    return n > 0 && n <= max && g_utf8_validate (text, (gssize) n, NULL);
}

static gboolean hex64 (const char *text)
{
    if (!bounded (text, 64) || strlen (text) != 64) return FALSE;
    for (guint i = 0; i < 64; i++)
        if (!g_ascii_isxdigit (text[i]) || g_ascii_isupper (text[i]))
            return FALSE;
    return TRUE;
}

static void
point_destroy (gpointer data)
{
    atm_series_contract_point_free (data);
}

void
atm_series_contract_point_free (AtmSeriesContractPoint *point)
{
    if (point == NULL) return;
    g_free (point->x);
    g_free (point->source_decimal);
    g_free (point->coefficient);
    g_free (point->missing_reason);
    g_free (point->break_reason);
    g_clear_pointer (&point->support, g_ptr_array_unref);
    g_free (point);
}

void
atm_series_contract_free (AtmSeriesContract *series)
{
    if (series == NULL) return;
    g_free (series->subject);
    g_free (series->attribute);
    g_free (series->x_unit);
    g_free (series->x_dimension);
    g_free (series->y_unit);
    g_free (series->y_dimension);
    g_free (series->series_semantics);
    g_free (series->scenario);
    g_free (series->time_scope);
    g_free (series->epistemic_status);
    g_free (series->repository_id);
    g_free (series->repository_version);
    g_free (series->snapshot_sha);
    g_free (series->source_path);
    g_free (series->profile_id);
    g_free (series->profile_version);
    g_free (series->scientific_content_id);
    g_free (series->qualified_series_id);
    g_clear_pointer (&series->points, g_ptr_array_unref);
    g_free (series);
}

AtmSeriesContract *
atm_series_contract_new (
    AtmSeriesXKind x_kind,
    const char *subject,
    const char *attribute,
    const char *x_unit,
    const char *x_dimension,
    const char *y_unit,
    const char *y_dimension,
    const char *series_semantics,
    const char *scenario,
    const char *time_scope,
    const char *epistemic_status,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path,
    const char *profile_id,
    const char *profile_version,
    GError **error)
{
    if (x_kind < ATM_SERIES_X_CALENDAR_YEAR || x_kind > ATM_SERIES_X_EVENT_ORDINAL ||
        !bounded (subject, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (attribute, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (x_unit, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (x_dimension, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (y_unit, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (y_dimension, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (series_semantics, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (scenario, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (time_scope, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (epistemic_status, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (repository_id, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (repository_version, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (snapshot_sha, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (source_path, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (profile_id, ATM_SERIES_CONTRACT_MAX_TEXT) ||
        !bounded (profile_version, ATM_SERIES_CONTRACT_MAX_TEXT))
        { fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Invalid or oversized series metadata."); return NULL; }

    AtmSeriesContract *series = g_new0 (AtmSeriesContract, 1);
    series->x_kind = x_kind;
    series->subject = g_strdup (subject);
    series->attribute = g_strdup (attribute);
    series->x_unit = g_strdup (x_unit);
    series->x_dimension = g_strdup (x_dimension);
    series->y_unit = g_strdup (y_unit);
    series->y_dimension = g_strdup (y_dimension);
    series->series_semantics = g_strdup (series_semantics);
    series->scenario = g_strdup (scenario);
    series->time_scope = g_strdup (time_scope);
    series->epistemic_status = g_strdup (epistemic_status);
    series->repository_id = g_strdup (repository_id);
    series->repository_version = g_strdup (repository_version);
    series->snapshot_sha = g_strdup (snapshot_sha);
    series->source_path = g_strdup (source_path);
    series->profile_id = g_strdup (profile_id);
    series->profile_version = g_strdup (profile_version);
    series->points = g_ptr_array_new_with_free_func (point_destroy);
    return series;
}

static gboolean
x_valid (AtmSeriesXKind kind, const char *x)
{
    if (!bounded (x, ATM_SERIES_CONTRACT_MAX_X)) return FALSE;
    if (kind == ATM_SERIES_X_UTC_INSTANT) {
        /* Canonical UTC instant: YYYY-MM-DDTHH:MM:SSZ, with real calendar/time fields. */
        if (strlen (x) != 20 || x[4] != '-' || x[7] != '-' ||
            x[10] != 'T' || x[13] != ':' || x[16] != ':' || x[19] != 'Z')
            return FALSE;
        for (guint i = 0; i < 20; i++)
            if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && i != 19 &&
                !g_ascii_isdigit (x[i])) return FALSE;
        GDateTime *dt = g_date_time_new_from_iso8601 (x);
        if (dt == NULL) return FALSE;
        char *canonical = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%SZ");
        gboolean matches = g_strcmp0 (canonical, x) == 0;
        g_free (canonical);
        g_date_time_unref (dt);
        return matches;
    }
    if (kind == ATM_SERIES_X_EVENT_ORDINAL) {
        if (x[0] == '0' && x[1] != '\0') return FALSE;
        for (const char *p = x; *p != '\0'; p++)
            if (!g_ascii_isdigit (*p)) return FALSE;
        errno = 0;
        char *end = NULL;
        (void) g_ascii_strtoull (x, &end, 10);
        return x[0] != '\0' && end != NULL && *end == '\0' && errno != ERANGE;
    }
    if (kind == ATM_SERIES_X_CALENDAR_YEAR) {
        gsize n = strlen (x);
        if (n < 1 || n > 6) return FALSE;
        gsize start = x[0] == '-' ? 1 : 0;
        if (start == n) return FALSE;
        if (x[start] == '0' && n - start > 1) return FALSE;
        for (gsize i = start; i < n; i++)
            if (!g_ascii_isdigit (x[i])) return FALSE;
        return TRUE;
    }
    return FALSE;
}

static gboolean
point_numeric_valid (const AtmSeriesContractPoint *p, GError **error)
{
    if (p->y_status != ATM_SERIES_Y_NUMERIC)
        return TRUE;
    if (!bounded (p->source_decimal, ATM_SERIES_CONTRACT_MAX_NUMERIC) ||
        !bounded (p->coefficient, ATM_SERIES_CONTRACT_MAX_NUMERIC))
        return fail (error, ATM_SERIES_CONTRACT_ERROR_LIMIT, "Numeric representation exceeds ceiling.");
    AtmScientificDecimal *decimal = NULL;
    if (!atm_scientific_decimal_parse (p->source_decimal, &decimal, error))
        return fail (error, ATM_SERIES_CONTRACT_ERROR_NUMERIC, "Invalid source decimal.");
    gboolean matches = g_strcmp0 (decimal->coefficient, p->coefficient) == 0 &&
                       decimal->exponent == p->exponent;
    atm_scientific_decimal_free (decimal);
    if (!matches)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_NUMERIC, "Decimal coefficient/exponent mismatch.");
    if (p->negative_zero) {
        if (g_strcmp0 (p->coefficient, "0") != 0 || p->exponent != 0 ||
            p->source_decimal[0] != '-')
            return fail (error, ATM_SERIES_CONTRACT_ERROR_NUMERIC, "Invalid signed-zero representation.");
    }
    return TRUE;
}

gboolean
atm_series_contract_add_point (
    AtmSeriesContract *series,
    const char *x,
    AtmSeriesYStatus y_status,
    const char *source_decimal,
    const char *coefficient,
    gint64 exponent,
    gboolean negative_zero,
    gboolean break_before,
    const char *missing_reason,
    const char *break_reason,
    const char *const *support,
    gsize support_count,
    GError **error)
{
    if (series == NULL || !x_valid (series->x_kind, x) ||
        (y_status != ATM_SERIES_Y_NUMERIC && y_status != ATM_SERIES_Y_MISSING) ||
        support == NULL || support_count == 0 || support_count > ATM_SERIES_CONTRACT_MAX_SUPPORT)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_ARGUMENT, "Invalid series point arguments.");
    if (series->points->len >= ATM_SERIES_CONTRACT_MAX_POINTS)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_LIMIT, "Series point ceiling exceeded.");

    AtmSeriesContractPoint *p = g_new0 (AtmSeriesContractPoint, 1);
    p->order = series->points->len;
    p->x = g_strdup (x);
    p->y_status = y_status;
    p->source_decimal = g_strdup (source_decimal);
    p->coefficient = g_strdup (coefficient);
    p->exponent = exponent;
    p->negative_zero = negative_zero;
    p->break_before = break_before;
    p->missing_reason = g_strdup (missing_reason);
    p->break_reason = g_strdup (break_reason);
    p->support = g_ptr_array_new_with_free_func (g_free);
    for (gsize i = 0; i < support_count; i++) {
        if (!hex64 (support[i])) {
            atm_series_contract_point_free (p);
            return fail (error, ATM_SERIES_CONTRACT_ERROR_SUPPORT, "Point support must be lowercase SHA-256 identities.");
        }
        g_ptr_array_add (p->support, g_strdup (support[i]));
    }
    g_ptr_array_add (series->points, p);
    return TRUE;
}

static void text_member (JsonBuilder *b, const char *key, const char *value)
{
    json_builder_set_member_name (b, key);
    if (value != NULL) json_builder_add_string_value (b, value);
    else json_builder_add_null_value (b);
}

static void strings_member (JsonBuilder *b, const char *key, const GPtrArray *values)
{
    json_builder_set_member_name (b, key);
    json_builder_begin_array (b);
    for (guint i = 0; i < values->len; i++)
        json_builder_add_string_value (b, g_ptr_array_index (values, i));
    json_builder_end_array (b);
}

static gboolean
build_scientific_json (const AtmSeriesContract *s, char **out, GError **error)
{
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    text_member (b, "schema", ATM_SERIES_CONTRACT_SCHEMA);
    text_member (b, "x_kind",
        s->x_kind == ATM_SERIES_X_CALENDAR_YEAR ? "calendar_year" :
        s->x_kind == ATM_SERIES_X_UTC_INSTANT ? "utc_instant" : "event_ordinal");
    text_member (b, "subject", s->subject);
    text_member (b, "attribute", s->attribute);
    text_member (b, "x_unit", s->x_unit);
    text_member (b, "x_dimension", s->x_dimension);
    text_member (b, "y_unit", s->y_unit);
    text_member (b, "y_dimension", s->y_dimension);
    text_member (b, "series_semantics", s->series_semantics);
    text_member (b, "scenario", s->scenario);
    text_member (b, "time_scope", s->time_scope);
    text_member (b, "epistemic_status", s->epistemic_status);
    json_builder_set_member_name (b, "points");
    json_builder_begin_array (b);
    for (guint i = 0; i < s->points->len; i++) {
        const AtmSeriesContractPoint *p = g_ptr_array_index (s->points, i);
        json_builder_begin_object (b);
        json_builder_set_member_name (b, "order");
        json_builder_add_int_value (b, p->order);
        text_member (b, "x", p->x);
        json_builder_set_member_name (b, "y_status");
        json_builder_add_int_value (b, p->y_status);
        text_member (b, "source_decimal", p->source_decimal);
        text_member (b, "coefficient", p->coefficient);
        json_builder_set_member_name (b, "exponent");
        json_builder_add_int_value (b, p->exponent);
        json_builder_set_member_name (b, "negative_zero");
        json_builder_add_boolean_value (b, p->negative_zero);
        json_builder_set_member_name (b, "break_before");
        json_builder_add_boolean_value (b, p->break_before);
        text_member (b, "missing_reason", p->missing_reason);
        text_member (b, "break_reason", p->break_reason);
        strings_member (b, "support", p->support);
        json_builder_end_object (b);
    }
    json_builder_end_array (b);
    json_builder_end_object (b);
    JsonNode *root = json_builder_get_root (b);
    *out = json_to_string (root, FALSE);
    json_node_free (root);
    g_object_unref (b);
    if (*out == NULL) return fail (error, ATM_SERIES_CONTRACT_ERROR_IDENTITY, "Could not serialize scientific series identity.");
    return TRUE;
}

static gboolean
build_qualified_json (const AtmSeriesContract *s, const char *scientific_id, char **out, GError **error)
{
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    text_member (b, "schema", "atm-verified-series-qualification/1");
    text_member (b, "scientific_content_id", scientific_id);
    text_member (b, "repository_id", s->repository_id);
    text_member (b, "repository_version", s->repository_version);
    text_member (b, "snapshot_sha", s->snapshot_sha);
    text_member (b, "source_path", s->source_path);
    text_member (b, "profile_id", s->profile_id);
    text_member (b, "profile_version", s->profile_version);
    json_builder_end_object (b);
    JsonNode *root = json_builder_get_root (b);
    *out = json_to_string (root, FALSE);
    json_node_free (root);
    g_object_unref (b);
    if (*out == NULL) return fail (error, ATM_SERIES_CONTRACT_ERROR_IDENTITY, "Could not serialize qualified series identity.");
    return TRUE;
}

static gboolean
support_is_qualified (const AtmSraResult *q, const char *id)
{
    for (guint i = 0; i < q->qualification->evidence_atom_ids->len; i++)
        if (g_strcmp0 (id, g_ptr_array_index (q->qualification->evidence_atom_ids, i)) == 0)
            return TRUE;
    return FALSE;
}

static gint
compare_x (AtmSeriesXKind kind, const char *a, const char *b)
{
    if (kind == ATM_SERIES_X_CALENDAR_YEAR) {
        gint64 ai = g_ascii_strtoll (a, NULL, 10);
        gint64 bi = g_ascii_strtoll (b, NULL, 10);
        return ai < bi ? -1 : ai > bi ? 1 : 0;
    }
    if (kind == ATM_SERIES_X_EVENT_ORDINAL) {
        guint64 ai = g_ascii_strtoull (a, NULL, 10);
        guint64 bi = g_ascii_strtoull (b, NULL, 10);
        return ai < bi ? -1 : ai > bi ? 1 : 0;
    }
    return strcmp (a, b);
}

gboolean
atm_series_contract_validate (AtmSeriesContract *s, const AtmSraResult *q, GError **error)
{
    if (s == NULL || q == NULL || !q->finalized || q->qualification == NULL ||
        q->qualification->evidence_atom_ids == NULL)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_SUPPORT, "Series requires finalized qualification.");
    if (!atm_sra_result_validate (q, error))
        return FALSE;
    if (s->points == NULL || s->points->len == 0 || s->points->len > ATM_SERIES_CONTRACT_MAX_POINTS)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_LIMIT, "Invalid series point count.");
    if (s->x_kind == ATM_SERIES_X_CALENDAR_YEAR && g_strcmp0 (s->x_unit, "calendar_year") != 0)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Calendar-year X requires calendar_year unit.");
    if (s->x_kind == ATM_SERIES_X_UTC_INSTANT && g_strcmp0 (s->x_unit, "UTC") != 0)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "UTC-instant X requires UTC unit.");
    if (s->x_kind == ATM_SERIES_X_EVENT_ORDINAL && g_strcmp0 (s->x_unit, "event") != 0)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Event ordinal X requires event unit.");

    guint numeric = 0, missing = 0;
    const AtmSeriesContractPoint *previous = NULL;
    for (guint i = 0; i < s->points->len; i++) {
        AtmSeriesContractPoint *p = g_ptr_array_index (s->points, i);
        if (p == NULL || p->order != i || !x_valid (s->x_kind, p->x))
            return fail (error, ATM_SERIES_CONTRACT_ERROR_ORDER, "Invalid X ordering or point order.");
        if (previous != NULL && compare_x (s->x_kind, previous->x, p->x) >= 0)
            return fail (error, ATM_SERIES_CONTRACT_ERROR_ORDER, "X values must be strictly increasing.");
        if (p->support == NULL || p->support->len == 0 || p->support->len > ATM_SERIES_CONTRACT_MAX_SUPPORT)
            return fail (error, ATM_SERIES_CONTRACT_ERROR_SUPPORT, "Point support is missing or oversized.");
        for (guint k = 0; k < p->support->len; k++)
            if (!support_is_qualified (q, g_ptr_array_index (p->support, k)))
                return fail (error, ATM_SERIES_CONTRACT_ERROR_SUPPORT, "Point support is not in finalized qualification.");
        if (p->y_status == ATM_SERIES_Y_NUMERIC) {
            if (p->missing_reason != NULL || (p->break_before && p->break_reason == NULL))
                return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Numeric point has invalid missing/break semantics.");
            if (!point_numeric_valid (p, error)) return FALSE;
            numeric++;
        } else if (p->y_status == ATM_SERIES_Y_MISSING) {
            if (!bounded (p->missing_reason, ATM_SERIES_CONTRACT_MAX_TEXT))
                return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "MISSING requires a bounded reason.");
            if (p->source_decimal != NULL || p->coefficient != NULL || p->negative_zero)
                return fail (error, ATM_SERIES_CONTRACT_ERROR_NUMERIC, "MISSING cannot carry numeric value.");
            missing++;
        } else {
            return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Unknown Y status.");
        }
        if (p->break_before && !bounded (p->break_reason, ATM_SERIES_CONTRACT_MAX_TEXT))
            return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Break requires a bounded reason.");
        if (p->break_before && i == 0)
            return fail (error, ATM_SERIES_CONTRACT_ERROR_ORDER, "First point cannot begin with a discontinuity.");
        previous = p;
    }
    if (numeric == 0 && missing == 0)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_SHAPE, "Series contains no points.");
    if (s->scientific_content_id != NULL || s->qualified_series_id != NULL)
        return fail (error, ATM_SERIES_CONTRACT_ERROR_IDENTITY, "Caller identity strings are not authoritative.");
    char *scientific_json = NULL;
    if (!build_scientific_json (s, &scientific_json, error)) return FALSE;
    if (!atm_scientific_content_id_json ("atm.verified-series.v1", scientific_json,
                                         &s->scientific_content_id, error)) {
        g_free (scientific_json); return FALSE;
    }
    g_free (scientific_json);
    char *qualified_json = NULL;
    if (!build_qualified_json (s, s->scientific_content_id, &qualified_json, error)) return FALSE;
    gboolean ok = atm_scientific_content_id_json (
        "atm.verified-series.qualification.v1", qualified_json, &s->qualified_series_id, error);
    g_free (qualified_json);
    return ok;
}
const char *atm_series_contract_scientific_id (const AtmSeriesContract *s)
{ return s != NULL ? s->scientific_content_id : NULL; }
const char *atm_series_contract_qualified_id (const AtmSeriesContract *s)
{ return s != NULL ? s->qualified_series_id : NULL; }
