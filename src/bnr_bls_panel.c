#include "bnr_bls_panel.h"

#include <json-glib/json-glib.h>
#include <string.h>

#define BNR_MAX_SOURCE_BYTES ((gsize) 65536)
#define BNR_MAX_TOTAL_BYTES ((gsize) 131072)
#define BNR_EXPECTED_PANEL_SHA256 \
    "c5dad2c9577431e9765bc02b166e8d5a2b4c4b4c8e761fbb5bbce50f4e6498ff"
#define BNR_EXPECTED_INVENTORY_SHA256 \
    "9fd12a73499c51d6be200c3e6fba467fc4f0e8dbff33b946fcca8a4bec5b2bf9"
#define BNR_EXPECTED_PROMOTION_SHA256 \
    "2df50e5c1ab64032c449b6c1fbdc3c93c3e5622e6e221fd716f22fcf337aa380"
#define BNR_EXPECTED_CROSS_ROUND_SHA256 \
    "818e16c4ec0598a7c1410044b74c30bc3744cdb5533dba606a9904bb9420b07a"
#define BNR_EXPECTED_SEMANTIC_SHA256 \
    "679ef4f2e401d27ebb9eadadc20a603b4ce57e9296c9fd9bc695a0999b515b8c"
#define BNR_EXPECTED_PANEL_GIT_BLOB \
    "146d43794a5b284596c38af04279bed1859d72dc"

static const char *const source_paths[ATM_BNR_BLS_SOURCE_COUNT] = {
    "data/processed/bnr_bls_realised_rounds.csv",
    "data/provenance/processed_data_inventory_registry.json",
    "model/calibration_validation/bnr_bls_canonical_panel_promotion_review.json",
    "model/calibration_validation/bnr_bls_cross_round_mapping_audit.json",
    "model/calibration_validation/bnr_bls_missing_round_semantic_mapping_audit.json"
};

static const char *const source_sha256[ATM_BNR_BLS_SOURCE_COUNT] = {
    BNR_EXPECTED_PANEL_SHA256,
    BNR_EXPECTED_INVENTORY_SHA256,
    BNR_EXPECTED_PROMOTION_SHA256,
    BNR_EXPECTED_CROSS_ROUND_SHA256,
    BNR_EXPECTED_SEMANTIC_SHA256
};

static const char expected_header[] =
    "quarter,reference_date,source_id,source_kind,nfc_credit_standards,"
    "nfc_loan_demand,household_mortgage_credit_standards,"
    "household_consumer_credit_standards,household_mortgage_loan_demand,"
    "household_consumer_loan_demand";

static const char *const expected_quarters[] = {
    "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3", "2023-Q4",
    "2024-Q1", "2024-Q2", "2024-Q3", "2024-Q4", "2025-Q1", "2025-Q3"
};

static const char *const expected_source_ids[] = {
    "bls_2023_feb", "bls_2023_may", "bls_2023_aug", "bls_2023_nov",
    "bls_2024_feb", "bls_2024_may", "bls_2024_aug", "bls_2024_nov",
    "bls_2025_feb", "bls_2025_may_xlsx_review", "bls_2025_nov"
};

static const char *const expected_source_kinds[] = {
    "legacy_xls_full_cell_extraction",
    "legacy_xls_full_cell_extraction",
    "retained_legacy_xls_semantic_coordinate_review",
    "retained_legacy_xls_semantic_coordinate_review",
    "legacy_xls_full_cell_extraction",
    "legacy_xls_full_cell_extraction",
    "retained_legacy_xls_semantic_coordinate_review",
    "legacy_xls_full_cell_extraction",
    "legacy_xls_full_cell_extraction",
    "independently_reviewed_xlsx_layout",
    "legacy_xls_full_cell_extraction"
};

static const char *const expected_selected_decimals[] = {
    "11.89", "16.34", "15.54", "0.0", "0.0", "0.0",
    "0.0", "12.54", "12.08", "11.71", "9.46"
};

typedef struct {
    char *quarter;
    guint source_row;
    char *reference_date;
    char *source_id;
    char *source_kind;
    char *source_decimal;
    AtmScientificDecimal *canonical;
} BnrObservation;

