#include "bnr_bls_series_contract.h"

#include <json-glib/json-glib.h>
#include <string.h>

#define BNR_SUBJECT "non_financial_corporations"
#define BNR_ATTRIBUTE "nfc_credit_standards"
#define BNR_UNIT "percent"
#define BNR_DIMENSION "survey_net_percentage"
#define BNR_SCENARIO "not_applicable"
#define BNR_EPISTEMIC_STATUS "empirical"
#define BNR_SERIES_SEMANTICS "empirical_quarterly_observations"
#define BNR_TIME_SCOPE "2022-Q4..2025-Q3"

static const char *const quarters[] = {
    "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3",
    "2023-Q4", "2024-Q1", "2024-Q2", "2024-Q3",
    "2024-Q4", "2025-Q1", "2025-Q2", "2025-Q3"
};

struct AtmBnrBlsSeriesContract {
    AtmBnrBlsPanel *panel;
    AtmBnrBlsSra *sra;
    AtmSeriesContract *contract;
};

GQuark
atm_bnr_bls_series_contract_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-bnr-bls-series-contract-error-quark"
    );
}

static gboolean
reject (
    GError **error,
    AtmBnrBlsSeriesContractError code,
    const char *message
)
{
    g_set_error_literal (
        error,
        ATM_BNR_BLS_SERIES_CONTRACT_ERROR,
        code,
        message
    );
    return FALSE;
}

static gboolean
same_string (const char *a, const char *b)
{
    return g_strcmp0 (a, b) == 0;
}

static const AtmSraEstablishedFact *
find_fact (const AtmSraResult *result, const char *fact_id)
{
    if (result == NULL || result->established_facts == NULL)
        return NULL;

    for (guint i = 0; i < result->established_facts->len; i++) {
        const AtmSraEstablishedFact *fact =
            g_ptr_array_index (result->established_facts, i);
        if (fact != NULL && same_string (fact->fact_id, fact_id))
            return fact;
    }

    return NULL;
}

static gboolean
evidence_ids_contains (const AtmSraResult *result, const char *id)
{
    if (result == NULL || result->qualification == NULL ||
        result->qualification->evidence_atom_ids == NULL || id == NULL)
        return FALSE;

    for (guint i = 0;
         i < result->qualification->evidence_atom_ids->len;
         i++) {
        if (same_string (
                id,
                g_ptr_array_index (
                    result->qualification->evidence_atom_ids,
                    i
                )))
            return TRUE;
    }

    return FALSE;
}

static gboolean
support_contains (const GPtrArray *support, const char *id)
{
    if (support == NULL || id == NULL)
        return FALSE;

    for (guint i = 0; i < support->len; i++) {
        if (same_string (g_ptr_array_index ((GPtrArray *) support, i), id))
            return TRUE;
    }

    return FALSE;
}

static gboolean
support_is_unique_and_qualified (
    const AtmSraResult *result,
    const GPtrArray *support,
    guint expected_count
)
{
    if (support == NULL || support->len != expected_count)
        return FALSE;

    for (guint i = 0; i < support->len; i++) {
        const char *id = g_ptr_array_index ((GPtrArray *) support, i);
        if (!evidence_ids_contains (result, id))
            return FALSE;

        for (guint j = 0; j < i; j++) {
            if (same_string (id, g_ptr_array_index (
                    (GPtrArray *) support, j)))
                return FALSE;
        }
    }

    return TRUE;
}

static gboolean
parse_object (
    const char *json,
    JsonParser **out_parser,
    JsonObject **out_object,
    GError **error
)
{
    if (json == NULL || out_parser == NULL || *out_parser != NULL ||
        out_object == NULL || *out_object != NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR SRA fact has no valid qualifier payload."
        );

    JsonParser *parser = json_parser_new ();
    GError *parse_error = NULL;
    if (!json_parser_load_from_data (parser, json, -1, &parse_error)) {
        g_clear_error (&parse_error);
        g_object_unref (parser);
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR SRA qualifier payload is not valid JSON."
        );
    }

    JsonNode *root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root)) {
        g_object_unref (parser);
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR SRA qualifier payload is not an object."
        );
    }

    *out_object = json_node_get_object (root);
    *out_parser = parser;
    return TRUE;
}

