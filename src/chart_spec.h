#pragma once
#include "verified_series.h"
G_BEGIN_DECLS
#define ATM_CHART_SPEC_SCHEMA "atm-chart-spec/1"
#define ATM_CHART_SPEC_MAX_SERIES 4
/* STEP and BAR are reserved, not admitted by this profile. */
typedef enum { ATM_CHART_LINE = 1, ATM_CHART_SCATTER, ATM_CHART_STEP, ATM_CHART_BAR } AtmChartKind;
typedef struct AtmChartSpec AtmChartSpec;
/* Only complete GISTEMP VerifiedSeries currently exist. Inputs are validated
 * before rebuilding owned copies. *out must be NULL; failure preserves it.
 * Axes are linear, missing policy GAP, extents exact and data-derived.
 * No caller-provided unit, scale, values, or domain is accepted. */
gboolean atm_chart_spec_new (AtmChartKind kind, const AtmVerifiedSeries *const *series,
    gsize count, AtmChartSpec **out, GError **error);
gboolean atm_chart_spec_validate (const AtmChartSpec *spec, GError **error);
/* Reject altered materializations before reconstructing owned inputs. */
gboolean atm_chart_spec_rebuild (const AtmChartSpec *spec, AtmChartSpec **out, GError **error);
void atm_chart_spec_free (AtmChartSpec *spec);
/* All returned pointers are borrowed read-only, valid until destruction. */
const char *atm_chart_spec_id (const AtmChartSpec *spec);
AtmChartKind atm_chart_spec_kind (const AtmChartSpec *spec);
guint atm_chart_spec_count (const AtmChartSpec *spec);
const AtmVerifiedSeries *atm_chart_spec_series (const AtmChartSpec *spec, guint index);
const char *atm_chart_spec_series_scientific_id (const AtmChartSpec *spec, guint index);
const char *atm_chart_spec_series_qualified_id (const AtmChartSpec *spec, guint index);
guint atm_chart_spec_x_min (const AtmChartSpec *spec);
guint atm_chart_spec_x_max (const AtmChartSpec *spec);
const AtmScientificDecimal *atm_chart_spec_y_min (const AtmChartSpec *spec);
const AtmScientificDecimal *atm_chart_spec_y_max (const AtmChartSpec *spec);
G_END_DECLS