struct AtmBnrBlsPanel {
    char *repository;
    char *snapshot_sha;
    GBytes *sources[ATM_BNR_BLS_SOURCE_COUNT];
    char *source_digests[ATM_BNR_BLS_SOURCE_COUNT];
    GPtrArray *observations;
    gboolean missing_quarter_authorized;
};

GQuark
atm_bnr_bls_panel_error_quark (void)
{
    return g_quark_from_static_string ("atm-bnr-bls-panel-error-quark");
}

static gboolean
fail (GError **error, AtmBnrBlsPanelError code, const char *message)
{
    g_set_error_literal (error, ATM_BNR_BLS_PANEL_ERROR, code, message);
    return FALSE;
}

static void
observation_free (gpointer data)
{
    BnrObservation *point = data;
    if (point == NULL)
        return;
    g_free (point->quarter);
    g_free (point->reference_date);
    g_free (point->source_id);
    g_free (point->source_kind);
    g_free (point->source_decimal);
    atm_scientific_decimal_free (point->canonical);
    g_free (point);
}

void
atm_bnr_bls_panel_free (AtmBnrBlsPanel *panel)
{
    if (panel == NULL)
        return;
    g_free (panel->repository);
    g_free (panel->snapshot_sha);
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        g_clear_pointer (&panel->sources[i], g_bytes_unref);
        g_free (panel->source_digests[i]);
    }
    g_clear_pointer (&panel->observations, g_ptr_array_unref);
    g_free (panel);
}

static gboolean
json_string_equals (JsonObject *object, const char *name, const char *expected)
{
    if (object == NULL || !json_object_has_member (object, name))
        return FALSE;
    JsonNode *node = json_object_get_member (object, name);
    return node != NULL &&
        JSON_NODE_HOLDS_VALUE (node) &&
        json_node_get_value_type (node) == G_TYPE_STRING &&
        g_strcmp0 (json_node_get_string (node), expected) == 0;
}

static gboolean
json_bool_equals (JsonObject *object, const char *name, gboolean expected)
{
    if (object == NULL || !json_object_has_member (object, name))
        return FALSE;
    JsonNode *node = json_object_get_member (object, name);
    return node != NULL &&
        JSON_NODE_HOLDS_VALUE (node) &&
        json_node_get_value_type (node) == G_TYPE_BOOLEAN &&
        json_node_get_boolean (node) == expected;
}

static gboolean
json_int_equals (JsonObject *object, const char *name, gint64 expected)
{
    if (object == NULL || !json_object_has_member (object, name))
        return FALSE;
    JsonNode *node = json_object_get_member (object, name);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
        return FALSE;
    GType type = json_node_get_value_type (node);
    if (type != G_TYPE_INT64 && type != G_TYPE_INT &&
        type != G_TYPE_LONG && type != G_TYPE_UINT64 &&
        type != G_TYPE_UINT)
        return FALSE;
    return json_node_get_int (node) == expected;
}

static gboolean
json_double_equals (JsonObject *object, const char *name, double expected)
{
    if (object == NULL || !json_object_has_member (object, name))
        return FALSE;
    JsonNode *node = json_object_get_member (object, name);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
        return FALSE;
    GType type = json_node_get_value_type (node);
    if (type != G_TYPE_DOUBLE && type != G_TYPE_INT64 &&
        type != G_TYPE_INT && type != G_TYPE_LONG &&
        type != G_TYPE_UINT64 && type != G_TYPE_UINT)
        return FALSE;
    return json_node_get_double (node) == expected;
}

static JsonObject *
json_object_member (JsonObject *parent, const char *name)
{
    if (parent == NULL || !json_object_has_member (parent, name))
        return NULL;
    JsonNode *node = json_object_get_member (parent, name);
    return node != NULL && JSON_NODE_HOLDS_OBJECT (node)
        ? json_node_get_object (node)
        : NULL;
}