static gboolean
json_string_is (
    JsonObject *object,
    const char *key,
    const char *expected
)
{
    if (object == NULL || key == NULL || expected == NULL ||
        !json_object_has_member (object, key))
        return FALSE;

    JsonNode *node = json_object_get_member (object, key);
    return node != NULL &&
        JSON_NODE_HOLDS_VALUE (node) &&
        json_node_get_value_type (node) == G_TYPE_STRING &&
        same_string (json_node_get_string (node), expected);
}

static gboolean
json_nullable_string_is (
    JsonObject *object,
    const char *key,
    const char *expected
)
{
    if (object == NULL || key == NULL ||
        !json_object_has_member (object, key))
        return FALSE;

    JsonNode *node = json_object_get_member (object, key);
    if (expected == NULL)
        return JSON_NODE_HOLDS_NULL (node);

    return node != NULL &&
        JSON_NODE_HOLDS_VALUE (node) &&
        json_node_get_value_type (node) == G_TYPE_STRING &&
        same_string (json_node_get_string (node), expected);
}

static gboolean
json_boolean_is (
    JsonObject *object,
    const char *key,
    gboolean expected
)
{
    if (object == NULL || key == NULL ||
        !json_object_has_member (object, key))
        return FALSE;

    JsonNode *node = json_object_get_member (object, key);
    return node != NULL &&
        JSON_NODE_HOLDS_VALUE (node) &&
        json_node_get_value_type (node) == G_TYPE_BOOLEAN &&
        json_node_get_boolean (node) == expected;
}

static gboolean
json_array_matches_sources (
    JsonObject *object,
    const char *key,
    const AtmBnrBlsPanel *panel,
    gboolean digests
)
{
    if (object == NULL || !json_object_has_member (object, key))
        return FALSE;

    JsonNode *node = json_object_get_member (object, key);
    if (node == NULL || !JSON_NODE_HOLDS_ARRAY (node))
        return FALSE;

    JsonArray *array = json_node_get_array (node);
    if (json_array_get_length (array) != ATM_BNR_BLS_SOURCE_COUNT)
        return FALSE;

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        const char *expected = digests
            ? atm_bnr_bls_panel_source_sha256 (panel, i)
            : atm_bnr_bls_panel_source_path (panel, i);
        const char *actual = json_array_get_string_element (array, i);
        if (!same_string (actual, expected))
            return FALSE;
    }

    return TRUE;
}

static gboolean
validate_missing_fact (
    const AtmBnrBlsPanel *panel,
    const AtmSraResult *result,
    const AtmSraEstablishedFact *fact,
    GError **error
)
{
    if (panel == NULL || result == NULL || fact == NULL ||
        !same_string (fact->fact_id, "rmd:bnr-bls-c01:2025-Q2") ||
        !same_string (fact->fact_type, "rmd.bnr_bls_c01_missing_observation") ||
        !same_string (fact->subject_id, BNR_SUBJECT) ||
        !same_string (fact->attribute, BNR_ATTRIBUTE) ||
        !same_string (fact->value, "MISSING") ||
        !same_string (fact->unit, BNR_UNIT) ||
        !same_string (fact->dimension, BNR_DIMENSION) ||
        !support_is_unique_and_qualified (result, fact->support,
                                          ATM_BNR_BLS_SOURCE_COUNT))
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "The authorized BNR 2025-Q2 MISSING fact has unsupported identity or support."
        );

    JsonParser *parser = NULL;
    JsonObject *object = NULL;
    if (!parse_object (fact->qualifiers_json, &parser, &object, error))
        return FALSE;

    gboolean valid =
        json_string_is (object, "schema",
                        "atm-bnr-bls-c01-missing-observation/1") &&
        json_string_is (object, "subject", BNR_SUBJECT) &&
        json_string_is (object, "attribute", BNR_ATTRIBUTE) &&
        json_string_is (object, "metric_label", "Net percentage") &&
        json_string_is (object, "calendar_quarter",
                        ATM_BNR_BLS_MISSING_QUARTER) &&
        json_string_is (object, "y_status", "MISSING") &&
        json_string_is (object, "missing_reason",
                        ATM_BNR_BLS_MISSING_REASON) &&
        json_string_is (object, "unit", BNR_UNIT) &&
        json_string_is (object, "dimension", BNR_DIMENSION) &&
        json_string_is (object, "scenario", BNR_SCENARIO) &&
        json_string_is (object, "epistemic_status", BNR_EPISTEMIC_STATUS) &&
        json_boolean_is (object, "break_before", FALSE) &&
        json_nullable_string_is (object, "break_reason", NULL) &&
        json_string_is (object, "absence_contract",
                        "authorized_absence_without_csv_row/1") &&
        json_array_matches_sources (object, "authority_source_paths",
                                    panel, FALSE) &&
        json_array_matches_sources (object, "authority_source_sha256",
                                    panel, TRUE) &&
        !json_object_has_member (object, "source_row") &&
        !json_object_has_member (object, "source_decimal") &&
        !json_object_has_member (object, "coefficient") &&
        !json_object_has_member (object, "exponent");

    g_object_unref (parser);
    return valid || reject (
        error,
        ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
        "The BNR 2025-Q2 MISSING qualifiers do not match the five-file absence authority."
    );
}

