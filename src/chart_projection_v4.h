#pragma once

#include "chart_projection.h"
#include "chart_spec_v4.h"

G_BEGIN_DECLS

#define ATM_CHART_PROJECTION_V4_SCHEMA "atm-chart-projection/4"

typedef struct AtmChartProjectionV4 AtmChartProjectionV4;

typedef enum {
    ATM_CHART_PROJECTION_V4_ERROR_ARGUMENT,
    ATM_CHART_PROJECTION_V4_ERROR_POLICY,
    ATM_CHART_PROJECTION_V4_ERROR_IDENTITY,
    ATM_CHART_PROJECTION_V4_ERROR_COVERAGE
} AtmChartProjectionV4Error;

#define ATM_CHART_PROJECTION_V4_ERROR (atm_chart_projection_v4_error_quark ())
GQuark atm_chart_projection_v4_error_quark (void);

/*
 * Wraps a validated catalog-bound v4 spec around the existing source-specific
 * projection. All numeric conversion, bounds and display geometry remain owned
 * by the qualified legacy projection policy. No caller-authored values,
 * domains, source metadata or chart kinds are accepted.
 *
 * The wrapper owns rebuilt v4 identity and the source projection. Output must
 * be NULL on entry; failure leaves it unchanged. This type is not a renderer,
 * Application route or History persistence format.
 */
gboolean atm_chart_projection_v4_new (
    const AtmChartSpecV4 *spec,
    double width,
    double height,
    AtmChartProjectionV4 **out,
    GError **error
);

gboolean atm_chart_projection_v4_validate (
    const AtmChartProjectionV4 *projection,
    GError **error
);

void atm_chart_projection_v4_free (AtmChartProjectionV4 *projection);

/* Borrowed read-only objects owned by the wrapper. */
const AtmChartSpecV4 *atm_chart_projection_v4_spec (
    const AtmChartProjectionV4 *projection
);
const AtmChartProjection *atm_chart_projection_v4_source_projection (
    const AtmChartProjectionV4 *projection
);
const char *atm_chart_projection_v4_schema (
    const AtmChartProjectionV4 *projection
);
const char *atm_chart_projection_v4_series_id (
    const AtmChartProjectionV4 *projection
);
const char *atm_chart_projection_v4_policy (
    const AtmChartProjectionV4 *projection
);
guint atm_chart_projection_v4_coordinate_count (
    const AtmChartProjectionV4 *projection
);
guint atm_chart_projection_v4_numeric_observation_count (
    const AtmChartProjectionV4 *projection
);

G_END_DECLS
