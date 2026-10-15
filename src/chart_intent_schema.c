#include "chart_intent_schema.h"
#include "chart_catalog.h"

#include <json-glib/json-glib.h>
#include <string.h>

static gboolean
fail (GError **error, AtmChartIntentSchemaError code, const gchar *message)
{
    if (error != NULL)
        g_set_error_literal (error, ATM_CHART_INTENT_SCHEMA_ERROR, code, message);
    return FALSE;
}

GQuark
atm_chart_intent_schema_error_quark (void)
{
    return g_quark_from_static_string ("atm-chart-intent-schema-error-quark");
}

void
atm_chart_intent_proposal_clear (AtmChartIntentProposal *proposal)
{
    if (proposal == NULL)
        return;

    g_clear_pointer (&proposal->series_id, g_free);
    memset (proposal, 0, sizeof *proposal);
}

static gboolean
has_exact_fields (JsonObject *object, GError **error)
{
    static const gchar *const required[] = {
        "schema", "intent", "series_id", "chart_kind", "response_mode", "language"
    };

    if (json_object_get_size (object) != G_N_ELEMENTS (required))
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE,
            "Intent object must contain exactly the six required fields.");

    for (gsize i = 0; i < G_N_ELEMENTS (required); i++) {
        if (!json_object_has_member (object, required[i]))
            return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE,
                "Intent object is missing a required field.");
    }
    return TRUE;
}

static gboolean
get_string_field (
    JsonObject *object,
    const gchar *key,
    const gchar **out_value,
    GError **error
)
{
    JsonNode *node = json_object_get_member (object, key);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_STRING)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "A required intent field has the wrong JSON type.");

    const gchar *value = json_node_get_string (node);
    if (value == NULL)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "A required intent field must be a non-null string.");

    *out_value = value;
    return TRUE;
}

static gboolean
parse_intent_kind (
    const gchar *value,
    AtmChartIntentKind *out_kind
)
{
    if (g_str_equal (value, "chart"))
        *out_kind = ATM_CHART_INTENT_CHART;
    else if (g_str_equal (value, "interpretation"))
        *out_kind = ATM_CHART_INTENT_INTERPRETATION;
    else if (g_str_equal (value, "data_extract"))
        *out_kind = ATM_CHART_INTENT_DATA_EXTRACT;
    else if (g_str_equal (value, "not_chart"))
        *out_kind = ATM_CHART_INTENT_NOT_CHART;
    else if (g_str_equal (value, "ambiguous"))
        *out_kind = ATM_CHART_INTENT_AMBIGUOUS;
    else
        return FALSE;

    return TRUE;
}

static gboolean
parse_response_mode (
    const gchar *value,
    AtmChartIntentResponseMode *out_mode
)
{
    if (g_str_equal (value, "chart_only"))
        *out_mode = ATM_CHART_INTENT_RESPONSE_CHART_ONLY;
    else if (g_str_equal (value, "chart_with_interpretation"))
        *out_mode = ATM_CHART_INTENT_RESPONSE_CHART_WITH_INTERPRETATION;
    else if (g_str_equal (value, "data_extract"))
        *out_mode = ATM_CHART_INTENT_RESPONSE_DATA_EXTRACT;
    else if (g_str_equal (value, "none"))
        *out_mode = ATM_CHART_INTENT_RESPONSE_NONE;
    else
        return FALSE;

    return TRUE;
}

static gboolean
parse_language (
    const gchar *value,
    AtmChartIntentLanguage *out_language
)
{
    if (g_str_equal (value, "en"))
        *out_language = ATM_CHART_INTENT_LANGUAGE_EN;
    else if (g_str_equal (value, "ro"))
        *out_language = ATM_CHART_INTENT_LANGUAGE_RO;
    else if (g_str_equal (value, "other"))
        *out_language = ATM_CHART_INTENT_LANGUAGE_OTHER;
    else
        return FALSE;

    return TRUE;
}

static gboolean
parse_nullable_series (
    JsonObject *object,
    AtmChartIntentProposal *proposal,
    GError **error
)
{
    JsonNode *node = json_object_get_member (object, "series_id");
    if (node == NULL)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE,
            "Intent object is missing series_id.");

    if (JSON_NODE_HOLDS_NULL (node)) {
        proposal->series_id = NULL;
        return TRUE;
    }

    if (!JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_STRING)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "series_id must be a registered string or null.");

    const gchar *series_id = json_node_get_string (node);
    if (series_id == NULL || *series_id == '\0')
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "series_id must not be an empty string.");

    if (atm_chart_catalog_find_id (series_id) == NULL)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_UNKNOWN_SERIES,
            "series_id is not registered in the static eligible-series catalog.");

    proposal->series_id = g_strdup (series_id);
    return TRUE;
}