static gboolean
validate_numeric_fact (
    const AtmBnrBlsPanel *panel,
    const AtmSraResult *result,
    const AtmBnrBlsObservation *point,
    const AtmSraEstablishedFact *fact,
    GError **error
)
{
    if (panel == NULL || result == NULL || point == NULL || fact == NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR numeric point or its SRA fact is unavailable."
        );

    char *expected_id = g_strdup_printf ("rmd:bnr-bls-c01:%s", point->quarter);
    char *expected_value = g_strdup_printf (
        "%se%" G_GINT64_FORMAT,
        point->coefficient,
        point->exponent
    );
    gboolean fact_shape =
        same_string (fact->fact_id, expected_id) &&
        same_string (fact->fact_type, "rmd.bnr_bls_c01_observation") &&
        same_string (fact->subject_id, BNR_SUBJECT) &&
        same_string (fact->attribute, BNR_ATTRIBUTE) &&
        same_string (fact->value, expected_value) &&
        same_string (fact->unit, BNR_UNIT) &&
        same_string (fact->dimension, BNR_DIMENSION) &&
        support_is_unique_and_qualified (
            result,
            fact->support,
            ATM_BNR_BLS_SOURCE_COUNT + 1u
        );

    g_free (expected_id);
    g_free (expected_value);
    if (!fact_shape)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR numeric SRA fact does not match its source-bound point."
        );

    const AtmSraEstablishedFact *missing =
        find_fact (result, "rmd:bnr-bls-c01:2025-Q2");
    if (missing == NULL ||
        !support_is_unique_and_qualified (
            result, missing->support, ATM_BNR_BLS_SOURCE_COUNT)) {
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR source authority bindings are missing from the finalized SRA."
        );
    }

    guint authority_matches = 0;
    for (guint i = 0; i < missing->support->len; i++) {
        const char *authority_id = g_ptr_array_index (missing->support, i);
        if (support_contains (fact->support, authority_id))
            authority_matches++;
    }
    if (authority_matches != ATM_BNR_BLS_SOURCE_COUNT)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR numeric point does not carry all five source-authority bindings."
        );

    JsonParser *parser = NULL;
    JsonObject *object = NULL;
    if (!parse_object (fact->qualifiers_json, &parser, &object, error))
        return FALSE;

    char *row_text = g_strdup_printf ("%u", point->source_row);
    char *exponent_text = g_strdup_printf (
        "%" G_GINT64_FORMAT,
        point->exponent
    );
    gboolean valid =
        json_string_is (object, "schema",
                        "atm-bnr-bls-c01-observation/1") &&
        json_string_is (object, "subject", BNR_SUBJECT) &&
        json_string_is (object, "attribute", BNR_ATTRIBUTE) &&
        json_string_is (object, "metric_label", "Net percentage") &&
        json_string_is (object, "calendar_quarter", point->quarter) &&
        json_string_is (object, "y_status", "NUMERIC") &&
        json_string_is (object, "source_decimal", point->source_decimal) &&
        json_string_is (object, "coefficient", point->coefficient) &&
        json_string_is (object, "exponent", exponent_text) &&
        json_string_is (object, "numeric_profile",
                        "exact_decimal_coefficient_exponent/1") &&
        json_string_is (object, "unit", BNR_UNIT) &&
        json_string_is (object, "dimension", BNR_DIMENSION) &&
        json_string_is (object, "scenario", BNR_SCENARIO) &&
        json_string_is (object, "epistemic_status", BNR_EPISTEMIC_STATUS) &&
        json_string_is (object, "source_row", row_text) &&
        json_string_is (object, "source_id", point->source_id) &&
        json_string_is (object, "source_kind", point->source_kind) &&
        json_nullable_string_is (object, "reference_date",
                                 point->reference_date) &&
        json_nullable_string_is (object, "missing_reason", NULL) &&
        json_boolean_is (object, "break_before", FALSE) &&
        json_nullable_string_is (object, "break_reason", NULL);

    g_free (row_text);
    g_free (exponent_text);
    g_object_unref (parser);
    return valid || reject (
        error,
        ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
        "BNR numeric point qualifiers do not reproduce the exact source row."
    );
}

