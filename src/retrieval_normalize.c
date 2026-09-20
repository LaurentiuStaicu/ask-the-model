#include "retrieval_normalize.h"

#include <string.h>

GQuark
atm_retrieval_normalize_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-normalize-error-quark"
    );
}

void
atm_normalized_query_free (AtmNormalizedQuery *query)
{
    if (query == NULL) {
        return;
    }

    g_free (query->expanded_text);
    g_free (query);
}

static gboolean
token_is_any (
    const char *token,
    const char *const *values,
    gsize value_count
)
{
    for (gsize i = 0; i < value_count; i++) {
        if (g_strcmp0 (token, values[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
append_alias_once (
    GString *expanded,
    GHashTable *aliases,
    const char *alias
)
{
    if (g_hash_table_contains (aliases, alias)) {
        return;
    }

    g_hash_table_add (
        aliases,
        g_strdup (alias)
    );

    if (expanded->len > 0 &&
        !g_ascii_isspace (expanded->str[expanded->len - 1])) {
        g_string_append_c (expanded, ' ');
    }

    g_string_append (expanded, alias);
}

static gboolean
token_looks_numeric_scope (const char *token)
{
    gsize length = strlen (token);

    if (length == 0 || length > 12) {
        return FALSE;
    }

    for (gsize i = 0; i < length; i++) {
        if (!g_ascii_isdigit (token[i])) {
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
atm_retrieval_normalize_query (
    const char *query,
    AtmNormalizedQuery **out_query,
    GError **error
)
{
    static const char *status_ro[] = {
        "stare", "starea", "statut"
    };
    static const char *current_ro[] = {
        "curent", "curenta", "curentă",
        "actual", "actuala", "actuală"
    };
    static const char *boundary_ro[] = {
        "limita", "limită", "limite", "granita", "granița"
    };
    static const char *structure_ro[] = {
        "structura", "structură", "structuri"
    };
    static const char *mechanism_ro[] = {
        "mecanism", "mecanismul", "mecanisme"
    };
    static const char *loop_ro[] = {
        "bucla", "buclă", "bucle"
    };
    static const char *source_ro[] = {
        "sursa", "sursă", "surse", "sursele"
    };
    static const char *evidence_ro[] = {
        "evidenta", "evidență", "dovada", "dovadă", "dovezi"
    };
    static const char *provenance_ro[] = {
        "provenienta", "proveniență"
    };
    static const char *value_ro[] = {
        "valoare", "valori"
    };
    static const char *year_ro[] = {
        "an", "anul", "ani"
    };
    static const char *period_ro[] = {
        "perioada", "perioadă", "perioade"
    };
    static const char *implementation_ro[] = {
        "implementare", "implementarea", "cod"
    };
    static const char *validation_ro[] = {
        "validare", "validarea", "validari", "validări",
        "validarii", "validării",
        "validat", "validata", "validată", "validate"
    };
    static const char *paradigm_ro[] = {
        "paradigma", "paradigmă", "paradigme", "paradigmele"
    };
    static const char *modeling_ro[] = {
        "modelare", "modelarea", "modelarii", "modelării"
    };
    static const char *canonical_ro[] = {
        "canonic", "canonica", "canonică", "canonice", "canonicele"
    };
    static const char *compare_ro[] = {
        "compara", "compară", "comparați", "comparati",
        "comparare", "comparatie", "comparație", "comparația"
    };
    static const char *forecast_ro[] = {
        "prognoza", "prognoză", "prognoze", "prognozele"
    };
    static const char *probability_ro[] = {
        "probabilitate", "probabilitatea", "probabilitati",
        "probabilități", "probabilistic", "probabilistica",
        "probabilistică", "probabilistice"
    };
    static const char *participant_ro[] = {
        "participant", "participanti", "participanți",
        "participantii", "participanții"
    };
    static const char *human_ro[] = {
        "uman", "umana", "umană", "umani", "umane"
    };
    static const char *established_ro[] = {
        "stabilit", "stabilita", "stabilită", "stabilite"
    };
    static const char *numeric_terms[] = {
        "mape", "percent", "percentage",
        "rate", "ratio", "coefficient"
    };
    gchar **tokens = NULL;
    GHashTable *aliases = NULL;
    GString *expanded = NULL;
    AtmNormalizedQuery *normalized = NULL;
    gboolean saw_validation = FALSE;
    gboolean saw_human_or_participant = FALSE;
    gsize query_length;

    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (out_query != NULL, FALSE);
    g_return_val_if_fail (*out_query == NULL, FALSE);

    query_length = strlen (query);

    if (query_length == 0 ||
        query_length > ATM_RETRIEVAL_NORMALIZE_MAX_QUERY_BYTES ||
        !g_utf8_validate (query, query_length, NULL)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_NORMALIZE_ERROR,
            ATM_RETRIEVAL_NORMALIZE_ERROR_ARGUMENT,
            "Retrieval query normalization input is invalid."
        );
        return FALSE;
    }

    tokens = g_str_tokenize_and_fold (
        query,
        NULL,
        NULL
    );
    aliases = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );
    expanded = g_string_new (query);
    normalized = g_new0 (AtmNormalizedQuery, 1);

    for (guint i = 0; tokens[i] != NULL; i++) {
        const char *token = tokens[i];

        if (token_is_any (
                token,
                status_ro,
                G_N_ELEMENTS (status_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "status"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                current_ro,
                G_N_ELEMENTS (current_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "current"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (g_strcmp0 (token, "status") == 0 ||
            g_strcmp0 (token, "current") == 0 ||
            g_strcmp0 (token, "currently") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                boundary_ro,
                G_N_ELEMENTS (boundary_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "boundary"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (g_strcmp0 (token, "boundary") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                structure_ro,
                G_N_ELEMENTS (structure_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "structure"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE;
        }

        if (token_is_any (
                token,
                mechanism_ro,
                G_N_ELEMENTS (mechanism_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "mechanism"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE;
        }

        if (token_is_any (
                token,
                loop_ro,
                G_N_ELEMENTS (loop_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "feedback"
            );
            append_alias_once (
                expanded,
                aliases,
                "loop"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE;
        }

        if (g_strcmp0 (token, "structure") == 0 ||
            g_strcmp0 (token, "mechanism") == 0 ||
            g_strcmp0 (token, "feedback") == 0 ||
            g_strcmp0 (token, "loop") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE;
        }

        if (token_is_any (
                token,
                validation_ro,
                G_N_ELEMENTS (validation_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "validation"
            );
            saw_validation = TRUE;
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (g_strcmp0 (token, "validation") == 0 ||
            g_strcmp0 (token, "validated") == 0 ||
            g_strcmp0 (token, "limitation") == 0 ||
            g_strcmp0 (token, "limitations") == 0) {
            if (g_strcmp0 (token, "validation") == 0 ||
                g_strcmp0 (token, "validated") == 0) {
                saw_validation = TRUE;
            }

            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                paradigm_ro,
                G_N_ELEMENTS (paradigm_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "paradigm"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE |
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                modeling_ro,
                G_N_ELEMENTS (modeling_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "modeling"
            );
            append_alias_once (
                expanded,
                aliases,
                "model"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE |
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                canonical_ro,
                G_N_ELEMENTS (canonical_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "canonical"
            );
        }

        if (token_is_any (
                token,
                compare_ro,
                G_N_ELEMENTS (compare_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "compare"
            );
        }

        if (g_strcmp0 (token, "paradigm") == 0 ||
            g_strcmp0 (token, "paradigms") == 0 ||
            g_strcmp0 (token, "modeling") == 0 ||
            g_strcmp0 (token, "modelling") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_STRUCTURE |
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                forecast_ro,
                G_N_ELEMENTS (forecast_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "forecast"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                probability_ro,
                G_N_ELEMENTS (probability_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "probability"
            );
            append_alias_once (
                expanded,
                aliases,
                "probabilistic"
            );
        }

        if (g_strcmp0 (token, "claim") == 0 ||
            g_strcmp0 (token, "claims") == 0 ||
            g_strcmp0 (token, "forecast") == 0 ||
            g_strcmp0 (token, "forecasts") == 0 ||
            g_strcmp0 (token, "probabilistic") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_CURRENT_STATE;
        }

        if (token_is_any (
                token,
                participant_ro,
                G_N_ELEMENTS (participant_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "participant"
            );
            saw_human_or_participant = TRUE;
        }

        if (token_is_any (
                token,
                human_ro,
                G_N_ELEMENTS (human_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "human"
            );
            saw_human_or_participant = TRUE;
        }

        if (token_is_any (
                token,
                established_ro,
                G_N_ELEMENTS (established_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "established"
            );
        }

        if (token_is_any (
                token,
                source_ro,
                G_N_ELEMENTS (source_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "source"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_EVIDENCE;
        }

        if (token_is_any (
                token,
                evidence_ro,
                G_N_ELEMENTS (evidence_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "evidence"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_EVIDENCE;
        }

        if (token_is_any (
                token,
                provenance_ro,
                G_N_ELEMENTS (provenance_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "provenance"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_EVIDENCE;
        }

        if (g_strcmp0 (token, "source") == 0 ||
            g_strcmp0 (token, "sources") == 0 ||
            g_strcmp0 (token, "evidence") == 0 ||
            g_strcmp0 (token, "provenance") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_EVIDENCE;
        }

        if (token_is_any (
                token,
                value_ro,
                G_N_ELEMENTS (value_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "value"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_NUMERIC;
        }

        if (token_is_any (
                token,
                year_ro,
                G_N_ELEMENTS (year_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "year"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_NUMERIC;
        }

        if (token_is_any (
                token,
                period_ro,
                G_N_ELEMENTS (period_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "period"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_NUMERIC;
        }

        if (g_strcmp0 (token, "value") == 0 ||
            g_strcmp0 (token, "year") == 0 ||
            g_strcmp0 (token, "period") == 0 ||
            token_is_any (
                token,
                numeric_terms,
                G_N_ELEMENTS (numeric_terms)
            ) ||
            token_looks_numeric_scope (token)) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_NUMERIC;
        }

        if (token_is_any (
                token,
                implementation_ro,
                G_N_ELEMENTS (implementation_ro)
            )) {
            append_alias_once (
                expanded,
                aliases,
                "implementation"
            );
            append_alias_once (
                expanded,
                aliases,
                "code"
            );
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_IMPLEMENTATION;
        }

        if (g_strcmp0 (token, "implementation") == 0 ||
            g_strcmp0 (token, "code") == 0 ||
            g_strcmp0 (token, "script") == 0) {
            normalized->intents |=
                ATM_RETRIEVAL_INTENT_IMPLEMENTATION;
        }
    }

    if (saw_validation && saw_human_or_participant) {
        normalized->intents |=
            ATM_RETRIEVAL_INTENT_EVIDENCE;
    }

    normalized->expanded_text = g_string_free (
        expanded,
        FALSE
    );
    expanded = NULL;

    g_hash_table_unref (aliases);
    g_strfreev (tokens);

    *out_query = g_steal_pointer (&normalized);
    return TRUE;
}
