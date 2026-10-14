#include "chart_spec.h"
#include "energy_institute_verified_series.h"
#include "energy_institute_admission.h"
#include "bnr_bls_verified_series.h"
#include "series_contract.h"
#include "scientific_canonical.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct AtmChartSpec {
    AtmChartSpecVersion version;
    AtmChartKind kind;
    guint count;
    AtmVerifiedSeries *series[ATM_CHART_SPEC_MAX_SERIES];
    AtmEnergyInstituteVerifiedSeries *ei_series;
    AtmBnrBlsVerifiedSeries *bnr_series;
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
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        atm_energy_institute_verified_series_free (s->ei_series);
    else if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        atm_bnr_bls_verified_series_free (s->bnr_series);
    else
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
    AtmChartSpec *s = g_new0 (AtmChartSpec, 1);
    s->version = ATM_CHART_SPEC_VERSION_GISTEMP_V1;
    s->kind = kind;
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

/* Compare exact contract decimals using the same integer-only ordering used
 * by the legacy v1 path; no binary64 conversion or unit reinterpretation. */
static gint compare_contract_points (const AtmSeriesContractPoint *a,
                                     const AtmSeriesContractPoint *b)
{
    AtmVerifiedPoint left = {0}, right = {0};
    left.coefficient = (char *) a->coefficient;
    left.exponent = a->exponent;
    right.coefficient = (char *) b->coefficient;
    right.exponent = b->exponent;
    return compare (&left, &right);
}

static gboolean contract_year (const AtmSeriesContractPoint *p, guint *year)
{
    if (p == NULL || p->x == NULL || year == NULL || p->x[0] == '\0')
        return FALSE;
    char *end = NULL;
    guint64 parsed = g_ascii_strtoull (p->x, &end, 10);
    if (end == p->x || end == NULL || *end != '\0' || parsed > G_MAXUINT)
        return FALSE;
    *year = (guint) parsed;
    return TRUE;
}

static gboolean identity_v2 (const AtmChartSpec *s, char **out, GError **error)
{
    const AtmSeriesContract *c =
        atm_energy_institute_verified_series_contract (s->ei_series);
    if (c == NULL)
        return reject (error, "Energy Institute ChartSpec has no source-bound contract.");

    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    text (b, "schema", ATM_CHART_SPEC_V2_SCHEMA);
    text (b, "identity_domain", "atm.chart-spec.v2");
    text (b, "kind", s->kind == ATM_CHART_LINE ? "LINE" : "SCATTER");
    text (b, "series_profile", c->profile_id);
    text (b, "series_profile_version", c->profile_version);
    text (b, "admission_profile", ATM_EI_ADMISSION_PROFILE);
    text (b, "repository_id", c->repository_id);
    text (b, "repository_version", c->repository_version);
    text (b, "snapshot_sha", c->snapshot_sha);
    text (b, "source_path", c->source_path);
    text (b, "scientific_series_id", c->scientific_content_id);
    text (b, "qualified_series_id", c->qualified_series_id);
    text (b, "subject", c->subject);
    text (b, "attribute", c->attribute);
    text (b, "series_semantics", c->series_semantics);
    text (b, "scenario", c->scenario);
    text (b, "time_scope", c->time_scope);
    text (b, "epistemic_status", c->epistemic_status);
    text (b, "x_semantics", "calendar_year");
    text (b, "x_unit", c->x_unit);
    text (b, "x_dimension", c->x_dimension);
    text (b, "x_scale", "linear");
    text (b, "y_unit", c->y_unit);
    text (b, "y_dimension", c->y_dimension);
    text (b, "numeric_profile", ATM_SERIES_CONTRACT_NUMERIC_PROFILE);
    text (b, "y_scale", "linear");
    text (b, "missing_policy", "GAP");
    text (b, "extent_policy", "exact_data_non_degenerate/1");
    /* This is an explicit marker, not an approval to project or render EI. */
    text (b, "display_profile_id", ATM_CHART_SPEC_V2_EI_DISPLAY_PROFILE);
    text (b, "display_profile_version", ATM_CHART_SPEC_V2_EI_DISPLAY_PROFILE_VERSION);
    json_builder_set_member_name (b, "point_count");
    json_builder_add_int_value (b, (gint64) atm_energy_institute_verified_series_count (s->ei_series));
    json_builder_set_member_name (b, "x_min");
    json_builder_add_int_value (b, s->x_min);
    json_builder_set_member_name (b, "x_max");
    json_builder_add_int_value (b, s->x_max);
    decimal (b, "y_min", &s->y_min);
    decimal (b, "y_max", &s->y_max);
    json_builder_set_member_name (b, "series");
    json_builder_begin_array (b);
    json_builder_begin_object (b);
    text (b, "scientific_id", c->scientific_content_id);
    text (b, "qualified_id", c->qualified_series_id);
    text (b, "profile_id", c->profile_id);
    text (b, "profile_version", c->profile_version);
    json_builder_end_object (b);
    json_builder_end_array (b);
    json_builder_end_object (b);

    JsonNode *root = json_builder_get_root (b);
    char *json = json_to_string (root, FALSE);
    gboolean ok = atm_scientific_content_id_json ("atm.chart-spec.v2", json, out, error);
    g_free (json);
    json_node_free (root);
    g_object_unref (b);
    return ok;
}