static JsonArray *
json_array_member (JsonObject *parent, const char *name)
{
    if (parent == NULL || !json_object_has_member (parent, name))
        return NULL;
    JsonNode *node = json_object_get_member (parent, name);
    return node != NULL && JSON_NODE_HOLDS_ARRAY (node)
        ? json_node_get_array (node)
        : NULL;
}

static gboolean
array_equals_strings (
    JsonObject *object,
    const char *name,
    const char *const *expected,
    guint expected_count
)
{
    JsonArray *array = json_array_member (object, name);
    if (array == NULL || json_array_get_length (array) != expected_count)
        return FALSE;

    for (guint i = 0; i < expected_count; i++) {
        JsonNode *node = json_array_get_element (array, i);
        if (node == NULL ||
            !JSON_NODE_HOLDS_VALUE (node) ||
            json_node_get_value_type (node) != G_TYPE_STRING ||
            g_strcmp0 (json_node_get_string (node), expected[i]) != 0)
            return FALSE;
    }
    return TRUE;
}

static gboolean
array_contains_string (JsonArray *array, const char *expected)
{
    if (array == NULL)
        return FALSE;
    for (guint i = 0; i < json_array_get_length (array); i++) {
        JsonNode *node = json_array_get_element (array, i);
        if (node != NULL &&
            JSON_NODE_HOLDS_VALUE (node) &&
            json_node_get_value_type (node) == G_TYPE_STRING &&
            g_strcmp0 (json_node_get_string (node), expected) == 0)
            return TRUE;
    }
    return FALSE;
}

static gboolean
json_has_true_rules (
    JsonObject *object,
    const char *const *names,
    guint count
)
{
    for (guint i = 0; i < count; i++)
        if (!json_bool_equals (object, names[i], TRUE))
            return FALSE;
    return TRUE;
}

static gboolean
load_json_object (
    GBytes *bytes,
    JsonParser **out_parser,
    JsonObject **out_object,
    GError **error
)
{
    gsize size = 0;
    const guint8 *data = g_bytes_get_data (bytes, &size);
    JsonParser *parser = json_parser_new();
    GError *parse_error = NULL;

    if (!json_parser_load_from_data (
            parser, (const char *) data, (gssize) size, &parse_error)) {
        g_clear_error (&parse_error);
        g_object_unref (parser);
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_AUTHORITY,
                     "Pinned BNR authority is not valid JSON.");
    }

    JsonNode *root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root)) {
        g_object_unref (parser);
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_AUTHORITY,
                     "Pinned BNR authority must have an object root.");
    }

    *out_parser = parser;
    *out_object = json_node_get_object (root);
    return TRUE;
}

static gboolean
validate_inventory (JsonObject *root)
{
    static const char *const authority_paths[] = {
        "model/calibration_validation/bnr_bls_canonical_panel_promotion_review.json",
        "model/calibration_validation/bnr_bls_cross_round_mapping_audit.json"
    };
    static const char *const inventory_rules[] = {
        "every_processed_csv_must_be_registered",
        "every_registered_csv_must_exist",
        "git_blob_identity_must_match",
        "every_authority_path_must_exist",
        "inventory_does_not_upgrade_data_quality_or_model_status",
        "inventory_does_not_authorize_live_refetch",
        "inventory_does_not_authorize_estimation_or_behavioural_activation"
    };

    if (!json_string_equals (root, "registry_version", "0.1"))
        return FALSE;

    JsonObject *rules = json_object_member (root, "rules");
    if (!json_has_true_rules (rules, inventory_rules,
                              G_N_ELEMENTS (inventory_rules)))
        return FALSE;

    JsonArray *entries = json_array_member (root, "entries");
    if (entries == NULL)
        return FALSE;

    guint matches = 0;
    for (guint i = 0; i < json_array_get_length (entries); i++) {
        JsonNode *node = json_array_get_element (entries, i);
        if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
            return FALSE;
        JsonObject *entry = json_node_get_object (node);
        if (!json_string_equals (
                entry, "path", source_paths[ATM_BNR_BLS_SOURCE_PANEL]))
            continue;

        matches++;
        if (!json_string_equals (
                entry, "git_blob_sha1", BNR_EXPECTED_PANEL_GIT_BLOB) ||
            !json_string_equals (
                entry, "provenance_class", "ASSESSMENT_EMBEDDED"))
            return FALSE;

        JsonArray *authorities = json_array_member (entry, "authorities");
        for (guint j = 0; j < G_N_ELEMENTS (authority_paths); j++)
            if (!array_contains_string (authorities, authority_paths[j]))
                return FALSE;

        if (!json_object_has_member (entry, "role"))
            return FALSE;
        JsonNode *role = json_object_get_member (entry, "role");
        if (role == NULL ||
            !JSON_NODE_HOLDS_VALUE (role) ||
            json_node_get_value_type (role) != G_TYPE_STRING ||
            strstr (json_node_get_string (role), "2025-Q2") == NULL)
            return FALSE;
    }
    return matches == 1;
}

