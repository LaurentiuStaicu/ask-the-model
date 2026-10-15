#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CHART_CATALOG_SCHEMA "atm-chart-catalog/1"
#define ATM_CHART_CATALOG_VERSION "1"
#define ATM_CHART_CATALOG_EXPECTED_ENTRIES 3u
#define ATM_CHART_CATALOG_MAX_TEXT_BYTES 512u
#define ATM_CHART_CATALOG_MAX_ALIASES_PER_ENTRY 16u
#define ATM_CHART_CATALOG_MAX_SNAPSHOTS_PER_ENTRY 4u

typedef enum {
    ATM_CHART_CATALOG_ADAPTER_GISTEMP_V1 = 1,
    ATM_CHART_CATALOG_ADAPTER_ENERGY_INSTITUTE_V2 = 2,
    ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3 = 3
} AtmChartCatalogAdapter;

typedef enum {
    ATM_CHART_CATALOG_X_CALENDAR_YEAR = 1,
    ATM_CHART_CATALOG_X_CALENDAR_QUARTER = 2
} AtmChartCatalogXKind;

typedef enum {
    ATM_CHART_CATALOG_MISSING_REJECT = 1,
    ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE = 2
} AtmChartCatalogMissingPolicy;

typedef enum {
    ATM_CHART_CATALOG_LOOKUP_INVALID = 0,
    ATM_CHART_CATALOG_LOOKUP_NOT_FOUND,
    ATM_CHART_CATALOG_LOOKUP_FOUND,
    ATM_CHART_CATALOG_LOOKUP_AMBIGUOUS
} AtmChartCatalogLookupResult;

typedef enum {
    ATM_CHART_CATALOG_ERROR_ARGUMENT,
    ATM_CHART_CATALOG_ERROR_LIMIT,
    ATM_CHART_CATALOG_ERROR_SHAPE,
    ATM_CHART_CATALOG_ERROR_DUPLICATE_ID,
    ATM_CHART_CATALOG_ERROR_DUPLICATE_ALIAS,
    ATM_CHART_CATALOG_ERROR_PROFILE,
    ATM_CHART_CATALOG_ERROR_SOURCE_POLICY,
    ATM_CHART_CATALOG_ERROR_SEMANTICS
} AtmChartCatalogError;

#define ATM_CHART_CATALOG_ERROR (atm_chart_catalog_error_quark ())

/*
 * Immutable, compiled-in catalog entry. This is a selector/description of an
 * already-qualified adapter, not independent evidence and not a constructor
 * for a numeric series. Adding an entry requires an explicit reviewed adapter,
 * admission/reconstruction policy and a matching regression suite.
 */
typedef struct {
    const char *series_id;
    const char *display_name;
    const char *description;
    const char *const *aliases;
    gsize alias_count;

    AtmChartCatalogAdapter adapter;
    const char *adapter_id;
    const char *series_profile_id;
    const char *series_profile_version;
    const char *admission_profile_id;
    const char *admission_profile_version;
    const char *reconstruction_profile_id;
    const char *chart_spec_schema;
    const char *history_schema_version;

    const char *repository_key;
    const char *repository_full_name;
    const char *const *snapshot_shas;
    gsize snapshot_count;
    const char *source_path;

    AtmChartCatalogXKind x_kind;
    const char *x_coordinate_semantics;
    const char *x_frequency;
    const char *x_unit;
    const char *x_dimension;
    const char *subject;
    const char *attribute;
    const char *y_unit;
    const char *y_dimension;
    const char *series_semantics;
    const char *observation_meaning;
    const char *reference_period;
    const char *geography;

    AtmChartCatalogMissingPolicy missing_policy;
    const char *missing_coordinate;
    const char *missing_reason;

    const char *const *allowed_chart_kinds;
    gsize allowed_chart_kind_count;
    const char *display_profile_id;
    const char *display_profile_version;

    const char *publisher_name;
    const char *publisher_url;
    const char *pinned_artifact_description;
    const char *reconstruction_scope;
} AtmChartCatalogEntry;

GQuark atm_chart_catalog_error_quark (void);

/* Return the immutable compiled-in entries. No filesystem/repository scan. */
const AtmChartCatalogEntry *atm_chart_catalog_entries (gsize *out_count);
const AtmChartCatalogEntry *atm_chart_catalog_find_id (const char *series_id);

/*
 * Alias matching performs UTF-8 validation, canonical normalization,
 * case-folding and outer-whitespace trimming. It does not parse free-form
 * language or choose between multiple candidate series.
 */
AtmChartCatalogLookupResult atm_chart_catalog_find_alias (
    const char *alias,
    const AtmChartCatalogEntry **out_entry
);

/* Exact 40-hex snapshot check against this entry's closed allowlist. */
gboolean atm_chart_catalog_snapshot_admitted (
    const AtmChartCatalogEntry *entry,
    const char *snapshot_sha
);

/* Validate catalog integrity, alias uniqueness and exact adapter-policy tuples. */
gboolean atm_chart_catalog_validate (GError **error);

G_END_DECLS