static gboolean
identity_v3_bnr (const AtmChartSpec *s, char **out, GError **error)
{
    const AtmSeriesContract *c =
        atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (s->bnr_series));
    if (c == NULL || c->points == NULL || c->points->len != 12u)
        return reject (error, "BNR C01 ChartSpec v3 has no qualified quarterly contract.");

    const AtmSeriesContractPoint *first = g_ptr_array_index (c->points, 0);
    const AtmSeriesContractPoint *last = g_ptr_array_index (c->points, 11);
    if (first == NULL || last == NULL || first->x == NULL || last->x == NULL)
        return reject (error, "BNR C01 ChartSpec v3 has no valid quarter extent.");

    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    text (b, "schema", ATM_CHART_SPEC_V3_SCHEMA);
    text (b, "identity_domain", "atm.chart-spec.v3");
    text (b, "kind", s->kind == ATM_CHART_LINE ? "LINE" : "SCATTER");
    text (b, "series_profile", c->profile_id);
    text (b, "series_profile_version", c->profile_version);
    text (b, "panel_profile", ATM_BNR_BLS_PROFILE);
    text (b, "repository_id", c->repository_id);
    text (b, "repository_version", c->repository_version);
    text (b, "snapshot_sha", c->snapshot_sha);
    text (b, "source_path", c->source_path);
    text (b, "scientific_series_id", c->scientific_content_id);
    text (b, "qualified_series_id", c->qualified_series_id);
    text (b, "subject", c->subject);
    text (b, "attribute", c->attribute);
    text (b, "series_semantics", c->series_semantics);
    text (b, "scenario", c->scenario);
    text (b, "time_scope", c->time_scope);
    text (b, "epistemic_status", c->epistemic_status);
    text (b, "x_semantics", "calendar_quarter");
    text (b, "x_unit", c->x_unit);
    text (b, "x_dimension", c->x_dimension);
    text (b, "x_scale", "ordered_quarter_ordinal");
    text (b, "y_unit", c->y_unit);
    text (b, "y_dimension", c->y_dimension);
    text (b, "numeric_profile", ATM_SERIES_CONTRACT_NUMERIC_PROFILE);
    text (b, "y_scale", "linear");
    text (b, "missing_policy", "profile_authorized_missing_gap/1");
    text (b, "extent_policy", "exact_numeric_data_non_degenerate/1");
    text (b, "display_profile_id", ATM_CHART_SPEC_V3_BNR_DISPLAY_PROFILE);
    text (b, "display_profile_version", ATM_CHART_SPEC_V3_BNR_DISPLAY_PROFILE_VERSION);
    json_builder_set_member_name (b, "point_count");
    json_builder_add_int_value (b, (gint64) c->points->len);
    json_builder_set_member_name (b, "x_min");
    json_builder_add_string_value (b, first->x);
    json_builder_set_member_name (b, "x_max");
    json_builder_add_string_value (b, last->x);
    decimal (b, "y_min", &s->y_min);
    decimal (b, "y_max", &s->y_max);
    json_builder_set_member_name (b, "series");
    json_builder_begin_array (b);
    json_builder_begin_object (b);
    text (b, "scientific_id", c->scientific_content_id);
    text (b, "qualified_id", c->qualified_series_id);
    text (b, "profile_id", c->profile_id);
    text (b, "profile_version", c->profile_version);
    json_builder_end_object (b);
    json_builder_end_array (b);
    json_builder_end_object (b);

    JsonNode *root = json_builder_get_root (b);
    char *json = json_to_string (root, FALSE);
    gboolean ok = atm_scientific_content_id_json ("atm.chart-spec.v3", json, out, error);
    g_free (json);
    json_node_free (root);
    g_object_unref (b);
    return ok;
}