static gboolean
validate_promotion_review (JsonObject *root)
{
    static const char *const approved_quarters[] = {
        "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3", "2023-Q4",
        "2024-Q1", "2024-Q2", "2024-Q3", "2024-Q4", "2025-Q1", "2025-Q3"
    };
    static const char *const blank_dates[] = { "2023-Q2", "2024-Q2" };
    static const char *const missing_quarter[] = { ATM_BNR_BLS_MISSING_QUARTER };
    static const char *const hard_rules[] = {
        "no_interpolation", "no_forward_fill", "no_synthetic_quarter_creation",
        "no_synthetic_reference_date", "no_nfc_household_signal_aggregation",
        "no_parameter_estimation", "no_model_selection", "no_lag_selection",
        "no_holdout_opening", "no_causal_claim", "no_system_dynamics_activation",
        "no_behavioural_closure_change"
    };

    if (!json_string_equals (
            root, "review_result",
            "APPROVED_FOR_CANONICAL_PANEL_PROMOTION_WITH_SINGLE_EXPLICIT_GAP") ||
        !json_string_equals (
            root, "source_semantic_review",
            source_paths[ATM_BNR_BLS_SOURCE_SEMANTIC_AUDIT]))
        return FALSE;

    JsonObject *approved = json_object_member (root, "approved_post_promotion");
    if (!json_string_equals (approved, "canonical_panel_sha256",
                             BNR_EXPECTED_PANEL_SHA256) ||
        !json_int_equals (approved, "observed_round_count", 11) ||
        !json_int_equals (approved, "expected_quarter_count_between_bounds", 12) ||
        !json_double_equals (approved, "coverage_fraction", 11.0 / 12.0) ||
        !array_equals_strings (approved, "observed_quarters",
                               approved_quarters, G_N_ELEMENTS (approved_quarters)) ||
        !array_equals_strings (approved, "missing_quarters",
                              missing_quarter, G_N_ELEMENTS (missing_quarter)) ||
        !array_equals_strings (approved, "blank_reference_date_quarters",
                               blank_dates, G_N_ELEMENTS (blank_dates)))
        return FALSE;

    return json_has_true_rules (
        json_object_member (root, "hard_rules"),
        hard_rules, G_N_ELEMENTS (hard_rules));
}

