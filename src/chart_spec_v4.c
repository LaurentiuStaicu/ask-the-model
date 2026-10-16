#include "chart_spec_v4.h"

#include "bnr_bls_verified_series.h"
#include "energy_institute_verified_series.h"
#include "scientific_canonical.h"
#include "verified_series.h"

#include <json-glib/json-glib.h>
#include <string.h>

struct AtmChartSpecV4 {
    const AtmChartCatalogEntry *entry;
    AtmChartSpec *source_spec;
    char *id;
};

static gboolean
reject (GError **error, AtmChartSpecV4Error code, const char *message)
{
    g_set_error_literal (error, ATM_CHART_SPEC_V4_ERROR, code, message);
    return FALSE;
}

GQuark
atm_chart_spec_v4_error_quark (void)
{
    return g_quark_from_static_string ("atm-chart-spec-v4-error-quark");
}

static gboolean
entry_is_registered (const AtmChartCatalogEntry *entry)
{
    return entry != NULL && entry->series_id != NULL &&
        atm_chart_catalog_find_id (entry->series_id) == entry;
}

static gboolean
parse_year (const char *text, guint *out_year)
{
    if (text == NULL || *text == '\0' || out_year == NULL)
        return FALSE;

    char *end = NULL;
    guint64 value = g_ascii_strtoull (text, &end, 10);
    if (end == text || *end != '\0' || value == 0 || value > 9999)
        return FALSE;

    char canonical[5];
    g_snprintf (canonical, sizeof canonical, "%04" G_GUINT64_FORMAT, value);
    if (g_strcmp0 (canonical, text) != 0)
        return FALSE;

    *out_year = (guint) value;
    return TRUE;
}

static gboolean
parse_quarter (const char *text, guint *out_year, guint *out_quarter)
{
    if (text == NULL || out_year == NULL || out_quarter == NULL ||
        strlen (text) != 7 || text[4] != '-' || text[5] != 'Q' ||
        !g_ascii_isdigit (text[0]) || !g_ascii_isdigit (text[1]) ||
        !g_ascii_isdigit (text[2]) || !g_ascii_isdigit (text[3]) ||
        !g_ascii_isdigit (text[6]))
        return FALSE;

    guint year = (guint) (text[0] - '0') * 1000u +
        (guint) (text[1] - '0') * 100u +
        (guint) (text[2] - '0') * 10u +
        (guint) (text[3] - '0');
    guint quarter = (guint) (text[6] - '0');

    if (year == 0 || quarter < 1u || quarter > 4u)
        return FALSE;

    *out_year = year;
    *out_quarter = quarter;
    return TRUE;
}

static gboolean
source_profile_matches (const AtmChartCatalogEntry *entry,
                        const AtmChartSpec *source,
                        GError **error)
{
    AtmChartSpecVersion version = atm_chart_spec_version (source);
    const char *schema = atm_chart_spec_schema (source);
    const char *profile = atm_chart_spec_series_profile (source);

    switch (entry->adapter) {
    case ATM_CHART_CATALOG_ADAPTER_GISTEMP_V1:
        if (version != ATM_CHART_SPEC_VERSION_GISTEMP_V1 ||
            g_strcmp0 (schema, ATM_CHART_SPEC_SCHEMA) != 0 ||
            g_strcmp0 (profile, ATM_VERIFIED_SERIES_PROFILE) != 0)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_PROFILE,
                "Catalog GISTEMP entry does not match the source-qualified GISTEMP v1 spec.");
        return TRUE;

    case ATM_CHART_CATALOG_ADAPTER_ENERGY_INSTITUTE_V2:
        if (version != ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2 ||
            g_strcmp0 (schema, ATM_CHART_SPEC_V2_SCHEMA) != 0 ||
            g_strcmp0 (profile, ATM_EI_VERIFIED_SERIES_PROFILE) != 0)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_PROFILE,
                "Catalog Energy Institute entry does not match the source-qualified TES v2 spec.");
        return TRUE;

    case ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3:
        if (version != ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3 ||
            g_strcmp0 (schema, ATM_CHART_SPEC_V3_SCHEMA) != 0 ||
            g_strcmp0 (profile, ATM_BNR_BLS_VERIFIED_SERIES_PROFILE) != 0)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_PROFILE,
                "Catalog BNR entry does not match the source-qualified C01 v3 spec.");
        return TRUE;
    }

    return reject (error, ATM_CHART_SPEC_V4_ERROR_PROFILE,
        "Catalog adapter is not supported by this ChartSpec v4 constructor.");
}

