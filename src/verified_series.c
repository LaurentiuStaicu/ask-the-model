#include "verified_series.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct AtmVerifiedSeries {
    AtmGistempAdmission *admission;
    AtmGistempSra *sra;
    GPtrArray *points;
    char *scientific_id;
    char *qualified_id;
};
static gboolean reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}
static void point_free (gpointer data)
{
    AtmVerifiedPoint *p = data;
    g_free (p->source_decimal);
    g_free (p->coefficient);
    g_free (p->missing_reason);
    g_free (p->break_reason);
    g_ptr_array_unref (p->support);
    g_free (p);
}
void atm_verified_series_free (AtmVerifiedSeries *s)
{
    if (s == NULL) return;
    atm_gistemp_admission_free (s->admission);
    atm_gistemp_sra_free (s->sra);
    g_ptr_array_unref (s->points);
    g_free (s->scientific_id);
    g_free (s->qualified_id);
    g_free (s);
}
static void text_member (JsonBuilder *b, const char *key, const char *value)
{
    json_builder_set_member_name (b, key);
    if (value != NULL) json_builder_add_string_value (b, value);
    else json_builder_add_null_value (b);
}
static void integer_member (JsonBuilder *b, const char *key, guint value)
{
    json_builder_set_member_name (b, key);
    json_builder_add_int_value (b, value);
}
static char *finish (JsonBuilder *b)
{
    json_builder_end_object (b);
    JsonNode *root = json_builder_get_root (b);
    char *json = json_to_string (root, FALSE);
    json_node_free (root);
    g_object_unref (b);
    return json;
}
static char *scientific_json (const AtmVerifiedSeries *s)
{
    JsonBuilder *b = json_builder_new ();
    const AtmGistempAdmission *a = s->admission;
    json_builder_begin_object (b);
    text_member (b, "schema", ATM_VERIFIED_SERIES_SCHEMA);
    text_member (b, "series_semantics", "empirical_annual_observations");
    text_member (b, "subject", atm_gistemp_admission_subject (a));
    text_member (b, "attribute", atm_gistemp_admission_attribute (a));
    text_member (b, "x_semantics", "calendar_year");
    text_member (b, "x_unit", "calendar_year");
    text_member (b, "x_dimension", "time");
    text_member (b, "y_unit", atm_gistemp_admission_unit (a));
    text_member (b, "y_dimension", "temperature_difference");
    text_member (b, "reference_period", atm_gistemp_admission_reference_period (a));
    text_member (b, "selection", atm_gistemp_admission_selection (a));
    text_member (b, "access_vintage", atm_gistemp_admission_vintage (a));
    text_member (b, "epistemic_status", atm_gistemp_admission_epistemic_status (a));
    text_member (b, "scenario", "not_applicable");
    text_member (b, "numeric_profile", "exact_decimal_coefficient_exponent/1");
    text_member (b, "completeness", "complete_annual_only");
    json_builder_set_member_name (b, "points");
    json_builder_begin_array (b);
    for (guint i = 0; i < s->points->len; i++) {
        const AtmVerifiedPoint *p = g_ptr_array_index (s->points, i);
        json_builder_begin_object (b);
        integer_member (b, "order", p->order);
        integer_member (b, "calendar_year", p->calendar_year);
        text_member (b, "y_status", p->y_status == ATM_VERIFIED_Y_NUMERIC ? "NUMERIC" : "MISSING");
        text_member (b, "coefficient", p->coefficient);
        char *exponent = g_strdup_printf ("%" G_GINT64_FORMAT, p->exponent);
        text_member (b, "exponent", exponent);
        g_free (exponent);
        json_builder_set_member_name (b, "break_before");
        json_builder_add_boolean_value (b, p->break_before);
        text_member (b, "missing_reason", p->missing_reason);
        text_member (b, "break_reason", p->break_reason);
        json_builder_end_object (b);
    }
    json_builder_end_array (b);
    return finish (b);
}
static void strings_member (JsonBuilder *b, const char *name, const GPtrArray *values)
{
    json_builder_set_member_name (b, name);
    json_builder_begin_array (b);
    for (guint i = 0; i < values->len; i++)
        json_builder_add_string_value (b, g_ptr_array_index (values, i));
    json_builder_end_array (b);
}
static char *qualified_json (const AtmVerifiedSeries *s, const char *scientific_id)
{
    JsonBuilder *b = json_builder_new ();
    const AtmSraResult *r = atm_gistemp_sra_result (s->sra);
    json_builder_begin_object (b);
    text_member (b, "schema", "atm-verified-series-qualification/1");
    text_member (b, "scientific_content_id", scientific_id);
    text_member (b, "series_profile", ATM_VERIFIED_SERIES_PROFILE);
    text_member (b, "admission_profile", ATM_GISTEMP_ADMISSION_PROFILE);
    text_member (b, "repository", atm_gistemp_admission_repository (s->admission));
    text_member (b, "snapshot", atm_gistemp_admission_snapshot (s->admission));
    text_member (b, "source_path", atm_gistemp_admission_source_path (s->admission, 0));
    text_member (b, "x_column", "year");
    text_member (b, "y_column", "temperature_anomaly_c_1951_1980");
    text_member (b, "sra_qualified_id", r->qualification->qualified_artifact_id);
    strings_member (b, "aggregate_support", r->qualification->evidence_atom_ids);
    json_builder_set_member_name (b, "point_bindings");
    json_builder_begin_array (b);
    for (guint i = 0; i < s->points->len; i++) {
        const AtmVerifiedPoint *p = g_ptr_array_index (s->points, i);
        json_builder_begin_object (b);
        integer_member (b, "order", p->order);
        integer_member (b, "source_row", p->source_row);
        text_member (b, "source_decimal", p->source_decimal);
        strings_member (b, "support", p->support);
        json_builder_end_object (b);
    }
    json_builder_end_array (b);
    return finish (b);
}
static gboolean identities (const AtmVerifiedSeries *s, char **scientific_id,
    char **qualified_id, GError **error)
{
    char *content = scientific_json (s);
    gboolean ok = atm_scientific_content_id_json ("atm.verified-series.v1", content, scientific_id, error);
    g_free (content);
    if (!ok) return FALSE;
    char *qualification = qualified_json (s, *scientific_id);
    ok = atm_scientific_content_id_json ("atm.verified-series.qualification.v1", qualification, qualified_id, error);
    g_free (qualification);
    if (!ok) g_clear_pointer (scientific_id, g_free);
    return ok;
}
gboolean atm_verified_series_from_gistemp (const AtmGistempAdmission *admission,
    AtmVerifiedSeries **out, GError **error)
{
    if (admission == NULL || out == NULL || *out != NULL)
        return reject (error, "Invalid verified-series arguments.");
    AtmVerifiedSeries *s = g_new0 (AtmVerifiedSeries, 1);
    s->points = g_ptr_array_new_with_free_func (point_free);
    if (!atm_gistemp_admission_rebuild (admission, &s->admission, error) ||
        !atm_gistemp_sra_new (s->admission, &s->sra, error) ||
        !atm_gistemp_sra_validate (s->sra, error)) goto invalid;
    const AtmAnnualSeriesCandidate *c = atm_gistemp_admission_candidate (s->admission);
    const AtmSraResult *r = atm_gistemp_sra_result (s->sra);
    if (atm_annual_series_candidate_count (c) != 146 || r->established_facts->len != 146 ||
        r->qualification->evidence_atom_ids->len != 150) {
        reject (error, "Unsupported verified-series coverage.");
        goto invalid;
    }
    for (guint i = 0; i < 146; i++) {
        const AtmSraEstablishedFact *f = g_ptr_array_index (r->established_facts, i);
        guint year = atm_annual_series_candidate_year (c, i);
        char *fact_id = g_strdup_printf ("ewd:gistemp-annual:%u", year);
        char *value = g_strdup_printf ("%se%" G_GINT64_FORMAT,
            atm_annual_series_candidate_coefficient (c, i), atm_annual_series_candidate_exponent (c, i));
        gboolean matches = g_strcmp0 (f->fact_id, fact_id) == 0 &&
                           g_strcmp0 (f->value, value) == 0 && f->support->len == 5;
        g_free (fact_id); g_free (value);
        if (!matches) { reject (error, "Source/SRA point mapping mismatch."); goto invalid; }
        AtmVerifiedPoint *p = g_new0 (AtmVerifiedPoint, 1);
        p->order = i;
        p->calendar_year = year;
        p->source_row = atm_annual_series_candidate_source_row (c, i);
        p->y_status = ATM_VERIFIED_Y_NUMERIC;
        p->source_decimal = g_strdup (atm_annual_series_candidate_decimal (c, i));
        p->coefficient = g_strdup (atm_annual_series_candidate_coefficient (c, i));
        p->exponent = atm_annual_series_candidate_exponent (c, i);
        p->support = g_ptr_array_new_with_free_func (g_free);
        for (guint j = 0; j < f->support->len; j++)
            g_ptr_array_add (p->support, g_strdup (g_ptr_array_index (f->support, j)));
        g_ptr_array_add (s->points, p);
    }
    if (!identities (s, &s->scientific_id, &s->qualified_id, error)) goto invalid;
    *out = s;
    return TRUE;
invalid:
    atm_verified_series_free (s);
    return FALSE;
}
static gboolean bounded_text (const char *text, gsize maximum)
{
    if (text == NULL) return FALSE;
    gsize n = 0;
    while (n <= maximum && text[n] != '\0') n++;
    return n > 0 && n <= maximum && g_utf8_validate (text, (gssize) n, NULL);
}
static gboolean shape_valid (const AtmVerifiedSeries *s, GError **error)
{
    if (s == NULL || s->points->len != 146)
        return reject (error, "Invalid verified-series coverage.");
    for (guint i = 0; i < s->points->len; i++) {
        const AtmVerifiedPoint *p = g_ptr_array_index (s->points, i);
        if (p == NULL || p->order != i || p->calendar_year != 1880 + i ||
            p->source_row != i + 2 || p->y_status != ATM_VERIFIED_Y_NUMERIC ||
            p->break_before || p->missing_reason != NULL || p->break_reason != NULL ||
             !bounded_text (p->coefficient, ATM_ANNUAL_SERIES_MAX_DECIMAL_BYTES) ||
            !bounded_text (p->source_decimal, ATM_ANNUAL_SERIES_MAX_DECIMAL_BYTES) || p->support == NULL ||
            p->support->len != 5)
            return reject (error, "Unsupported or inconsistent complete annual point.");
        for (guint j = 0; j < p->support->len; j++) {
            const char *id = g_ptr_array_index (p->support, j);
            if (!bounded_text (id, 64) || strlen (id) != 64)
                return reject (error, "Invalid point support identity.");
            for (guint k = 0; k < 64; k++)
                if (!g_ascii_isdigit (id[k]) && !(id[k] >= 'a' && id[k] <= 'f'))
                    return reject (error, "Invalid point support encoding.");
        }
    }
    return TRUE;
}
gboolean atm_verified_series_validate (const AtmVerifiedSeries *s, GError **error)
{
    if (!shape_valid (s, error) || !atm_gistemp_sra_validate (s->sra, error)) return FALSE;
    char *content_id = NULL, *qualified_id = NULL;
    if (!identities (s, &content_id, &qualified_id, error)) return FALSE;
    gboolean consistent = g_strcmp0 (content_id, s->scientific_id) == 0 &&
                          g_strcmp0 (qualified_id, s->qualified_id) == 0;
    g_free (content_id); g_free (qualified_id);
    if (!consistent) return reject (error, "Verified-series identity mismatch.");
    AtmVerifiedSeries *expected = NULL;
    if (!atm_verified_series_from_gistemp (s->admission, &expected, error)) return FALSE;
    gboolean matches = g_strcmp0 (s->scientific_id, expected->scientific_id) == 0 &&
                       g_strcmp0 (s->qualified_id, expected->qualified_id) == 0;
    atm_verified_series_free (expected);
    return matches || reject (error, "Verified series disagrees with reconstructed source.");
}
guint atm_verified_series_count (const AtmVerifiedSeries *s)
{ return s != NULL ? s->points->len : 0; }
const AtmVerifiedPoint *atm_verified_series_point (const AtmVerifiedSeries *s, guint i)
{ return s != NULL && i < s->points->len ? g_ptr_array_index (s->points, i) : NULL; }
const char *atm_verified_series_scientific_id (const AtmVerifiedSeries *s)
{ return s != NULL ? s->scientific_id : NULL; }
const char *atm_verified_series_qualified_id (const AtmVerifiedSeries *s)
{ return s != NULL ? s->qualified_id : NULL; }
const char *atm_verified_series_unit (const AtmVerifiedSeries *s)
{ return s != NULL ? atm_gistemp_admission_unit (s->admission) : NULL; }
const char *atm_verified_series_reference_period (const AtmVerifiedSeries *s)
{ return s != NULL ? atm_gistemp_admission_reference_period (s->admission) : NULL; }
guint atm_verified_series_support_count (const AtmVerifiedSeries *s)
{ return s != NULL ? atm_gistemp_sra_result (s->sra)->qualification->evidence_atom_ids->len : 0; }
const char *atm_verified_series_support (const AtmVerifiedSeries *s, guint i)
{
    if (s == NULL) return NULL;
    const GPtrArray *ids = atm_gistemp_sra_result (s->sra)->qualification->evidence_atom_ids;
    return i < ids->len ? g_ptr_array_index (ids, i) : NULL;
}

#ifdef ATM_VERIFIED_SERIES_TESTING
/* Fault-injection seam, compiled exclusively into the qualification test.
 * Rehashing is intentionally not source qualification. Never expose in app API. */
gboolean atm_verified_series_test_rehash (AtmVerifiedSeries *s, GError **error)
{
    if (!shape_valid (s, error)) return FALSE;
    g_clear_pointer (&s->scientific_id, g_free);
    g_clear_pointer (&s->qualified_id, g_free);
    return identities (s, &s->scientific_id, &s->qualified_id, error);
}
#endif

gboolean atm_verified_series_rebuild (const AtmVerifiedSeries *s, AtmVerifiedSeries **out, GError **error)
{
    if (out == NULL || *out != NULL) return reject (error, "Invalid rebuild output.");
    if (!atm_verified_series_validate (s, error)) return FALSE;
    return atm_verified_series_from_gistemp (s->admission, out, error);
}
