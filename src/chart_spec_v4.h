#pragma once

#include "chart_catalog.h"
#include "chart_spec.h"

G_BEGIN_DECLS

#define ATM_CHART_SPEC_V4_SCHEMA "atm-chart-spec/4"
#define ATM_CHART_SPEC_V4_ID_DOMAIN "atm.chart-spec.v4"

/*
 * Catalog-bound generic ChartSpec envelope. This first v4 slice is deliberately
 * not a renderer, Application or History input. It wraps one independently
 * qualified v1/v2/v3 source spec and never accepts caller-authored point data.
 */
typedef struct AtmChartSpecV4 AtmChartSpecV4;

typedef enum {
    ATM_CHART_SPEC_V4_ERROR_ARGUMENT,
    ATM_CHART_SPEC_V4_ERROR_CATALOG,
    ATM_CHART_SPEC_V4_ERROR_PROFILE,
    ATM_CHART_SPEC_V4_ERROR_SEMANTICS,
    ATM_CHART_SPEC_V4_ERROR_COVERAGE,
    ATM_CHART_SPEC_V4_ERROR_IDENTITY
} AtmChartSpecV4Error;

#define ATM_CHART_SPEC_V4_ERROR (atm_chart_spec_v4_error_quark ())

GQuark atm_chart_spec_v4_error_quark (void);

/*
 * Construct from a pointer-identical static catalog entry and a fully
 * qualified source-specific v1/v2/v3 ChartSpec. The source spec is validated
 * and rebuilt into owned state; the catalog's exact snapshot/path/profile,
 * dimensions, coordinate coverage and missing policy are independently
 * checked. Only one LINE series is eligible in the first v4 envelope.
 *
 * On failure *out is not changed. No generic rendering or persistence
 * authority is implied by success.
 */
gboolean atm_chart_spec_v4_new (
    const AtmChartCatalogEntry *entry,
    const AtmChartSpec *qualified_source_spec,
    AtmChartSpecV4 **out,
    GError **error
);

/* Revalidates the catalog binding, source spec and v4 identity. */
gboolean atm_chart_spec_v4_validate (
    const AtmChartSpecV4 *spec,
    GError **error
);

/* Rebuilds both the owned source spec and v4 identity. *out must be NULL. */
gboolean atm_chart_spec_v4_rebuild (
    const AtmChartSpecV4 *spec,
    AtmChartSpecV4 **out,
    GError **error
);

void atm_chart_spec_v4_free (AtmChartSpecV4 *spec);

/* Read-only accessors. Returned pointers are borrowed from the immutable spec. */
const char *atm_chart_spec_v4_schema (const AtmChartSpecV4 *spec);
const char *atm_chart_spec_v4_id (const AtmChartSpecV4 *spec);
const char *atm_chart_spec_v4_series_id (const AtmChartSpecV4 *spec);
const AtmChartCatalogEntry *atm_chart_spec_v4_catalog_entry (const AtmChartSpecV4 *spec);
const AtmChartSpec *atm_chart_spec_v4_source_spec (const AtmChartSpecV4 *spec);
AtmChartKind atm_chart_spec_v4_kind (const AtmChartSpecV4 *spec);
guint atm_chart_spec_v4_coordinate_count (const AtmChartSpecV4 *spec);
guint atm_chart_spec_v4_numeric_observation_count (const AtmChartSpecV4 *spec);
const char *atm_chart_spec_v4_missing_coordinate (const AtmChartSpecV4 *spec);
const char *atm_chart_spec_v4_missing_reason (const AtmChartSpecV4 *spec);

#ifdef ATM_CHART_SPEC_V4_TESTING
/* Test-only identity hook: exercises canonicalization with metadata variants. */
gboolean atm_chart_spec_v4_test_build_id (
    const AtmChartCatalogEntry *entry,
    const AtmChartSpec *source,
    char **out_id,
    GError **error
);
#endif

G_END_DECLS
