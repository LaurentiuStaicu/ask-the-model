#pragma once
#include "verified_series.h"
#include "energy_institute_verified_series.h"
#include "bnr_bls_verified_series.h"
#include <glib.h>
G_BEGIN_DECLS

#define ATM_CHART_SPEC_SCHEMA "atm-chart-spec/1"
#define ATM_CHART_SPEC_V2_SCHEMA "atm-chart-spec/2"
#define ATM_CHART_SPEC_V3_SCHEMA "atm-chart-spec/3"
#define ATM_CHART_SPEC_V3_BNR_DISPLAY_PROFILE "atm-display/bnr-bls-c01-quarterly-gap"
#define ATM_CHART_SPEC_V3_BNR_DISPLAY_PROFILE_VERSION "1"
#define ATM_CHART_SPEC_MAX_SERIES 4
#define ATM_CHART_SPEC_V2_EI_DISPLAY_PROFILE "atm-display/energy-institute-tes-linear"
#define ATM_CHART_SPEC_V2_EI_DISPLAY_PROFILE_VERSION "1"

/* STEP and BAR are reserved, not admitted by either current profile. */
typedef enum { ATM_CHART_LINE = 1, ATM_CHART_SCATTER, ATM_CHART_STEP, ATM_CHART_BAR } AtmChartKind;
typedef enum {
    ATM_CHART_SPEC_VERSION_GISTEMP_V1 = 1,
    ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2 = 2,
    ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3 = 3
} AtmChartSpecVersion;
typedef enum {
    ATM_CHART_POINT_NUMERIC = 1,
    ATM_CHART_POINT_MISSING = 2
} AtmChartPointStatus;

typedef struct AtmChartSpec AtmChartSpec;

/* Borrowed source-neutral view of one point. String and support pointers remain
 * valid until the owning ChartSpec is destroyed. Current admitted profiles use
 * calendar-year X coordinates; a future X kind requires a separately reviewed
 * accessor contract rather than implicit conversion. */
typedef struct {
    guint order;
    guint calendar_year;
    const char *calendar_quarter; /* Non-NULL only for the BNR C01 quarterly profile. */
    guint source_row;
    const char *missing_reason; /* Borrowed; non-NULL only for a qualified MISSING point. */
    AtmChartPointStatus y_status;
    const char *source_decimal;
    const char *coefficient;
    gint64 exponent;
    gboolean break_before;
    const GPtrArray *support;
} AtmChartPointView;

/* Legacy GISTEMP v1 constructor. Its identity serialization and domain remain
 * unchanged so existing chart IDs and History rows stay byte-for-byte stable. */
gboolean atm_chart_spec_new (AtmChartKind kind, const AtmVerifiedSeries *const *series,
    gsize count, AtmChartSpec **out, GError **error);

/* Source-specific Energy Institute v2 constructor. Accepts only the opaque,
 * independently reconstructing EI series object; never caller-supplied points
 * or a generic AtmSeriesContract. It is native/dormant and does not authorize
 * display projection, application intent, rendering or History persistence. */
gboolean atm_chart_spec_new_energy_institute_v2 (
    AtmChartKind kind,
    const AtmEnergyInstituteVerifiedSeries *series,
    AtmChartSpec **out,
    GError **error);

/* Source-specific BNR BLS C01 quarterly ChartSpec v3. It admits only the
 * opaque pinned-profile VerifiedSeries object, including its one qualified
 * MISSING point. This constructs a native semantic spec only; it does not
 * grant projection, rendering, Application or History authority. */
gboolean atm_chart_spec_new_bnr_bls_c01_v3 (
    AtmChartKind kind,
    const AtmBnrBlsVerifiedSeries *series,
    AtmChartSpec **out,
    GError **error);

gboolean atm_chart_spec_validate (const AtmChartSpec *spec, GError **error);
/* Reject altered materializations before reconstructing owned inputs. */
gboolean atm_chart_spec_rebuild (const AtmChartSpec *spec, AtmChartSpec **out, GError **error);
void atm_chart_spec_free (AtmChartSpec *spec);

/* Generic identity/accessors. Pointers are borrowed read-only. */
AtmChartSpecVersion atm_chart_spec_version (const AtmChartSpec *spec);
const char *atm_chart_spec_schema (const AtmChartSpec *spec);
const char *atm_chart_spec_id (const AtmChartSpec *spec);
AtmChartKind atm_chart_spec_kind (const AtmChartSpec *spec);
guint atm_chart_spec_count (const AtmChartSpec *spec);
guint atm_chart_spec_point_count (const AtmChartSpec *spec, guint series_index);
gboolean atm_chart_spec_point (const AtmChartSpec *spec, guint series_index,
    guint point_index, AtmChartPointView *out);
const char *atm_chart_spec_x_unit (const AtmChartSpec *spec);
const char *atm_chart_spec_x_dimension (const AtmChartSpec *spec);
const char *atm_chart_spec_y_unit (const AtmChartSpec *spec);
const char *atm_chart_spec_y_dimension (const AtmChartSpec *spec);
const char *atm_chart_spec_series_profile (const AtmChartSpec *spec);
const char *atm_chart_spec_series_snapshot_sha (const AtmChartSpec *spec, guint index);
const char *atm_chart_spec_series_source_path (const AtmChartSpec *spec, guint index);
const char *atm_chart_spec_series_repository_id (const AtmChartSpec *spec, guint index);

const AtmVerifiedSeries *atm_chart_spec_series (const AtmChartSpec *spec, guint index);
const char *atm_chart_spec_series_scientific_id (const AtmChartSpec *spec, guint index);
const char *atm_chart_spec_series_qualified_id (const AtmChartSpec *spec, guint index);
/* Annual profiles expose numeric year extents. BNR v3 exposes ordinal
 * x_min/x_max plus these authoritative quarter-label extents. */
guint atm_chart_spec_x_min (const AtmChartSpec *spec);
guint atm_chart_spec_x_max (const AtmChartSpec *spec);
const char *atm_chart_spec_calendar_quarter_min (const AtmChartSpec *spec);
const char *atm_chart_spec_calendar_quarter_max (const AtmChartSpec *spec);
const AtmScientificDecimal *atm_chart_spec_y_min (const AtmChartSpec *spec);
const AtmScientificDecimal *atm_chart_spec_y_max (const AtmChartSpec *spec);
G_END_DECLS