static gboolean
coverage_matches (const AtmChartCatalogEntry *entry,
                  const AtmChartSpec *source,
                  GError **error)
{
    guint count = atm_chart_spec_point_count (source, 0);
    if (count != entry->coordinate_count ||
        entry->numeric_observation_count > entry->coordinate_count)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
            "Source ChartSpec coordinate count differs from the catalog declaration.");

    guint numeric_count = 0;
    guint missing_count = 0;

    if (entry->x_kind == ATM_CHART_CATALOG_X_CALENDAR_YEAR) {
        guint start_year = 0;
        guint end_year = 0;
        if (!parse_year (entry->coverage_start, &start_year) ||
            !parse_year (entry->coverage_end, &end_year) ||
            end_year < start_year ||
            (guint64) end_year - start_year + 1u != entry->coordinate_count)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "Catalog annual coverage bounds are not a complete closed year range.");

        for (guint i = 0; i < count; i++) {
            AtmChartPointView point = {0};
            if (!atm_chart_spec_point (source, 0, i, &point) ||
                point.order != i ||
                point.calendar_year != start_year + i ||
                point.calendar_quarter != NULL ||
                point.y_status != ATM_CHART_POINT_NUMERIC ||
                point.source_decimal == NULL || point.coefficient == NULL ||
                point.missing_reason != NULL)
                return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                    "Source annual points differ from the catalog coordinate or numeric-observation policy.");
            numeric_count++;
        }

        if (count == 0 || atm_chart_spec_point_count (source, 0) == 0 ||
            atm_chart_spec_x_min (source) != start_year ||
            atm_chart_spec_x_max (source) != end_year)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "Source annual ChartSpec extents differ from the catalog coverage bounds.");
    } else if (entry->x_kind == ATM_CHART_CATALOG_X_CALENDAR_QUARTER) {
        guint start_year = 0, start_quarter = 0, end_year = 0, end_quarter = 0;
        if (!parse_quarter (entry->coverage_start, &start_year, &start_quarter) ||
            !parse_quarter (entry->coverage_end, &end_year, &end_quarter))
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "Catalog quarterly coverage bounds are malformed.");

        guint64 start_ordinal = (guint64) start_year * 4u + start_quarter - 1u;
        guint64 end_ordinal = (guint64) end_year * 4u + end_quarter - 1u;
        if (end_ordinal < start_ordinal ||
            end_ordinal - start_ordinal + 1u != entry->coordinate_count)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "Catalog quarterly coverage is not a complete coordinate range.");

        for (guint i = 0; i < count; i++) {
            guint year = start_year;
            guint quarter = start_quarter + i;
            while (quarter > 4u) {
                quarter -= 4u;
                year++;
            }
            char expected[8];
            g_snprintf (expected, sizeof expected, "%04u-Q%u", year, quarter);

            AtmChartPointView point = {0};
            if (!atm_chart_spec_point (source, 0, i, &point) ||
                point.order != i || point.calendar_year != 0u ||
                g_strcmp0 (point.calendar_quarter, expected) != 0)
                return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                    "Source quarterly coordinates are not the exact catalog-bound sequence.");

            if (point.y_status == ATM_CHART_POINT_NUMERIC) {
                if (point.source_decimal == NULL || point.coefficient == NULL ||
                    point.missing_reason != NULL)
                    return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                        "Numeric quarterly point has missing-value fields or no source decimal.");
                numeric_count++;
            } else if (point.y_status == ATM_CHART_POINT_MISSING) {
                if (entry->missing_policy != ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE ||
                    entry->missing_coordinate == NULL ||
                    g_strcmp0 (point.calendar_quarter, entry->missing_coordinate) != 0 ||
                    g_strcmp0 (point.missing_reason, entry->missing_reason) != 0 ||
                    point.source_decimal != NULL || point.coefficient != NULL)
                    return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                        "Missing quarterly point is not the exact catalog-authorized absence.");
                missing_count++;
            } else {
                return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                    "Quarterly point has an unknown observation status.");
            }
        }

        if (count == 0 ||
            g_strcmp0 (atm_chart_spec_calendar_quarter_min (source),
                       entry->coverage_start) != 0 ||
            g_strcmp0 (atm_chart_spec_calendar_quarter_max (source),
                       entry->coverage_end) != 0)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "Source quarterly ChartSpec extents differ from the catalog coverage bounds.");
    } else {
        return reject (error, ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
            "Catalog coordinate kind is not admitted by ChartSpec v4.");
    }

    if (numeric_count != entry->numeric_observation_count)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
            "Numeric observation count differs from the catalog declaration.");

    if (entry->missing_policy == ATM_CHART_CATALOG_MISSING_REJECT) {
        if (missing_count != 0 || entry->missing_coordinate != NULL ||
            entry->missing_reason != NULL)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "This catalog profile does not authorize missing observations.");
    } else if (entry->missing_policy == ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE) {
        if (entry->adapter != ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3 ||
            missing_count != 1u ||
            entry->missing_coordinate == NULL || entry->missing_reason == NULL)
            return reject (error, ATM_CHART_SPEC_V4_ERROR_COVERAGE,
                "Only the exact BNR C01 profile authorizes its bounded missing quarter.");
    } else {
        return reject (error, ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
            "Catalog missing-value policy is not supported.");
    }

    return TRUE;
}