static gboolean
validate_cross_round_audit (JsonObject *root)
{
    static const char *const missing_quarter[] = { ATM_BNR_BLS_MISSING_QUARTER };
    static const char *const hard_rules[] = {
        "no_nfc_household_signal_aggregation", "no_parameter_estimation",
        "no_model_selection", "no_lag_selection", "no_holdout_opening",
        "no_causal_claim", "no_dsti_level_claim",
        "no_system_dynamics_activation", "no_behavioural_closure_change"
    };

    if (!json_string_equals (
            root, "status", "PASS_BLS_MULTI_ROUND_MAPPING_WITH_SINGLE_EXPLICIT_GAP") ||
        !json_int_equals (root, "observed_round_count", 11) ||
        !json_string_equals (root, "first_observed_quarter", "2022-Q4") ||
        !json_string_equals (root, "last_observed_quarter", "2025-Q3") ||
        !json_int_equals (root, "expected_quarter_count_between_bounds", 12) ||
        !json_double_equals (root, "coverage_fraction", 11.0 / 12.0) ||
        !array_equals_strings (root, "missing_quarters",
                               missing_quarter, G_N_ELEMENTS (missing_quarter)) ||
        !json_bool_equals (root, "interpolation_performed", FALSE) ||
        !json_bool_equals (root, "synthetic_quarters_created", FALSE) ||
        !json_bool_equals (root, "parameter_estimation_performed", FALSE) ||
        !json_bool_equals (root, "model_selection_performed", FALSE) ||
        !json_bool_equals (root, "system_dynamics_activation", FALSE) ||
        !json_bool_equals (root, "behavioural_closure_change", FALSE) ||
        !json_has_true_rules (json_object_member (root, "hard_rules"),
                              hard_rules, G_N_ELEMENTS (hard_rules)))
        return FALSE;

    JsonObject *recovered = json_object_member (
        root, "recovered_missing_round_validations");
    JsonObject *promotion = json_object_member (
        root, "canonical_panel_promotion");
    return json_string_equals (
               recovered, "semantic_review",
               source_paths[ATM_BNR_BLS_SOURCE_SEMANTIC_AUDIT]) &&
           json_string_equals (
               recovered, "semantic_review_sha256",
               BNR_EXPECTED_SEMANTIC_SHA256) &&
           json_string_equals (
               promotion, "review",
               source_paths[ATM_BNR_BLS_SOURCE_PROMOTION_REVIEW]) &&
           json_string_equals (
               promotion, "promoted_canonical_panel_sha256",
               BNR_EXPECTED_PANEL_SHA256) &&
           json_string_equals (
               promotion, "remaining_gap",
               ATM_BNR_BLS_MISSING_QUARTER) &&
           json_bool_equals (
               promotion, "parameter_estimation_authorized", FALSE) &&
           json_bool_equals (
               promotion, "behavioural_closure_change", FALSE);
}

static gboolean
validate_semantic_audit (JsonObject *root)
{
    static const char *const hard_rules[] = {
        "no_canonical_panel_mutation", "no_interpolation",
        "no_synthetic_quarter_creation", "no_parameter_estimation",
        "no_model_selection", "no_lag_selection", "no_holdout_opening",
        "no_causal_claim", "no_dsti_level_claim",
        "no_system_dynamics_activation", "no_behavioural_closure_change"
    };

    return json_string_equals (
               root, "status",
               "PASS_MISSING_ROUND_SEMANTIC_COORDINATE_MAPPING_WITH_EXPLICIT_ROUND_IDENTITY_BRIDGES") &&
           json_bool_equals (root, "canonical_panel_modified", FALSE) &&
           json_bool_equals (root, "interpolation_performed", FALSE) &&
           json_bool_equals (root, "synthetic_quarters_created", FALSE) &&
           json_has_true_rules (json_object_member (root, "hard_rules"),
                                hard_rules, G_N_ELEMENTS (hard_rules)) &&
           json_string_equals (
               root, "pass_effect",
               "THREE_MISSING_BLS_ROUNDS_READY_FOR_EXPLICIT_CANONICAL_PANEL_REGENERATION_REVIEW_ONLY");
}

static gboolean
validate_authorities (AtmBnrBlsPanel *panel, GError **error)
{
    JsonParser *parsers[ATM_BNR_BLS_SOURCE_COUNT] = { NULL };
    JsonObject *objects[ATM_BNR_BLS_SOURCE_COUNT] = { NULL };
    gboolean ok = FALSE;

    for (guint i = ATM_BNR_BLS_SOURCE_INVENTORY;
         i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        if (!load_json_object (
                panel->sources[i], &parsers[i], &objects[i], error))
            goto out;
    }

    if (!validate_inventory (objects[ATM_BNR_BLS_SOURCE_INVENTORY]) ||
        !validate_promotion_review (objects[ATM_BNR_BLS_SOURCE_PROMOTION_REVIEW]) ||
        !validate_cross_round_audit (objects[ATM_BNR_BLS_SOURCE_CROSS_ROUND_AUDIT]) ||
        !validate_semantic_audit (objects[ATM_BNR_BLS_SOURCE_SEMANTIC_AUDIT])) {
        fail (error, ATM_BNR_BLS_PANEL_ERROR_AUTHORITY,
              "The pinned BNR inventory, promotion review and cross-round authorities do not agree on the single 2025-Q2 gap.");
        goto out;
    }

    ok = TRUE;
out:
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++)
        g_clear_object (&parsers[i]);
    return ok;
}