static gboolean
result_ids_equal (const AtmSraResult *a, const AtmSraResult *b)
{
    return a != NULL && b != NULL &&
        a->finalized && b->finalized &&
        a->qualification != NULL && b->qualification != NULL &&
        same_string (
            a->qualification->scientific_content_id,
            b->qualification->scientific_content_id
        ) &&
        same_string (
            a->qualification->qualified_artifact_id,
            b->qualification->qualified_artifact_id
        );
}

/* Rebuild the source-bound SRA from this panel and prove that it is the same
 * finalized result supplied by the caller. Optionally transfer the rebuilt
 * SRA to the caller. */
static gboolean
build_matching_sra (
    const AtmBnrBlsPanel *panel,
    const AtmBnrBlsSra *supplied_sra,
    AtmBnrBlsSra **out_rebuilt_sra,
    GError **error
)
{
    if (panel == NULL || supplied_sra == NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
            "BNR SeriesContract requires both the admitted panel and finalized SRA."
        );

    if (!atm_bnr_bls_panel_validate (panel, error) ||
        !atm_bnr_bls_sra_validate (supplied_sra, error))
        return FALSE;

    AtmBnrBlsSra *rebuilt = NULL;
    if (!atm_bnr_bls_sra_new (panel, &rebuilt, error))
        return FALSE;

    const AtmSraResult *actual_result =
        atm_bnr_bls_sra_result (supplied_sra);
    const AtmSraResult *rebuilt_result =
        atm_bnr_bls_sra_result (rebuilt);

    if (!result_ids_equal (actual_result, rebuilt_result)) {
        atm_bnr_bls_sra_free (rebuilt);
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_IDENTITY,
            "The supplied BNR SRA is not the finalized result reconstructed from this exact panel."
        );
    }

    if (out_rebuilt_sra != NULL) {
        if (*out_rebuilt_sra != NULL) {
            atm_bnr_bls_sra_free (rebuilt);
            return reject (
                error,
                ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
                "BNR rebuilt-SRA output must be NULL."
            );
        }
        *out_rebuilt_sra = rebuilt;
    } else {
        atm_bnr_bls_sra_free (rebuilt);
    }

    return TRUE;
}

