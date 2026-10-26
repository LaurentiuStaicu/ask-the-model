#include "chart_projection_v4.h"

#include <math.h>
#include <string.h>

struct AtmChartProjectionV4 {
    AtmChartSpecV4 *owned_spec;
    AtmChartProjection *source_projection;
};

GQuark
atm_chart_projection_v4_error_quark (void)
{
    return g_quark_from_static_string ("atm-chart-projection-v4-error-quark");
}

static gboolean
reject (GError **error,
        AtmChartProjectionV4Error code,
        const char *message)
{
    g_set_error_literal (error, ATM_CHART_PROJECTION_V4_ERROR, code, message);
    return FALSE;
}

static gboolean
expected_policy_matches (const AtmChartSpecV4 *spec,
                         const AtmChartProjection *projection,
                         GError **error)
{
    const AtmChartCatalogEntry *entry = atm_chart_spec_v4_catalog_entry (spec);
    if (entry == NULL || entry->display_profile_id == NULL ||
        entry->display_profile_version == NULL)
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_POLICY,
            "The v4 catalog entry has no complete display-profile identity.");

    char *expected = g_strdup_printf ("%s/%s",
        entry->display_profile_id, entry->display_profile_version);
    gboolean matches = g_strcmp0 (
        atm_chart_projection_policy (projection), expected) == 0;
    g_free (expected);

    return matches || reject (error, ATM_CHART_PROJECTION_V4_ERROR_POLICY,
        "The source projection policy does not match the exact catalog display profile.");
}

static gboolean
coverage_matches (const AtmChartSpecV4 *spec,
                  const AtmChartProjection *projection,
                  GError **error)
{
    const AtmChartSpec *source = atm_chart_spec_v4_source_spec (spec);
    guint coordinates = atm_chart_spec_v4_coordinate_count (spec);
    guint expected_numeric =
        atm_chart_spec_v4_numeric_observation_count (spec);
    const char *missing_coordinate =
        atm_chart_spec_v4_missing_coordinate (spec);
    const char *missing_reason =
        atm_chart_spec_v4_missing_reason (spec);

    if (coordinates == 0 || expected_numeric == 0 ||
        expected_numeric > coordinates ||
        atm_chart_projection_count (projection) != expected_numeric)
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
            "Projected numeric point count differs from the catalog-bound v4 counts.");

    if ((missing_coordinate == NULL) != (missing_reason == NULL))
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
            "The catalog-bound missing coordinate and reason must be present together.");

    if (missing_coordinate == NULL) {
        if (coordinates != expected_numeric)
            return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
                "A complete series cannot have an unexplained coordinate-count gap.");
    } else if (coordinates != expected_numeric + 1u) {
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
            "This projection slice admits only the single catalog-authorized missing coordinate.");
    }

    guint projected_index = 0;
    guint numeric_seen = 0;
    guint missing_seen = 0;

    for (guint i = 0; i < coordinates; i++) {
        AtmChartPointView source_point = {0};
        if (!atm_chart_spec_point (source, 0, i, &source_point) ||
            source_point.order != i)
            return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
                "Source coordinate order differs from its catalog-bound sequence.");

        if (source_point.y_status == ATM_CHART_POINT_NUMERIC) {
            if (source_point.source_decimal == NULL ||
                source_point.coefficient == NULL ||
                source_point.missing_reason != NULL)
                return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
                    "A numeric source coordinate has invalid missing-value fields.");

            const AtmChartProjectedPoint *point =
                atm_chart_projection_point (projection, projected_index);
            if (point == NULL || point->series_index != 0 ||
                point->point_index != i || !isfinite (point->x) ||
                !isfinite (point->y))
                return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
                    "Projected numeric points do not map one-to-one to source coordinates.");

            projected_index++;
            numeric_seen++;
        } else if (source_point.y_status == ATM_CHART_POINT_MISSING) {
            if (missing_coordinate == NULL ||
                g_strcmp0 (source_point.calendar_quarter,
                           missing_coordinate) != 0 ||
                g_strcmp0 (source_point.missing_reason, missing_reason) != 0 ||
                source_point.source_decimal != NULL ||
                source_point.coefficient != NULL)
                return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
                    "A missing source coordinate is not the exact catalog-authorized absence.");

            missing_seen++;
        } else {
            return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
                "The projection encountered an unknown source observation status.");
        }
    }

    if (numeric_seen != expected_numeric ||
        projected_index != atm_chart_projection_count (projection) ||
        missing_seen != (missing_coordinate != NULL ? 1u : 0u))
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_COVERAGE,
            "Source coverage and projected numeric/missing counts disagree.");

    return TRUE;
}