static gboolean
decimal_shape (const char *text)
{
    const char *p = text;
    gsize length = strlen (text);

    if (length == 0 || length > 64)
        return FALSE;
    if (*p == '-')
        p++;
    if (!g_ascii_isdigit (*p))
        return FALSE;
    if (*p == '0' && g_ascii_isdigit (p[1]))
        return FALSE;
    while (g_ascii_isdigit (*p))
        p++;
    if (*p != '.')
        return FALSE;
    p++;
    if (!g_ascii_isdigit (*p))
        return FALSE;
    while (g_ascii_isdigit (*p))
        p++;
    return *p == '\0';
}

static gboolean
parse_candidate (AtmBnrBlsPanel *panel, GError **error)
{
    gsize size = 0;
    const guint8 *data = g_bytes_get_data (
        panel->sources[ATM_BNR_BLS_SOURCE_PANEL], &size);

    if (size > BNR_MAX_SOURCE_BYTES || memchr (data, '\0', size) != NULL)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_SHAPE,
                     "The pinned BNR panel contains invalid CSV bytes.");

    char *text = g_strndup ((const char *) data, size);
    gchar **lines = g_strsplit (text, "\n", -1);
    guint line_count = g_strv_length (lines);
    if (line_count > 0 && lines[line_count - 1][0] == '\0')
        line_count--;

    if (line_count != G_N_ELEMENTS (expected_quarters) + 1) {
        g_strfreev (lines);
        g_free (text);
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_SHAPE,
                     "The canonical BNR panel must contain exactly eleven observed CSV rows.");
    }

    for (guint i = 0; i < line_count; i++) {
        gsize length = strlen (lines[i]);
        if (length > 0 && lines[i][length - 1] == '\r')
            lines[i][length - 1] = '\0';
        if (strchr (lines[i], '\r') != NULL || strchr (lines[i], '"') != NULL) {
            g_strfreev (lines);
            g_free (text);
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_SHAPE,
                         "The canonical BNR panel has unsupported CSV quoting or line breaks.");
        }
    }

    if (g_strcmp0 (lines[0], expected_header) != 0) {
        g_strfreev (lines);
        g_free (text);
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_SHAPE,
                     "The canonical BNR panel header does not match the reviewed ten-column layout.");
    }

    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        gchar **fields = g_strsplit (lines[i + 1], ",", -1);
        if (g_strv_length (fields) != 10) {
            g_strfreev (fields);
            g_strfreev (lines);
            g_free (text);
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_SHAPE,
                         "A canonical BNR panel row does not have exactly ten columns.");
        }

        gboolean blank_reference_date = i == 2 || i == 6;
        if (g_strcmp0 (fields[0], expected_quarters[i]) != 0 ||
            g_strcmp0 (fields[2], expected_source_ids[i]) != 0 ||
            g_strcmp0 (fields[3], expected_source_kinds[i]) != 0 ||
            (blank_reference_date
                ? fields[1][0] != '\0'
                : fields[1][0] == '\0') ||
            g_strcmp0 (fields[4], expected_selected_decimals[i]) != 0) {
            g_strfreev (fields);
            g_strfreev (lines);
            g_free (text);
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_ORDER,
                         "The BNR round identity, blank reference-date pattern or C01 value differs from the reviewed canonical panel.");
        }

        if (!decimal_shape (fields[4])) {
            g_strfreev (fields);
            g_strfreev (lines);
            g_free (text);
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_NUMBER,
                         "The BNR C01 net-percentage cell is not in exact decimal form.");
        }

        AtmScientificDecimal *canonical = NULL;
        GError *number_error = NULL;
        if (!atm_scientific_decimal_parse (
                fields[4], &canonical, &number_error)) {
            g_clear_error (&number_error);
            g_strfreev (fields);
            g_strfreev (lines);
            g_free (text);
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_NUMBER,
                         "The BNR C01 net-percentage cell is not an exact supported decimal.");
        }

        BnrObservation *point = g_new0 (BnrObservation, 1);
        point->quarter = g_strdup (fields[0]);
        point->source_row = i + 2u;
        if (fields[1][0] != '\0')
            point->reference_date = g_strdup (fields[1]);
        point->source_id = g_strdup (fields[2]);
        point->source_kind = g_strdup (fields[3]);
        point->source_decimal = g_strdup (fields[4]);
        point->canonical = canonical;
        g_ptr_array_add (panel->observations, point);
        g_strfreev (fields);
    }

    g_strfreev (lines);
    g_free (text);
    panel->missing_quarter_authorized = TRUE;
    return TRUE;
}