static gboolean
build_contract (
    const AtmBnrBlsPanel *panel,
    const AtmBnrBlsSra *sra,
    AtmSeriesContract **out_contract,
    GError **error
)
{
    if (panel == NULL || sra == NULL ||
        out_contract == NULL || *out_contract != NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
            "Invalid BNR SeriesContract construction arguments."
        );

    const AtmSraResult *result = atm_bnr_bls_sra_result (sra);
    if (result == NULL || !result->finalized ||
        result->established_facts == NULL ||
        result->established_facts->len != 12u ||
        result->derived_facts == NULL || result->derived_facts->len != 0u ||
        result->qualification == NULL ||
        result->qualification->evidence_atom_ids == NULL ||
        result->qualification->evidence_atom_ids->len !=
            ATM_BNR_BLS_SOURCE_COUNT + 11u)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SRA,
            "BNR SeriesContract requires the finalized 11-observation/one-MISSING SRA."
        );

    const AtmSraEstablishedFact *missing =
        find_fact (result, "rmd:bnr-bls-c01:2025-Q2");
    if (!validate_missing_fact (panel, result, missing, error))
        return FALSE;

    const char *snapshot = atm_bnr_bls_panel_snapshot (panel);
    const char *csv_path = atm_bnr_bls_panel_source_path (
        panel,
        ATM_BNR_BLS_SOURCE_PANEL
    );
    char *repository_version = snapshot != NULL
        ? g_strconcat ("snapshot:", snapshot, NULL)
        : NULL;
    if (snapshot == NULL || csv_path == NULL || repository_version == NULL) {
        g_free (repository_version);
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SOURCE,
            "The admitted BNR source tuple is incomplete."
        );
    }

    AtmSeriesContract *contract = atm_series_contract_new (
        ATM_SERIES_X_CALENDAR_QUARTER,
        BNR_SUBJECT,
        BNR_ATTRIBUTE,
        "calendar_quarter",
        "time",
        BNR_UNIT,
        BNR_DIMENSION,
        BNR_SERIES_SEMANTICS,
        BNR_SCENARIO,
        BNR_TIME_SCOPE,
        BNR_EPISTEMIC_STATUS,
        "rmd",
        repository_version,
        snapshot,
        csv_path,
        ATM_BNR_BLS_SERIES_PROFILE,
        "1",
        error
    );
    g_free (repository_version);
    if (contract == NULL)
        return FALSE;

    guint observation_index = 0;
    for (guint i = 0; i < G_N_ELEMENTS (quarters); i++) {
        const char *quarter = quarters[i];
        if (same_string (quarter, ATM_BNR_BLS_MISSING_QUARTER)) {
            const char *support[ATM_BNR_BLS_SOURCE_COUNT];
            for (guint j = 0; j < ATM_BNR_BLS_SOURCE_COUNT; j++)
                support[j] = g_ptr_array_index (missing->support, j);

            if (!atm_series_contract_add_point (
                    contract,
                    quarter,
                    ATM_SERIES_Y_MISSING,
                    NULL,
                    NULL,
                    0,
                    FALSE,
                    FALSE,
                    ATM_BNR_BLS_MISSING_REASON,
                    NULL,
                    support,
                    G_N_ELEMENTS (support),
                    error)) {
                atm_series_contract_free (contract);
                return FALSE;
            }
            continue;
        }

        AtmBnrBlsObservation observation = {0};
        if (!atm_bnr_bls_panel_observation (
                panel, observation_index, &observation) ||
            !same_string (observation.quarter, quarter)) {
            atm_series_contract_free (contract);
            return reject (
                error,
                ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SOURCE,
                "The pinned BNR panel does not map to the canonical quarter sequence."
            );
        }

        char *fact_id = g_strdup_printf (
            "rmd:bnr-bls-c01:%s",
            observation.quarter
        );
        const AtmSraEstablishedFact *fact = find_fact (result, fact_id);
        g_free (fact_id);

        if (!validate_numeric_fact (panel, result, &observation, fact, error)) {
            atm_series_contract_free (contract);
            return FALSE;
        }

        guint support_count = fact->support->len;
        const char **support = g_new0 (const char *, support_count);
        for (guint j = 0; j < support_count; j++)
            support[j] = g_ptr_array_index (fact->support, j);

        gboolean added = atm_series_contract_add_point (
            contract,
            observation.quarter,
            ATM_SERIES_Y_NUMERIC,
            observation.source_decimal,
            observation.coefficient,
            observation.exponent,
            observation.source_decimal[0] == '-' &&
                same_string (observation.coefficient, "0") &&
                observation.exponent == 0,
            FALSE,
            NULL,
            NULL,
            support,
            support_count,
            error
        );
        g_free (support);
        if (!added) {
            atm_series_contract_free (contract);
            return FALSE;
        }

        observation_index++;
    }

    if (observation_index !=
        atm_bnr_bls_panel_observation_count (panel)) {
        atm_series_contract_free (contract);
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_SOURCE,
            "The BNR SeriesContract did not consume each observed source row exactly once."
        );
    }

    if (!atm_series_contract_validate (contract, result, error)) {
        atm_series_contract_free (contract);
        return FALSE;
    }

    *out_contract = contract;
    return TRUE;
}

