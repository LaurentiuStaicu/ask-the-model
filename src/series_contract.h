#pragma once

#include <glib.h>
#include "scientific_result.h"

G_BEGIN_DECLS

#define ATM_SERIES_CONTRACT_SCHEMA "atm-verified-series/1"
#define ATM_SERIES_CONTRACT_NUMERIC_PROFILE "exact_decimal_coefficient_exponent/1"

#define ATM_SERIES_CONTRACT_MAX_POINTS 10000u
#define ATM_SERIES_CONTRACT_MAX_SUPPORT 32u
#define ATM_SERIES_CONTRACT_MAX_TEXT 256u
#define ATM_SERIES_CONTRACT_MAX_X 64u
#define ATM_SERIES_CONTRACT_MAX_NUMERIC 128u

typedef enum {
    ATM_SERIES_X_CALENDAR_YEAR = 1,
    ATM_SERIES_X_UTC_INSTANT = 2,
    ATM_SERIES_X_EVENT_ORDINAL = 3
} AtmSeriesXKind;

typedef enum {
    ATM_SERIES_Y_NUMERIC = 1,
    ATM_SERIES_Y_MISSING = 2
} AtmSeriesYStatus;

typedef enum {
    ATM_SERIES_CONTRACT_ERROR_ARGUMENT,
    ATM_SERIES_CONTRACT_ERROR_LIMIT,
    ATM_SERIES_CONTRACT_ERROR_SHAPE,
    ATM_SERIES_CONTRACT_ERROR_ORDER,
    ATM_SERIES_CONTRACT_ERROR_NUMERIC,
    ATM_SERIES_CONTRACT_ERROR_SUPPORT,
    ATM_SERIES_CONTRACT_ERROR_IDENTITY
} AtmSeriesContractError;

#define ATM_SERIES_CONTRACT_ERROR (atm_series_contract_error_quark ())

typedef struct {
    guint order;
    char *x;
    AtmSeriesYStatus y_status;
    char *source_decimal;
    char *coefficient;
    gint64 exponent;
    gboolean negative_zero;
    gboolean break_before;
    char *missing_reason;
    char *break_reason;
    GPtrArray *support;
} AtmSeriesContractPoint;

typedef struct {
    AtmSeriesXKind x_kind;
    char *subject;
    char *attribute;
    char *x_unit;
    char *x_dimension;
    char *y_unit;
    char *y_dimension;
    char *series_semantics;
    char *scenario;
    char *time_scope;
    char *epistemic_status;
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *source_path;
    char *profile_id;
    char *profile_version;
    GPtrArray *points;
    char *scientific_content_id;
    char *qualified_series_id;
} AtmSeriesContract;

GQuark atm_series_contract_error_quark (void);

void atm_series_contract_point_free (AtmSeriesContractPoint *point);
void atm_series_contract_free (AtmSeriesContract *series);

/* Synthetic/dormant constructor: all material is owned by the returned object. */
AtmSeriesContract *atm_series_contract_new (
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
    GError **error
);

gboolean atm_series_contract_add_point (
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
    GError **error
);

/* Qualification linkage is through the finalized/validated SRA, never ID strings alone. */
gboolean atm_series_contract_validate (
    AtmSeriesContract *series,
    const AtmSraResult *qualification,
    GError **error
);

const char *atm_series_contract_scientific_id (const AtmSeriesContract *series);
const char *atm_series_contract_qualified_id (const AtmSeriesContract *series);

G_END_DECLS