static gboolean
parse_nullable_chart_kind (
    JsonObject *object,
    AtmChartIntentProposal *proposal,
    GError **error
)
{
    JsonNode *node = json_object_get_member (object, "chart_kind");
    if (node == NULL)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE,
            "Intent object is missing chart_kind.");

    if (JSON_NODE_HOLDS_NULL (node)) {
        proposal->has_chart_kind = FALSE;
        proposal->chart_kind_line = FALSE;
        return TRUE;
    }

    if (!JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_STRING)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "chart_kind must be line or null.");

    const gchar *kind = json_node_get_string (node);
    if (kind == NULL || !g_str_equal (kind, "line"))
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "Only the registered line chart kind is supported.");

    proposal->has_chart_kind = TRUE;
    proposal->chart_kind_line = TRUE;
    return TRUE;
}

static gboolean
consistent (const AtmChartIntentProposal *p)
{
    gboolean selected = p->series_id != NULL;
    gboolean line = p->has_chart_kind && p->chart_kind_line;

    switch (p->intent) {
    case ATM_CHART_INTENT_CHART:
        /* The chart intent is reserved for a chart-only response. A chart
         * accompanied by interpretation must use intent=interpretation. */
        return selected && line &&
            p->response_mode == ATM_CHART_INTENT_RESPONSE_CHART_ONLY;

    case ATM_CHART_INTENT_INTERPRETATION:
        if (!selected)
            return FALSE;
        if (p->response_mode == ATM_CHART_INTENT_RESPONSE_CHART_WITH_INTERPRETATION)
            return line;
        if (p->response_mode == ATM_CHART_INTENT_RESPONSE_NONE)
            return !p->has_chart_kind;
        return FALSE;

    case ATM_CHART_INTENT_DATA_EXTRACT:
        return selected && !p->has_chart_kind &&
            p->response_mode == ATM_CHART_INTENT_RESPONSE_DATA_EXTRACT;

    case ATM_CHART_INTENT_NOT_CHART:
    case ATM_CHART_INTENT_AMBIGUOUS:
        return !selected && !p->has_chart_kind &&
            p->response_mode == ATM_CHART_INTENT_RESPONSE_NONE;
    }

    return FALSE;
}

gboolean
atm_chart_intent_proposal_parse (
    const gchar *json,
    gsize json_length,
    AtmChartIntentProposal *out_proposal,
    GError **error
)
{
    if (out_proposal == NULL)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_ARGUMENT,
            "out_proposal must not be NULL.");

    memset (out_proposal, 0, sizeof *out_proposal);

    if (json == NULL || json_length == 0)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_ARGUMENT,
            "JSON input must not be NULL or empty.");

    if (json_length > ATM_CHART_INTENT_JSON_MAX_BYTES)
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_TOO_LARGE,
            "Intent JSON exceeds the bounded input size.");

    JsonParser *parser = json_parser_new ();
    GError *parse_error = NULL;
    if (!json_parser_load_from_data (parser, json, (gssize) json_length, &parse_error)) {
        if (error != NULL)
            g_set_error (error, ATM_CHART_INTENT_SCHEMA_ERROR,
                ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_JSON,
                "Invalid intent JSON: %s",
                parse_error != NULL ? parse_error->message : "parse failed");
        g_clear_error (&parse_error);
        g_object_unref (parser);
        return FALSE;
    }
    g_clear_error (&parse_error);

    JsonNode *root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root)) {
        g_object_unref (parser);
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_SHAPE,
            "Intent JSON root must be an object.");
    }

    JsonObject *object = json_node_get_object (root);
    if (!has_exact_fields (object, error)) {
        g_object_unref (parser);
        return FALSE;
    }

    AtmChartIntentProposal proposal = { 0 };
    const gchar *schema = NULL;
    const gchar *intent = NULL;
    const gchar *response_mode = NULL;
    const gchar *language = NULL;

    gboolean ok =
        get_string_field (object, "schema", &schema, error) &&
        get_string_field (object, "intent", &intent, error) &&
        get_string_field (object, "response_mode", &response_mode, error) &&
        get_string_field (object, "language", &language, error);

    if (!ok) {
        g_object_unref (parser);
        return FALSE;
    }

    if (!g_str_equal (schema, ATM_CHART_INTENT_SCHEMA_ID)) {
        g_object_unref (parser);
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "Unsupported intent schema identity.");
    }

    if (!parse_intent_kind (intent, &proposal.intent) ||
        !parse_response_mode (response_mode, &proposal.response_mode) ||
        !parse_language (language, &proposal.language)) {
        g_object_unref (parser);
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INVALID_FIELD,
            "Intent, response_mode or language has an unknown enum value.");
    }

    if (!parse_nullable_series (object, &proposal, error) ||
        !parse_nullable_chart_kind (object, &proposal, error)) {
        atm_chart_intent_proposal_clear (&proposal);
        g_object_unref (parser);
        return FALSE;
    }

    if (!consistent (&proposal)) {
        atm_chart_intent_proposal_clear (&proposal);
        g_object_unref (parser);
        return fail (error, ATM_CHART_INTENT_SCHEMA_ERROR_INCONSISTENT,
            "Intent, series_id, chart_kind and response_mode are inconsistent.");
    }

    *out_proposal = proposal;
    g_object_unref (parser);
    return TRUE;
}