static gboolean
nullable_strings_equal (const char *a, const char *b)
{
    return a == NULL ? b == NULL : b != NULL && strcmp (a, b) == 0;
}

static gboolean
contract_equal (
    const AtmSeriesContract *a,
    const AtmSeriesContract *b
)
{
    if (a == NULL || b == NULL ||
        a->x_kind != b->x_kind ||
        !nullable_strings_equal (a->subject, b->subject) ||
        !nullable_strings_equal (a->attribute, b->attribute) ||
        !nullable_strings_equal (a->x_unit, b->x_unit) ||
        !nullable_strings_equal (a->x_dimension, b->x_dimension) ||
        !nullable_strings_equal (a->y_unit, b->y_unit) ||
        !nullable_strings_equal (a->y_dimension, b->y_dimension) ||
        !nullable_strings_equal (a->series_semantics, b->series_semantics) ||
        !nullable_strings_equal (a->scenario, b->scenario) ||
        !nullable_strings_equal (a->time_scope, b->time_scope) ||
        !nullable_strings_equal (a->epistemic_status, b->epistemic_status) ||
        !nullable_strings_equal (a->repository_id, b->repository_id) ||
        !nullable_strings_equal (a->repository_version, b->repository_version) ||
        !nullable_strings_equal (a->snapshot_sha, b->snapshot_sha) ||
        !nullable_strings_equal (a->source_path, b->source_path) ||
        !nullable_strings_equal (a->profile_id, b->profile_id) ||
        !nullable_strings_equal (a->profile_version, b->profile_version) ||
        !nullable_strings_equal (a->scientific_content_id, b->scientific_content_id) ||
        !nullable_strings_equal (a->qualified_series_id, b->qualified_series_id) ||
        a->points == NULL || b->points == NULL ||
        a->points->len != b->points->len)
        return FALSE;

    for (guint i = 0; i < a->points->len; i++) {
        const AtmSeriesContractPoint *ap =
            g_ptr_array_index (a->points, i);
        const AtmSeriesContractPoint *bp =
            g_ptr_array_index (b->points, i);
        if (ap == NULL || bp == NULL ||
            ap->order != bp->order ||
            !nullable_strings_equal (ap->x, bp->x) ||
            ap->y_status != bp->y_status ||
            !nullable_strings_equal (ap->source_decimal, bp->source_decimal) ||
            !nullable_strings_equal (ap->coefficient, bp->coefficient) ||
            ap->exponent != bp->exponent ||
            ap->negative_zero != bp->negative_zero ||
            ap->break_before != bp->break_before ||
            !nullable_strings_equal (ap->missing_reason, bp->missing_reason) ||
            !nullable_strings_equal (ap->break_reason, bp->break_reason) ||
            ap->support == NULL || bp->support == NULL ||
            ap->support->len != bp->support->len)
            return FALSE;

        for (guint j = 0; j < ap->support->len; j++) {
            if (!nullable_strings_equal (
                    g_ptr_array_index (ap->support, j),
                    g_ptr_array_index (bp->support, j)))
                return FALSE;
        }
    }

    return TRUE;
}

void
atm_bnr_bls_series_contract_free (AtmBnrBlsSeriesContract *series)
{
    if (series == NULL)
        return;
    g_clear_pointer (&series->panel, atm_bnr_bls_panel_free);
    g_clear_pointer (&series->sra, atm_bnr_bls_sra_free);
    g_clear_pointer (&series->contract, atm_series_contract_free);
    g_free (series);
}