gboolean atm_chart_spec_new_energy_institute_v2 (
    AtmChartKind kind,
    const AtmEnergyInstituteVerifiedSeries *input,
    AtmChartSpec **out,
    GError **error)
{
    if (out == NULL || *out != NULL || input == NULL ||
        (kind != ATM_CHART_LINE && kind != ATM_CHART_SCATTER))
        return reject (error, "Unsupported Energy Institute ChartSpec v2 arguments or kind.");

    AtmChartSpec *s = g_new0 (AtmChartSpec, 1);
    s->version = ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2;
    s->kind = kind;
    s->count = 1;

    if (!atm_energy_institute_verified_series_rebuild (input, &s->ei_series, error))
        goto invalid;

    const AtmSeriesContract *c =
        atm_energy_institute_verified_series_contract (s->ei_series);
    if (c == NULL ||
        c->x_kind != ATM_SERIES_X_CALENDAR_YEAR ||
        g_strcmp0 (c->subject, "global_total_energy_supply") != 0 ||
        g_strcmp0 (c->attribute, "total_energy_supply") != 0 ||
        g_strcmp0 (c->x_unit, "calendar_year") != 0 ||
        g_strcmp0 (c->x_dimension, "time") != 0 ||
        g_strcmp0 (c->y_unit, "exajoules/year") != 0 ||
        g_strcmp0 (c->y_dimension, "energy_flow_rate") != 0 ||
        g_strcmp0 (c->series_semantics, "empirical_annual_observations") != 0 ||
        g_strcmp0 (c->scenario, "not_applicable") != 0 ||
        g_strcmp0 (c->time_scope, "1965-2025") != 0 ||
        g_strcmp0 (c->epistemic_status, "empirical") != 0 ||
        g_strcmp0 (c->repository_id, "ewd") != 0 ||
        g_strcmp0 (c->profile_id, ATM_EI_VERIFIED_SERIES_PROFILE) != 0 ||
        g_strcmp0 (c->profile_version, "1") != 0 ||
        c->points == NULL || c->points->len != 61) {
        reject (error, "Energy Institute ChartSpec v2 source/profile tuple is unsupported.");
        goto invalid;
    }

    char *expected_version = g_strconcat ("snapshot:", c->snapshot_sha, NULL);
    gboolean version_matches =
        g_strcmp0 (c->repository_version, expected_version) == 0;
    g_free (expected_version);
    if (!version_matches) {
        reject (error, "Energy Institute ChartSpec v2 repository version disagrees with its snapshot.");
        goto invalid;
    }

    const AtmSeriesContractPoint *low = NULL, *high = NULL;
    s->x_min = G_MAXUINT;
    for (guint i = 0; i < c->points->len; i++) {
        const AtmSeriesContractPoint *p = g_ptr_array_index (c->points, i);
        guint year = 0;
        if (p == NULL || p->order != i ||
            !contract_year (p, &year) || year != 1965u + i ||
            p->y_status != ATM_SERIES_Y_NUMERIC ||
            p->coefficient == NULL || p->source_decimal == NULL ||
            p->break_before || p->missing_reason != NULL || p->break_reason != NULL) {
            reject (error, "Energy Institute ChartSpec v2 contains an unsupported point or gap.");
            goto invalid;
        }
        s->x_min = MIN (s->x_min, year);
        s->x_max = MAX (s->x_max, year);
        if (low == NULL || compare_contract_points (p, low) < 0) low = p;
        if (high == NULL || compare_contract_points (p, high) > 0) high = p;
    }

    if (low == NULL || high == NULL || s->x_min >= s->x_max ||
        compare_contract_points (low, high) >= 0) {
        reject (error, "Energy Institute ChartSpec v2 has empty or degenerate extents.");
        goto invalid;
    }
    s->y_min.coefficient = g_strdup (low->coefficient);
    s->y_min.exponent = low->exponent;
    s->y_max.coefficient = g_strdup (high->coefficient);
    s->y_max.exponent = high->exponent;
    if (!identity_v2 (s, &s->id, error))
        goto invalid;
    *out = s;
    return TRUE;

invalid:
    atm_chart_spec_free (s);
    return FALSE;
}