gboolean
atm_bnr_bls_panel_new (
    const char *repository,
    const char *snapshot_sha,
    GBytes *const sources[ATM_BNR_BLS_SOURCE_COUNT],
    AtmBnrBlsPanel **out_panel,
    GError **error
)
{
    if (repository == NULL || snapshot_sha == NULL || sources == NULL ||
        out_panel == NULL || *out_panel != NULL)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_ARGUMENT,
                     "Invalid BNR panel-admission arguments.");

    if (g_strcmp0 (repository, ATM_BNR_BLS_UPSTREAM_REPOSITORY) != 0)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_REPOSITORY,
                     "The BNR panel is pinned to one exact RMD repository.");
    if (g_strcmp0 (snapshot_sha, ATM_BNR_BLS_SNAPSHOT_SHA) != 0)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_SNAPSHOT,
                     "The RMD snapshot is not admitted for the BNR panel.");

    gsize total_bytes = 0;
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        if (sources[i] == NULL)
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_ARGUMENT,
                         "The complete five-file BNR authority bundle is required.");
        gsize size = g_bytes_get_size (sources[i]);
        if (size == 0 || size > BNR_MAX_SOURCE_BYTES)
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_LIMIT,
                         "A BNR source file is empty or exceeds the per-file limit.");
        if (size > BNR_MAX_TOTAL_BYTES - total_bytes)
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_LIMIT,
                         "The BNR source bundle exceeds the aggregate byte ceiling.");
        total_bytes += size;
    }

    AtmBnrBlsPanel *panel = g_new0 (AtmBnrBlsPanel, 1);
    panel->repository = g_strdup (repository);
    panel->snapshot_sha = g_strdup (snapshot_sha);
    panel->observations = g_ptr_array_new_with_free_func (observation_free);

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        gsize size = 0;
        const guint8 *data = g_bytes_get_data (sources[i], &size);
        panel->sources[i] = g_bytes_new (data, size);
        panel->source_digests[i] =
            g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, size);
        if (g_strcmp0 (panel->source_digests[i], source_sha256[i]) != 0) {
            atm_bnr_bls_panel_free (panel);
            return fail (error, ATM_BNR_BLS_PANEL_ERROR_DIGEST,
                         "A BNR panel or authority file does not match the pinned five-file bundle.");
        }
    }

    if (!validate_authorities (panel, error) ||
        !parse_candidate (panel, error)) {
        atm_bnr_bls_panel_free (panel);
        return FALSE;
    }

    *out_panel = panel;
    return TRUE;
}