gboolean
atm_bnr_bls_series_contract_from_sra (
    const AtmBnrBlsPanel *panel,
    const AtmBnrBlsSra *sra,
    AtmBnrBlsSeriesContract **out,
    GError **error
)
{
    if (panel == NULL || sra == NULL || out == NULL || *out != NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
            "Invalid BNR SeriesContract adapter arguments."
        );

    AtmBnrBlsSeriesContract *series =
        g_new0 (AtmBnrBlsSeriesContract, 1);

    if (!atm_bnr_bls_panel_rebuild (panel, &series->panel, error))
        goto invalid;

    if (!build_matching_sra (
            series->panel,
            sra,
            &series->sra,
            error))
        goto invalid;

    if (!build_contract (
            series->panel,
            series->sra,
            &series->contract,
            error))
        goto invalid;

    *out = series;
    return TRUE;

invalid:
    atm_bnr_bls_series_contract_free (series);
    return FALSE;
}

gboolean
atm_bnr_bls_series_contract_validate (
    const AtmBnrBlsSeriesContract *series,
    GError **error
)
{
    if (series == NULL || series->panel == NULL ||
        series->sra == NULL || series->contract == NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
            "BNR SeriesContract state is incomplete."
        );

    AtmBnrBlsSra *rebuilt_sra = NULL;
    if (!build_matching_sra (
            series->panel,
            series->sra,
            &rebuilt_sra,
            error))
        return FALSE;

    AtmSeriesContract *expected_contract = NULL;
    if (!build_contract (
            series->panel,
            rebuilt_sra,
            &expected_contract,
            error)) {
        atm_bnr_bls_sra_free (rebuilt_sra);
        return FALSE;
    }

    gboolean matches = contract_equal (
        series->contract,
        expected_contract
    );
    atm_series_contract_free (expected_contract);
    atm_bnr_bls_sra_free (rebuilt_sra);

    return matches || reject (
        error,
        ATM_BNR_BLS_SERIES_CONTRACT_ERROR_IDENTITY,
        "BNR SeriesContract differs from an independent reconstruction of the pinned five-file panel and finalized SRA."
    );
}

gboolean
atm_bnr_bls_series_contract_rebuild (
    const AtmBnrBlsSeriesContract *series,
    AtmBnrBlsSeriesContract **out,
    GError **error
)
{
    if (out == NULL || *out != NULL)
        return reject (
            error,
            ATM_BNR_BLS_SERIES_CONTRACT_ERROR_ARGUMENT,
            "BNR SeriesContract rebuild output must be NULL."
        );

    if (!atm_bnr_bls_series_contract_validate (series, error))
        return FALSE;

    return atm_bnr_bls_series_contract_from_sra (
        series->panel,
        series->sra,
        out,
        error
    );
}

const AtmSeriesContract *
atm_bnr_bls_series_contract_value (
    const AtmBnrBlsSeriesContract *series
)
{
    return series != NULL ? series->contract : NULL;
}

guint
atm_bnr_bls_series_contract_count (
    const AtmBnrBlsSeriesContract *series
)
{
    return series != NULL && series->contract != NULL &&
        series->contract->points != NULL
            ? series->contract->points->len
            : 0;
}

const AtmSeriesContractPoint *
atm_bnr_bls_series_contract_point (
    const AtmBnrBlsSeriesContract *series,
    guint index
)
{
    return series != NULL && series->contract != NULL &&
        series->contract->points != NULL &&
        index < series->contract->points->len
            ? g_ptr_array_index (series->contract->points, index)
            : NULL;
}

const char *
atm_bnr_bls_series_contract_scientific_id (
    const AtmBnrBlsSeriesContract *series
)
{
    return series != NULL && series->contract != NULL
        ? atm_series_contract_scientific_id (series->contract)
        : NULL;
}

const char *
atm_bnr_bls_series_contract_qualified_id (
    const AtmBnrBlsSeriesContract *series
)
{
    return series != NULL && series->contract != NULL
        ? atm_series_contract_qualified_id (series->contract)
        : NULL;
}