gboolean
atm_chart_spec_new_bnr_bls_c01_v3 (
    AtmChartKind kind,
    const AtmBnrBlsVerifiedSeries *input,
    AtmChartSpec **out,
    GError **error)
{
    if (out == NULL || *out != NULL || input == NULL ||
        (kind != ATM_CHART_LINE && kind != ATM_CHART_SCATTER))
        return reject (error, "Unsupported BNR C01 ChartSpec v3 arguments or kind.");

    AtmChartSpec *s = g_new0 (AtmChartSpec, 1);
    s->version = ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3;
    s->kind = kind;
    s->count = 1;

    if (!atm_bnr_bls_verified_series_rebuild (input, &s->bnr_series, error))
        goto invalid;

    const AtmSeriesContract *contract =
        atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (s->bnr_series));
    if (contract == NULL ||
        contract->x_kind != ATM_SERIES_X_CALENDAR_QUARTER ||
        g_strcmp0 (contract->subject, "non_financial_corporations") != 0 ||
        g_strcmp0 (contract->attribute, "nfc_credit_standards") != 0 ||
        g_strcmp0 (contract->x_unit, "calendar_quarter") != 0 ||
        g_strcmp0 (contract->x_dimension, "time") != 0 ||
        g_strcmp0 (contract->y_unit, "percent") != 0 ||
        g_strcmp0 (contract->y_dimension, "survey_net_percentage") != 0 ||
        g_strcmp0 (contract->series_semantics, "empirical_quarterly_observations") != 0 ||
        g_strcmp0 (contract->scenario, "not_applicable") != 0 ||
        g_strcmp0 (contract->time_scope, "2022-Q4..2025-Q3") != 0 ||
        g_strcmp0 (contract->epistemic_status, "empirical") != 0 ||
        g_strcmp0 (contract->repository_id, "rmd") != 0 ||
        g_strcmp0 (contract->snapshot_sha, ATM_BNR_BLS_SNAPSHOT_SHA) != 0 ||
        g_strcmp0 (contract->repository_version,
                   "snapshot:" ATM_BNR_BLS_SNAPSHOT_SHA) != 0 ||
        g_strcmp0 (contract->source_path,
                   "data/processed/bnr_bls_realised_rounds.csv") != 0 ||
        g_strcmp0 (contract->profile_id,
                   ATM_BNR_BLS_VERIFIED_SERIES_PROFILE) != 0 ||
        g_strcmp0 (contract->profile_version, "1") != 0 ||
        contract->points == NULL || contract->points->len != 12u) {
        reject (error, "BNR C01 ChartSpec v3 source/profile tuple is unsupported.");
        goto invalid;
    }

    static const char *const expected_quarters[] = {
        "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3",
        "2023-Q4", "2024-Q1", "2024-Q2", "2024-Q3",
        "2024-Q4", "2025-Q1", "2025-Q2", "2025-Q3"
    };
    const AtmSeriesContractPoint *low = NULL, *high = NULL;
    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        const AtmSeriesContractPoint *point =
            g_ptr_array_index (contract->points, i);
        if (point == NULL || point->order != i ||
            g_strcmp0 (point->x, expected_quarters[i]) != 0 ||
            point->break_before || point->break_reason != NULL ||
            point->support == NULL || point->support->len == 0) {
            reject (error, "BNR C01 ChartSpec v3 contains an invalid quarter, break, or support.");
            goto invalid;
        }
        if (i == 10u) {
            if (point->y_status != ATM_SERIES_Y_MISSING ||
                point->source_decimal != NULL || point->coefficient != NULL ||
                point->exponent != 0 ||
                g_strcmp0 (point->missing_reason, ATM_BNR_BLS_MISSING_REASON) != 0 ||
                point->support->len != ATM_BNR_BLS_SOURCE_COUNT) {
                reject (error, "BNR C01 ChartSpec v3 changed its authorized 2025-Q2 absence.");
                goto invalid;
            }
            continue;
        }
        if (point->y_status != ATM_SERIES_Y_NUMERIC ||
            point->source_decimal == NULL || point->coefficient == NULL ||
            point->missing_reason != NULL) {
            reject (error, "BNR C01 ChartSpec v3 contains an unsupported numeric observation.");
            goto invalid;
        }
        if (low == NULL || compare_contract_points (point, low) < 0) low = point;
        if (high == NULL || compare_contract_points (point, high) > 0) high = point;
    }
    /* The source includes zeros and positive values, with no negative values. */
    if (low == NULL || high == NULL || compare_contract_points (low, high) >= 0) {
        reject (error, "BNR C01 ChartSpec v3 has empty or degenerate numeric extents.");
        goto invalid;
    }

    /* Internally these are ordered point ordinals, not years. The canonical ID
     * and point accessor preserve the authoritative calendar-quarter labels. */
    s->x_min = 0u;
    s->x_max = 11u;
    s->y_min.coefficient = g_strdup (low->coefficient);
    s->y_min.exponent = low->exponent;
    s->y_max.coefficient = g_strdup (high->coefficient);
    s->y_max.exponent = high->exponent;
    if (!identity_v3_bnr (s, &s->id, error))
        goto invalid;
    *out = s;
    return TRUE;