static gboolean
source_matches_catalog (const AtmChartCatalogEntry *entry,
                        const AtmChartSpec *source,
                        GError **error)
{
    if (!entry_is_registered (entry))
        return reject (error, ATM_CHART_SPEC_V4_ERROR_CATALOG,
            "ChartSpec v4 requires a pointer-identical compiled catalog entry.");

    if (!atm_chart_catalog_validate (error))
        return FALSE;

    if (source == NULL)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_ARGUMENT,
            "A source-qualified ChartSpec is required.");

    if (!atm_chart_spec_validate (source, error))
        return FALSE;

    if (atm_chart_spec_count (source) != 1u)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
            "The first generic ChartSpec v4 slice supports exactly one series.");

    if (atm_chart_spec_kind (source) != ATM_CHART_LINE ||
        entry->allowed_chart_kind_count != 1u ||
        entry->allowed_chart_kinds == NULL ||
        g_strcmp0 (entry->allowed_chart_kinds[0], "line") != 0)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
            "The catalog and source spec must both authorize LINE.");

    if (!source_profile_matches (entry, source, error))
        return FALSE;

    const char *repository = atm_chart_spec_series_repository_id (source, 0);
    const char *snapshot = atm_chart_spec_series_snapshot_sha (source, 0);
    const char *path = atm_chart_spec_series_source_path (source, 0);

    if (g_strcmp0 (repository, entry->repository_key) != 0 ||
        !atm_chart_catalog_snapshot_admitted (entry, snapshot) ||
        g_strcmp0 (path, entry->source_path) != 0)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_PROFILE,
            "Source repository, pinned snapshot or source path is not admitted by the catalog.");

    if (g_strcmp0 (atm_chart_spec_x_unit (source), entry->x_unit) != 0 ||
        g_strcmp0 (atm_chart_spec_x_dimension (source), entry->x_dimension) != 0 ||
        g_strcmp0 (atm_chart_spec_y_unit (source), entry->y_unit) != 0 ||
        g_strcmp0 (atm_chart_spec_y_dimension (source), entry->y_dimension) != 0)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
            "Source ChartSpec units/dimensions differ from the catalog metadata.");

    if ((entry->x_kind == ATM_CHART_CATALOG_X_CALENDAR_YEAR &&
         (g_strcmp0 (entry->x_coordinate_semantics, "calendar_year") != 0 ||
          g_strcmp0 (entry->x_frequency, "annual") != 0)) ||
        (entry->x_kind == ATM_CHART_CATALOG_X_CALENDAR_QUARTER &&
         (g_strcmp0 (entry->x_coordinate_semantics, "calendar_quarter") != 0 ||
          g_strcmp0 (entry->x_frequency, "quarterly") != 0)))
        return reject (error, ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
            "Catalog X coordinate kind, semantics and frequency disagree.");

    if (atm_chart_spec_series_scientific_id (source, 0) == NULL ||
        atm_chart_spec_series_qualified_id (source, 0) == NULL)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_PROFILE,
            "Source ChartSpec lacks a qualified scientific-series identity.");

    return coverage_matches (entry, source, error);
}

static void
add_text (JsonBuilder *builder, const char *key, const char *value)
{
    json_builder_set_member_name (builder, key);
    if (value == NULL)
        json_builder_add_null_value (builder);
    else
        json_builder_add_string_value (builder, value);
}

static void
add_uint (JsonBuilder *builder, const char *key, guint value)
{
    json_builder_set_member_name (builder, key);
    json_builder_add_int_value (builder, value);
}