gboolean
atm_chart_projection_v4_validate (const AtmChartProjectionV4 *wrapper,
                                 GError **error)
{
    if (wrapper == NULL || wrapper->owned_spec == NULL ||
        wrapper->source_projection == NULL)
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_ARGUMENT,
            "The v4 projection wrapper is incomplete.");

    if (!atm_chart_spec_v4_validate (wrapper->owned_spec, error))
        return FALSE;
    if (!atm_chart_projection_validate (wrapper->source_projection, error))
        return FALSE;

    const AtmChartSpec *v4_source =
        atm_chart_spec_v4_source_spec (wrapper->owned_spec);
    const AtmChartSpec *projected_source =
        atm_chart_projection_spec (wrapper->source_projection);
    if (v4_source == NULL || projected_source == NULL ||
        g_strcmp0 (atm_chart_spec_id (v4_source),
                   atm_chart_spec_id (projected_source)) != 0)
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_IDENTITY,
            "The source projection identity differs from the v4-qualified source spec.");

    if (g_strcmp0 (atm_chart_spec_v4_schema (wrapper->owned_spec),
                   "atm-chart-spec/4") != 0 ||
        !expected_policy_matches (wrapper->owned_spec,
                                  wrapper->source_projection, error))
        return FALSE;

    return coverage_matches (wrapper->owned_spec,
                             wrapper->source_projection, error);
}

gboolean
atm_chart_projection_v4_new (const AtmChartSpecV4 *spec,
                             double width,
                             double height,
                             AtmChartProjectionV4 **out,
                             GError **error)
{
    if (out == NULL || *out != NULL)
        return reject (error, ATM_CHART_PROJECTION_V4_ERROR_ARGUMENT,
            "Projection output must be non-NULL and initially empty.");

    AtmChartSpecV4 *owned_spec = NULL;
    if (!atm_chart_spec_v4_rebuild (spec, &owned_spec, error))
        return FALSE;

    AtmChartProjection *source_projection = NULL;
    if (!atm_chart_projection_new (
            atm_chart_spec_v4_source_spec (owned_spec),
            width, height, &source_projection, error)) {
        atm_chart_spec_v4_free (owned_spec);
        return FALSE;
    }

    AtmChartProjectionV4 *wrapper = g_new0 (AtmChartProjectionV4, 1);
    wrapper->owned_spec = owned_spec;
    wrapper->source_projection = source_projection;

    if (!atm_chart_projection_v4_validate (wrapper, error)) {
        atm_chart_projection_v4_free (wrapper);
        return FALSE;
    }

    *out = wrapper;
    return TRUE;
}

void
atm_chart_projection_v4_free (AtmChartProjectionV4 *wrapper)
{
    if (wrapper == NULL)
        return;
    atm_chart_spec_v4_free (wrapper->owned_spec);
    atm_chart_projection_free (wrapper->source_projection);
    g_free (wrapper);
}

const AtmChartSpecV4 *
atm_chart_projection_v4_spec (const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ? wrapper->owned_spec : NULL;
}

const AtmChartProjection *
atm_chart_projection_v4_source_projection (
    const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ? wrapper->source_projection : NULL;
}

const char *
atm_chart_projection_v4_schema (const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ? ATM_CHART_PROJECTION_V4_SCHEMA : NULL;
}

const char *
atm_chart_projection_v4_series_id (const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ?
        atm_chart_spec_v4_series_id (wrapper->owned_spec) : NULL;
}

const char *
atm_chart_projection_v4_policy (const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ?
        atm_chart_projection_policy (wrapper->source_projection) : NULL;
}

guint
atm_chart_projection_v4_coordinate_count (const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ?
        atm_chart_spec_v4_coordinate_count (wrapper->owned_spec) : 0u;
}

guint
atm_chart_projection_v4_numeric_observation_count (
    const AtmChartProjectionV4 *wrapper)
{
    return wrapper != NULL ?
        atm_chart_spec_v4_numeric_observation_count (wrapper->owned_spec) : 0u;
}