invalid:
    atm_chart_spec_free (s);
    return FALSE;
}

gboolean atm_chart_spec_validate (const AtmChartSpec *s, GError **error)
{
    if (s != NULL && s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3) {
        if (s->count != 1 || s->bnr_series == NULL)
            return reject (error, "Invalid BNR C01 ChartSpec v3 coverage.");
        AtmChartSpec *expected = NULL;
        if (!atm_chart_spec_new_bnr_bls_c01_v3 (s->kind, s->bnr_series,
                                                &expected, error))
            return FALSE;
        gboolean matches =
            s->x_min == expected->x_min && s->x_max == expected->x_max &&
            s->y_min.exponent == expected->y_min.exponent &&
            s->y_max.exponent == expected->y_max.exponent &&
            g_strcmp0 (s->y_min.coefficient, expected->y_min.coefficient) == 0 &&
            g_strcmp0 (s->y_max.coefficient, expected->y_max.coefficient) == 0 &&
            g_strcmp0 (s->id, expected->id) == 0;
        atm_chart_spec_free (expected);
        return matches || reject (error, "BNR C01 ChartSpec v3 disagrees with reconstructed source.");
    }
    if (s != NULL && s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2) {
        if (s->count != 1 || s->ei_series == NULL)
            return reject (error, "Invalid Energy Institute ChartSpec v2 coverage.");
        AtmChartSpec *expected = NULL;
        if (!atm_chart_spec_new_energy_institute_v2 (s->kind, s->ei_series,
                                                     &expected, error))
            return FALSE;
        gboolean matches =
            s->x_min == expected->x_min && s->x_max == expected->x_max &&
            s->y_min.exponent == expected->y_min.exponent &&
            s->y_max.exponent == expected->y_max.exponent &&
            g_strcmp0 (s->y_min.coefficient, expected->y_min.coefficient) == 0 &&
            g_strcmp0 (s->y_max.coefficient, expected->y_max.coefficient) == 0 &&
            g_strcmp0 (s->id, expected->id) == 0;
        atm_chart_spec_free (expected);
        return matches || reject (error, "Energy Institute ChartSpec v2 disagrees with reconstructed source.");
    }
    if (s == NULL || s->version != ATM_CHART_SPEC_VERSION_GISTEMP_V1 ||
        s->count == 0 || s->count > ATM_CHART_SPEC_MAX_SERIES)
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
AtmChartSpecVersion atm_chart_spec_version (const AtmChartSpec *s)
{ return s ? s->version : 0; }
const char *atm_chart_spec_schema (const AtmChartSpec *s)
{
    if (s == NULL) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return ATM_CHART_SPEC_V2_SCHEMA;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return ATM_CHART_SPEC_V3_SCHEMA;
    return ATM_CHART_SPEC_SCHEMA;
}
const char *atm_chart_spec_id (const AtmChartSpec *s) { return s ? s->id : NULL; }
AtmChartKind atm_chart_spec_kind (const AtmChartSpec *s) { return s ? s->kind : 0; }
guint atm_chart_spec_count (const AtmChartSpec *s) { return s ? s->count : 0; }
const AtmVerifiedSeries *atm_chart_spec_series (const AtmChartSpec *s, guint i)
{ return s && s->version == ATM_CHART_SPEC_VERSION_GISTEMP_V1 && i < s->count ? s->series[i] : NULL; }
const char *atm_chart_spec_series_scientific_id (const AtmChartSpec *s, guint i)
{
    if (s == NULL || i >= s->count) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_scientific_id (s->ei_series);
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return atm_bnr_bls_verified_series_scientific_id (s->bnr_series);
    return atm_verified_series_scientific_id (s->series[i]);
}
const char *atm_chart_spec_series_qualified_id (const AtmChartSpec *s, guint i)
{
    if (s == NULL || i >= s->count) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_qualified_id (s->ei_series);
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return atm_bnr_bls_verified_series_qualified_id (s->bnr_series);
    return atm_verified_series_qualified_id (s->series[i]);
}
guint atm_chart_spec_x_min (const AtmChartSpec *s) { return s ? s->x_min : 0; }
guint atm_chart_spec_x_max (const AtmChartSpec *s) { return s ? s->x_max : 0; }
const AtmScientificDecimal *atm_chart_spec_y_min (const AtmChartSpec *s) { return s ? &s->y_min : NULL; }
const AtmScientificDecimal *atm_chart_spec_y_max (const AtmChartSpec *s) { return s ? &s->y_max : NULL; }

guint atm_chart_spec_point_count (const AtmChartSpec *s, guint series_index)
{
    if (s == NULL || series_index >= s->count) return 0;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_count (s->ei_series);
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return atm_bnr_bls_verified_series_count (s->bnr_series);
    return atm_verified_series_count (s->series[series_index]);
}

gboolean atm_chart_spec_point (const AtmChartSpec *s, guint series_index,
    guint point_index, AtmChartPointView *out)
{
    if (out == NULL) return FALSE;
    *out = (AtmChartPointView) {0};
    if (s == NULL || series_index >= s->count ||
        point_index >= atm_chart_spec_point_count (s, series_index))
        return FALSE;

    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3) {
        const AtmSeriesContractPoint *p =
            atm_bnr_bls_verified_series_point (s->bnr_series, point_index);
        if (p == NULL || p->x == NULL) return FALSE;
        out->order = p->order;
        out->calendar_quarter = p->x;
        out->source_row = 0u; /* A missing quarter is an assertion, not a CSV row. */
        out->y_status = p->y_status == ATM_SERIES_Y_NUMERIC
            ? ATM_CHART_POINT_NUMERIC : ATM_CHART_POINT_MISSING;
        out->source_decimal = p->source_decimal;
        out->coefficient = p->coefficient;
        out->exponent = p->exponent;
        out->break_before = p->break_before;
        out->support = p->support;
        return TRUE;
    }

    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2) {
        const AtmSeriesContractPoint *p =
            atm_energy_institute_verified_series_point (s->ei_series, point_index);
        if (p == NULL) return FALSE;
        guint year = 0;
        if (!contract_year (p, &year)) return FALSE;
        out->order = p->order;
        out->calendar_year = year;
        /* Admission pins a complete annual EI CSV whose data begin on row 2. */
        out->source_row = point_index + 2u;
        out->y_status = p->y_status == ATM_SERIES_Y_NUMERIC
            ? ATM_CHART_POINT_NUMERIC : ATM_CHART_POINT_MISSING;
        out->source_decimal = p->source_decimal;
        out->coefficient = p->coefficient;
        out->exponent = p->exponent;
        out->break_before = p->break_before;
        out->support = p->support;
        return TRUE;
    }

    if (s->version == ATM_CHART_SPEC_VERSION_GISTEMP_V1) {
        const AtmVerifiedPoint *p =
            atm_verified_series_point (s->series[series_index], point_index);
        if (p == NULL) return FALSE;
        out->order = p->order;
        out->calendar_year = p->calendar_year;
        out->source_row = p->source_row;
        out->y_status = p->y_status == ATM_VERIFIED_Y_NUMERIC
            ? ATM_CHART_POINT_NUMERIC : ATM_CHART_POINT_MISSING;
        out->source_decimal = p->source_decimal;
        out->coefficient = p->coefficient;
        out->exponent = p->exponent;
        out->break_before = p->break_before;
        out->support = p->support;
        return TRUE;
    }
    return FALSE;
}