static const char *
missing_policy_name (AtmChartCatalogMissingPolicy policy)
{
    switch (policy) {
    case ATM_CHART_CATALOG_MISSING_REJECT:
        return "reject";
    case ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE:
        return "bounded_profile";
    }
    return "unsupported";
}

static gboolean
build_id (const AtmChartCatalogEntry *entry,
          const AtmChartSpec *source,
          char **out_id,
          GError **error)
{
    JsonBuilder *builder = json_builder_new ();
    json_builder_begin_object (builder);

    add_text (builder, "schema", ATM_CHART_SPEC_V4_SCHEMA);
    add_text (builder, "catalog_schema", ATM_CHART_CATALOG_SCHEMA);
    add_text (builder, "catalog_version", ATM_CHART_CATALOG_VERSION);
    add_text (builder, "series_id", entry->series_id);
    add_text (builder, "display_name", entry->display_name);
    add_text (builder, "description", entry->description);
    add_text (builder, "adapter_id", entry->adapter_id);
    add_text (builder, "catalog_series_profile_id", entry->series_profile_id);
    add_text (builder, "catalog_series_profile_version", entry->series_profile_version);
    add_text (builder, "source_series_profile_id", atm_chart_spec_series_profile (source));
    add_text (builder, "admission_profile_id", entry->admission_profile_id);
    add_text (builder, "admission_profile_version", entry->admission_profile_version);
    add_text (builder, "reconstruction_profile_id", entry->reconstruction_profile_id);
    add_text (builder, "source_chart_spec_schema", atm_chart_spec_schema (source));
    add_text (builder, "source_chart_spec_id", atm_chart_spec_id (source));
    add_text (builder, "source_scientific_series_id",
              atm_chart_spec_series_scientific_id (source, 0));
    add_text (builder, "source_qualified_series_id",
              atm_chart_spec_series_qualified_id (source, 0));
    add_text (builder, "history_schema_version", entry->history_schema_version);

    add_text (builder, "repository_key", entry->repository_key);
    add_text (builder, "repository_full_name", entry->repository_full_name);
    add_text (builder, "snapshot_sha", atm_chart_spec_series_snapshot_sha (source, 0));
    json_builder_set_member_name (builder, "admitted_snapshots");
    json_builder_begin_array (builder);
    for (gsize i = 0; i < entry->snapshot_count; i++)
        json_builder_add_string_value (builder, entry->snapshot_shas[i]);
    json_builder_end_array (builder);
    add_text (builder, "source_path", entry->source_path);

    add_text (builder, "x_coordinate_semantics", entry->x_coordinate_semantics);
    add_text (builder, "x_frequency", entry->x_frequency);
    add_text (builder, "x_unit", entry->x_unit);
    add_text (builder, "x_dimension", entry->x_dimension);
    add_text (builder, "subject", entry->subject);
    add_text (builder, "attribute", entry->attribute);
    add_text (builder, "y_unit", entry->y_unit);
    add_text (builder, "y_dimension", entry->y_dimension);
    add_text (builder, "series_semantics", entry->series_semantics);
    add_text (builder, "observation_meaning", entry->observation_meaning);
    add_text (builder, "reference_period", entry->reference_period);
    add_text (builder, "geography", entry->geography);
    add_text (builder, "coverage_start", entry->coverage_start);
    add_text (builder, "coverage_end", entry->coverage_end);
    add_uint (builder, "coordinate_count", entry->coordinate_count);
    add_uint (builder, "numeric_observation_count", entry->numeric_observation_count);
    add_text (builder, "scenario", entry->scenario);
    add_text (builder, "epistemic_status", entry->epistemic_status);
    add_text (builder, "source_vintage", entry->source_vintage);
    add_text (builder, "missing_policy", missing_policy_name (entry->missing_policy));
    add_text (builder, "missing_coordinate", entry->missing_coordinate);
    add_text (builder, "missing_reason", entry->missing_reason);

    json_builder_set_member_name (builder, "chart_kind");
    json_builder_add_string_value (builder, "LINE");
    add_text (builder, "display_profile_id", entry->display_profile_id);
    add_text (builder, "display_profile_version", entry->display_profile_version);
    add_text (builder, "publisher_name", entry->publisher_name);
    add_text (builder, "publisher_url", entry->publisher_url);
    add_text (builder, "pinned_artifact_description", entry->pinned_artifact_description);
    add_text (builder, "reconstruction_scope", entry->reconstruction_scope);

    json_builder_end_object (builder);
    JsonNode *root = json_builder_get_root (builder);
    char *json = json_to_string (root, FALSE);
    gboolean ok = atm_scientific_content_id_json (
        ATM_CHART_SPEC_V4_ID_DOMAIN, json, out_id, error);

    g_free (json);
    json_node_free (root);
    g_object_unref (builder);
    return ok;
}

