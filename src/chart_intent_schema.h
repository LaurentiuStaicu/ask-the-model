#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CHART_INTENT_SCHEMA_ID "atm-chart-intent/1"
#define ATM_CHART_INTENT_JSON_MAX_BYTES 4096

typedef enum {
    ATM_CHART_INTENT_CHART,
    ATM_CHART_INTENT_INTERPRETATION,
    ATM_CHART_INTENT_DATA_EXTRACT,
    ATM_CHART_INTENT_NOT_CHART,
    ATM_CHART_INTENT_AMBIGUOUS
} AtmChartIntentKind;

typedef enum {
    ATM_CHART_INTENT_RESPONSE_CHART_ONLY,
    ATM_CHART_INTENT_RESPONSE_CHART_WITH_INTERPRETATION,
    ATM_CHART_INTENT_RESPONSE_DATA_EXTRACT,
    ATM_CHART_INTENT_RESPONSE_NONE
} AtmChartIntentResponseMode;

typedef enum {
    ATM_CHART_INTENT_LANGUAGE_EN,
    ATM_CHART_INTENT_LANGUAGE_RO,
    ATM_CHART_INTENT_LANGUAGE_OTHER
} AtmChartIntentLanguage;

typedef struct {
    AtmChartIntentKind intent;
    gchar *series_id; /* owned; NULL for not_chart/ambiguous */
    gboolean has_chart_kind;
    gboolean chart_kind_line; /* the only supported non-null chart kind */
    AtmChartIntentResponseMode response_mode;
    AtmChartIntentLanguage language;
} AtmChartIntentProposal;

typedef enum {
    ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_ARGUMENT,
    ATM_CHART_INTENT_SCHEMA_ERROR_TOO_LARGE,
    ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_JSON,
    ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE,
    ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
    ATM_CHART_INTENT_SCHEMA_ERROR_UNKNOWN_SERIES,
    ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT
} AtmChartIntentSchemaError;

#define ATM_CHART_INTENT_SCHEMA_ERROR (atm_chart_intent_schema_error_quark ())
GQuark atm_chart_intent_schema_error_quark (void);

/*
 * Parse a bounded JSON proposal. The parser validates structure, enum values,
 * catalog membership and cross-field consistency. It does not authorize chart
 * rendering; source reconstruction and application policy remain separate.
 *
 * On failure, out_proposal is zeroed and error (if supplied) explains the
 * validation failure. On success, call atm_chart_intent_proposal_clear().
 */
gboolean atm_chart_intent_proposal_parse (
    const gchar *json,
    gsize json_length,
    AtmChartIntentProposal *out_proposal,
    GError **error
);

void atm_chart_intent_proposal_clear (AtmChartIntentProposal *proposal);

G_END_DECLS