const char *atm_chart_spec_x_unit (const AtmChartSpec *s)
{
    if (s == NULL) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->x_unit;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return "calendar_quarter";
    return "calendar_year";
}
const char *atm_chart_spec_x_dimension (const AtmChartSpec *s)
{
    if (s == NULL) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->x_dimension;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return "time";
    return "time";
}
const char *atm_chart_spec_y_unit (const AtmChartSpec *s)
{
    if (s == NULL) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->y_unit;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return "percent";
    return atm_verified_series_unit (s->series[0]);
}
const char *atm_chart_spec_y_dimension (const AtmChartSpec *s)
{
    if (s == NULL) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->y_dimension;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return "survey_net_percentage";
    return "temperature_difference";
}
const char *atm_chart_spec_series_profile (const AtmChartSpec *s)
{
    if (s == NULL) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->profile_id;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return ATM_BNR_BLS_VERIFIED_SERIES_PROFILE;
    return ATM_VERIFIED_SERIES_PROFILE;
}

const char *atm_chart_spec_series_snapshot_sha (const AtmChartSpec *s, guint i)
{
    if (s == NULL || i >= s->count) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->snapshot_sha;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (s->bnr_series))->snapshot_sha;
    if (s->version == ATM_CHART_SPEC_VERSION_GISTEMP_V1)
        return atm_verified_series_snapshot_sha (s->series[i]);
    return NULL;
}