gboolean
atm_bnr_bls_panel_validate (
    const AtmBnrBlsPanel *panel,
    GError **error
)
{
    if (panel == NULL)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_ARGUMENT,
                     "Missing BNR panel admission.");

    AtmBnrBlsPanel *rebuilt = NULL;
    if (!atm_bnr_bls_panel_new (
            panel->repository, panel->snapshot_sha,
            panel->sources, &rebuilt, error))
        return FALSE;

    gboolean equal =
        panel->missing_quarter_authorized ==
            rebuilt->missing_quarter_authorized &&
        panel->observations->len == rebuilt->observations->len;

    for (guint i = 0; equal && i < panel->observations->len; i++) {
        const BnrObservation *a = g_ptr_array_index (panel->observations, i);
        const BnrObservation *b = g_ptr_array_index (rebuilt->observations, i);
        equal = a != NULL && b != NULL &&
            a->source_row == b->source_row &&
            g_strcmp0 (a->quarter, b->quarter) == 0 &&
            g_strcmp0 (a->reference_date, b->reference_date) == 0 &&
            g_strcmp0 (a->source_id, b->source_id) == 0 &&
            g_strcmp0 (a->source_kind, b->source_kind) == 0 &&
            g_strcmp0 (a->source_decimal, b->source_decimal) == 0 &&
            g_strcmp0 (a->canonical->coefficient, b->canonical->coefficient) == 0 &&
            a->canonical->exponent == b->canonical->exponent;
    }

    atm_bnr_bls_panel_free (rebuilt);
    if (!equal)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_IDENTITY,
                     "The reconstructed BNR panel differs from its admitted source identity.");
    return TRUE;
}

gboolean
atm_bnr_bls_panel_rebuild (
    const AtmBnrBlsPanel *panel,
    AtmBnrBlsPanel **out_panel,
    GError **error
)
{
    if (panel == NULL || out_panel == NULL || *out_panel != NULL)
        return fail (error, ATM_BNR_BLS_PANEL_ERROR_ARGUMENT,
                     "Invalid BNR panel rebuild arguments.");

    if (!atm_bnr_bls_panel_validate (panel, error))
        return FALSE;

    return atm_bnr_bls_panel_new (
        panel->repository,
        panel->snapshot_sha,
        panel->sources,
        out_panel,
        error
    );
}

const char *
atm_bnr_bls_panel_repository (const AtmBnrBlsPanel *panel)
{
    return panel != NULL ? panel->repository : NULL;
}

const char *
atm_bnr_bls_panel_snapshot (const AtmBnrBlsPanel *panel)
{
    return panel != NULL ? panel->snapshot_sha : NULL;
}

guint
atm_bnr_bls_panel_observation_count (const AtmBnrBlsPanel *panel)
{
    return panel != NULL ? panel->observations->len : 0;
}

gboolean
atm_bnr_bls_panel_observation (
    const AtmBnrBlsPanel *panel,
    guint index,
    AtmBnrBlsObservation *out_observation
)
{
    if (panel == NULL || out_observation == NULL ||
        index >= panel->observations->len)
        return FALSE;

    const BnrObservation *point = g_ptr_array_index (panel->observations, index);
    out_observation->quarter = point->quarter;
    out_observation->source_row = point->source_row;
    out_observation->reference_date = point->reference_date;
    out_observation->source_id = point->source_id;
    out_observation->source_kind = point->source_kind;
    out_observation->source_decimal = point->source_decimal;
    out_observation->coefficient = point->canonical->coefficient;
    out_observation->exponent = point->canonical->exponent;
    return TRUE;
}

const char *
atm_bnr_bls_panel_missing_quarter (const AtmBnrBlsPanel *panel)
{
    return panel != NULL && panel->missing_quarter_authorized
        ? ATM_BNR_BLS_MISSING_QUARTER : NULL;
}

const char *
atm_bnr_bls_panel_missing_reason (const AtmBnrBlsPanel *panel)
{
    return panel != NULL && panel->missing_quarter_authorized
        ? ATM_BNR_BLS_MISSING_REASON : NULL;
}

const char *
atm_bnr_bls_panel_source_path (
    const AtmBnrBlsPanel *panel,
    guint index
)
{
    return panel != NULL && index < ATM_BNR_BLS_SOURCE_COUNT
        ? source_paths[index] : NULL;
}

const char *
atm_bnr_bls_panel_source_sha256 (
    const AtmBnrBlsPanel *panel,
    guint index
)
{
    return panel != NULL && index < ATM_BNR_BLS_SOURCE_COUNT
        ? panel->source_digests[index] : NULL;
}