gboolean
atm_chart_spec_v4_new (const AtmChartCatalogEntry *entry,
                       const AtmChartSpec *qualified_source_spec,
                       AtmChartSpecV4 **out,
                       GError **error)
{
    if (out == NULL || *out != NULL)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_ARGUMENT,
            "Output must be non-NULL and initially empty.");

    if (!source_matches_catalog (entry, qualified_source_spec, error))
        return FALSE;

    AtmChartSpec *owned_source = NULL;
    if (!atm_chart_spec_rebuild (qualified_source_spec, &owned_source, error))
        return FALSE;

    if (!source_matches_catalog (entry, owned_source, error)) {
        atm_chart_spec_free (owned_source);
        return FALSE;
    }

    char *id = NULL;
    if (!build_id (entry, owned_source, &id, error)) {
        atm_chart_spec_free (owned_source);
        return FALSE;
    }

    AtmChartSpecV4 *spec = g_new0 (AtmChartSpecV4, 1);
    spec->entry = entry;
    spec->source_spec = owned_source;
    spec->id = id;
    *out = spec;
    return TRUE;
}

gboolean
atm_chart_spec_v4_validate (const AtmChartSpecV4 *spec, GError **error)
{
    if (spec == NULL || spec->id == NULL || spec->source_spec == NULL ||
        !entry_is_registered (spec->entry))
        return reject (error, ATM_CHART_SPEC_V4_ERROR_ARGUMENT,
            "ChartSpec v4 object is incomplete or is not backed by the compiled catalog.");

    if (!source_matches_catalog (spec->entry, spec->source_spec, error))
        return FALSE;

    char *expected_id = NULL;
    if (!build_id (spec->entry, spec->source_spec, &expected_id, error))
        return FALSE;

    gboolean matches = g_strcmp0 (spec->id, expected_id) == 0;
    g_free (expected_id);
    if (!matches)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_IDENTITY,
            "ChartSpec v4 identity differs from its reconstructed catalog/source material.");
    return TRUE;
}

gboolean
atm_chart_spec_v4_rebuild (const AtmChartSpecV4 *spec,
                           AtmChartSpecV4 **out,
                           GError **error)
{
    if (out == NULL || *out != NULL)
        return reject (error, ATM_CHART_SPEC_V4_ERROR_ARGUMENT,
            "Rebuild output must be non-NULL and initially empty.");
    if (!atm_chart_spec_v4_validate (spec, error))
        return FALSE;
    return atm_chart_spec_v4_new (spec->entry, spec->source_spec, out, error);
}

void
atm_chart_spec_v4_free (AtmChartSpecV4 *spec)
{
    if (spec == NULL)
        return;
    atm_chart_spec_free (spec->source_spec);
    g_free (spec->id);
    g_free (spec);
}

const char *
atm_chart_spec_v4_schema (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? ATM_CHART_SPEC_V4_SCHEMA : NULL;
}

const char *
atm_chart_spec_v4_id (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->id : NULL;
}

const char *
atm_chart_spec_v4_series_id (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->series_id : NULL;
}

const AtmChartCatalogEntry *
atm_chart_spec_v4_catalog_entry (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry : NULL;
}

const AtmChartSpec *
atm_chart_spec_v4_source_spec (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->source_spec : NULL;
}

AtmChartKind
atm_chart_spec_v4_kind (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? ATM_CHART_LINE : 0;
}

guint
atm_chart_spec_v4_coordinate_count (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->coordinate_count : 0u;
}

guint
atm_chart_spec_v4_numeric_observation_count (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->numeric_observation_count : 0u;
}

const char *
atm_chart_spec_v4_missing_coordinate (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->missing_coordinate : NULL;
}

const char *
atm_chart_spec_v4_missing_reason (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->missing_reason : NULL;
}

const char *
atm_chart_spec_v4_admission_profile_id (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->admission_profile_id : NULL;
}

const char *
atm_chart_spec_v4_reconstruction_profile_id (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->reconstruction_profile_id : NULL;
}

const char *
atm_chart_spec_v4_history_schema_version (const AtmChartSpecV4 *spec)
{
    return spec != NULL ? spec->entry->history_schema_version : NULL;
}