const char *atm_chart_spec_series_source_path (const AtmChartSpec *s, guint i)
{
    if (s == NULL || i >= s->count) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->source_path;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (s->bnr_series))->source_path;
    if (s->version == ATM_CHART_SPEC_VERSION_GISTEMP_V1)
        return atm_verified_series_source_path (s->series[i]);
    return NULL;
}

const char *atm_chart_spec_series_repository_id (const AtmChartSpec *s, guint i)
{
    if (s == NULL || i >= s->count) return NULL;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_energy_institute_verified_series_contract (s->ei_series)->repository_id;
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return "rmd";
    if (s->version == ATM_CHART_SPEC_VERSION_GISTEMP_V1)
        return "ewd";
    return NULL;
}

gboolean atm_chart_spec_rebuild (const AtmChartSpec *s, AtmChartSpec **out, GError **error)
{
    if (out == NULL || *out != NULL) return reject (error, "Invalid ChartSpec rebuild output.");
    if (!atm_chart_spec_validate (s, error)) return FALSE;
    if (s->version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return atm_chart_spec_new_energy_institute_v2 (s->kind, s->ei_series, out, error);
    if (s->version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return atm_chart_spec_new_bnr_bls_c01_v3 (s->kind, s->bnr_series, out, error);
    const AtmVerifiedSeries *inputs[ATM_CHART_SPEC_MAX_SERIES];
    for (guint i = 0; i < s->count; i++) inputs[i] = s->series[i];
    return atm_chart_spec_new (s->kind, inputs, s->count, out, error);
}
