#include "retrieval_conversation.h"

#include <string.h>

struct AtmRetrievalConversationState {
    GPtrArray *frozen_repositories;
    GPtrArray *last_repository_ids;
    char *last_exact_logical_source_id;
    char *last_exact_repository_id;
    guint last_intents;
    gboolean has_previous_turn;
};

GQuark
atm_retrieval_conversation_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-conversation-error-quark"
    );
}

void
atm_retrieval_conversation_turn_free (
    AtmRetrievalConversationTurn *turn
)
{
    if (turn == NULL) {
        return;
    }

    g_clear_pointer (
        &turn->retrieval,
        atm_retrieval_result_set_free
    );
    g_free (turn->effective_query);
    g_free (turn);
}

void
atm_retrieval_conversation_state_free (
    AtmRetrievalConversationState *state
)
{
    if (state == NULL) {
        return;
    }

    g_clear_pointer (
        &state->frozen_repositories,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &state->last_repository_ids,
        g_ptr_array_unref
    );
    g_free (state->last_exact_logical_source_id);
    g_free (state->last_exact_repository_id);
    g_free (state);
}

AtmRetrievalConversationState *
atm_retrieval_conversation_state_new (
    const GPtrArray *frozen_repositories,
    GError **error
)
{
    AtmRetrievalScopeSelection *validation = NULL;

    g_return_val_if_fail (frozen_repositories != NULL, NULL);

    if (!atm_retrieval_scope_select (
            "",
            frozen_repositories,
            &validation,
            error
        )) {
        return NULL;
    }

    AtmRetrievalConversationState *state = g_new0 (
        AtmRetrievalConversationState,
        1
    );

    state->frozen_repositories = g_steal_pointer (
        &validation->repositories
    );
    validation->repositories = NULL;
    state->last_repository_ids =
        g_ptr_array_new_with_free_func (g_free);

    atm_retrieval_scope_selection_free (validation);
    return state;
}

static gboolean
token_is_repository_name_part (const char *token)
{
    static const char *values[] = {
        "ewd",
        "cbd",
        "rmd",
        "empirical",
        "world3",
        "cognitive",
        "belief",
        "romanian",
        "monetary",
        "dynamics"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (values); i++) {
        if (g_strcmp0 (token, values[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
token_is_followup_marker (const char *token)
{
    static const char *values[] = {
        "si",
        "și",
        "dar",
        "iar",
        "apoi",
        "acolo",
        "aceeasi",
        "aceeași",
        "and",
        "but",
        "also",
        "then",
        "there",
        "same",
        "about"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (values); i++) {
        if (g_strcmp0 (token, values[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
token_is_comparison_marker (const char *token)
{
    static const char *values[] = {
        "compara",
        "compară",
        "comparatie",
        "comparație",
        "compare",
        "comparison",
        "versus",
        "vs"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (values); i++) {
        if (g_strcmp0 (token, values[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
token_is_source_followup (const char *token)
{
    static const char *values[] = {
        "sursa",
        "sursă",
        "surse",
        "sursele",
        "source",
        "sources",
        "evidence",
        "provenance",
        "dovada",
        "dovadă",
        "dovezi"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (values); i++) {
        if (g_strcmp0 (token, values[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
token_is_year_or_quarter (const char *token)
{
    gsize length;

    if (token == NULL) {
        return FALSE;
    }

    length = strlen (token);

    if (length == 4) {
        for (guint i = 0; i < 4; i++) {
            if (!g_ascii_isdigit (token[i])) {
                return FALSE;
            }
        }

        return TRUE;
    }

    if (length == 7 &&
        g_ascii_isdigit (token[0]) &&
        g_ascii_isdigit (token[1]) &&
        g_ascii_isdigit (token[2]) &&
        g_ascii_isdigit (token[3]) &&
        token[4] == '-' &&
        (token[5] == 'q' || token[5] == 'Q') &&
        token[6] >= '1' &&
        token[6] <= '4') {
        return TRUE;
    }

    return FALSE;
}

static gboolean
query_is_comparison_followup (gchar **tokens)
{
    for (guint i = 0; tokens[i] != NULL; i++) {
        if (token_is_comparison_marker (tokens[i])) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
query_is_contextual_followup (
    gchar **tokens,
    gboolean explicit_scope
)
{
    guint meaningful = 0;
    gboolean has_marker = FALSE;
    gboolean has_context_reference = FALSE;

    for (guint i = 0; tokens[i] != NULL; i++) {
        const char *token = tokens[i];

        if (token_is_repository_name_part (token)) {
            continue;
        }

        meaningful++;

        if (token_is_followup_marker (token) ||
            token_is_comparison_marker (token)) {
            has_marker = TRUE;
        }

        if (token_is_source_followup (token) ||
            token_is_year_or_quarter (token)) {
            has_context_reference = TRUE;
        }
    }

    if (meaningful == 0) {
        return explicit_scope;
    }

    if (has_marker && meaningful <= 6) {
        return TRUE;
    }

    if (has_context_reference && meaningful <= 4) {
        return TRUE;
    }

    return FALSE;
}

static gboolean
query_has_substantive_noncontext_terms (
    gchar **tokens
)
{
    for (guint i = 0; tokens[i] != NULL; i++) {
        const char *token = tokens[i];

        if (token_is_repository_name_part (token) ||
            token_is_followup_marker (token) ||
            token_is_comparison_marker (token) ||
            token_is_source_followup (token) ||
            token_is_year_or_quarter (token) ||
            g_strcmp0 (token, "in") == 0 ||
            g_strcmp0 (token, "în") == 0 ||
            g_strcmp0 (token, "cu") == 0 ||
            g_strcmp0 (token, "with") == 0 ||
            g_strcmp0 (token, "the") == 0 ||
            g_strcmp0 (token, "din") == 0 ||
            g_strcmp0 (token, "from") == 0) {
            continue;
        }

        return TRUE;
    }

    return FALSE;
}

static gboolean
ids_contain (
    const GPtrArray *ids,
    const char *repository_id
)
{
    for (guint i = 0; i < ids->len; i++) {
        if (g_strcmp0 (
                g_ptr_array_index ((GPtrArray *) ids, i),
                repository_id
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
add_scope_unique (
    GPtrArray *scopes,
    const AtmRetrievalRepositoryScope *scope
)
{
    for (guint i = 0; i < scopes->len; i++) {
        const AtmRetrievalRepositoryScope *existing =
            g_ptr_array_index (scopes, i);

        if (g_strcmp0 (
                existing->repository_id,
                scope->repository_id
            ) == 0) {
            return;
        }
    }

    g_ptr_array_add (
        scopes,
        atm_retrieval_repository_scope_new (
            scope->repository_id,
            scope->index_path
        )
    );
}

static GPtrArray *
scopes_from_last_ids (
    const AtmRetrievalConversationState *state
)
{
    GPtrArray *scopes = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );

    for (guint i = 0;
         i < state->frozen_repositories->len;
         i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index (
                state->frozen_repositories,
                i
            );

        if (ids_contain (
                state->last_repository_ids,
                scope->repository_id
            )) {
            add_scope_unique (scopes, scope);
        }
    }

    return scopes;
}

static GPtrArray *
clone_scopes (const GPtrArray *input)
{
    GPtrArray *scopes = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );

    for (guint i = 0; i < input->len; i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index ((GPtrArray *) input, i);

        add_scope_unique (scopes, scope);
    }

    return scopes;
}

static GPtrArray *
union_previous_and_explicit (
    const AtmRetrievalConversationState *state,
    const GPtrArray *explicit_scopes
)
{
    GPtrArray *scopes = scopes_from_last_ids (state);

    for (guint i = 0; i < explicit_scopes->len; i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index (
                (GPtrArray *) explicit_scopes,
                i
            );

        add_scope_unique (scopes, scope);
    }

    return scopes;
}

static gboolean
should_inherit_previous_intent (guint current_intents)
{
    guint substantive_intents =
        ATM_RETRIEVAL_INTENT_CURRENT_STATE |
        ATM_RETRIEVAL_INTENT_STRUCTURE |
        ATM_RETRIEVAL_INTENT_IMPLEMENTATION;

    return (current_intents & substantive_intents) == 0;
}

static void
append_intent_keywords (
    GString *query,
    guint intents
)
{
    if ((intents & ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0) {
        g_string_append (query, " current status");
    }

    if ((intents & ATM_RETRIEVAL_INTENT_STRUCTURE) != 0) {
        g_string_append (query, " structure mechanism");
    }

    if ((intents & ATM_RETRIEVAL_INTENT_EVIDENCE) != 0) {
        g_string_append (query, " source evidence");
    }

    if ((intents & ATM_RETRIEVAL_INTENT_NUMERIC) != 0) {
        g_string_append (query, " value period");
    }

    if ((intents & ATM_RETRIEVAL_INTENT_IMPLEMENTATION) != 0) {
        g_string_append (query, " implementation code");
    }
}

static void
append_scope_ids (
    GString *query,
    const GPtrArray *scopes
)
{
    for (guint i = 0; i < scopes->len; i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index ((GPtrArray *) scopes, i);

        g_string_append_c (query, ' ');
        g_string_append (query, scope->repository_id);
    }
}

static gboolean
single_scope_matches_anchor (
    const GPtrArray *scopes,
    const AtmRetrievalConversationState *state
)
{
    if (scopes->len != 1 ||
        state->last_exact_logical_source_id == NULL ||
        state->last_exact_repository_id == NULL) {
        return FALSE;
    }

    const AtmRetrievalRepositoryScope *scope =
        g_ptr_array_index ((GPtrArray *) scopes, 0);

    return g_strcmp0 (
        scope->repository_id,
        state->last_exact_repository_id
    ) == 0;
}

static void
clear_last_state (
    AtmRetrievalConversationState *state
)
{
    g_ptr_array_set_size (
        state->last_repository_ids,
        0
    );
    g_clear_pointer (
        &state->last_exact_logical_source_id,
        g_free
    );
    g_clear_pointer (
        &state->last_exact_repository_id,
        g_free
    );
    state->last_intents = 0;
}

static void
update_last_state (
    AtmRetrievalConversationState *state,
    const AtmRetrievalResultSet *results
)
{
    const char *unique_exact_id = NULL;
    const char *unique_exact_repository = NULL;
    gboolean exact_is_ambiguous = FALSE;

    clear_last_state (state);

    for (guint repository_index = 0;
         repository_index < results->repositories->len;
         repository_index++) {
        const AtmRepositoryEvidenceSet *set =
            g_ptr_array_index (
                results->repositories,
                repository_index
            );

        g_ptr_array_add (
            state->last_repository_ids,
            g_strdup (set->repository_id)
        );

        for (guint evidence_index = 0;
             evidence_index < set->evidence->len;
             evidence_index++) {
            const AtmEvidenceRecord *record =
                g_ptr_array_index (
                    set->evidence,
                    evidence_index
                );

            if (record->match_kind !=
                    ATM_EVIDENCE_MATCH_EXACT ||
                record->logical_source_id == NULL) {
                continue;
            }

            if (unique_exact_id == NULL) {
                unique_exact_id =
                    record->logical_source_id;
                unique_exact_repository =
                    set->repository_id;
                continue;
            }

            if (g_strcmp0 (
                    unique_exact_id,
                    record->logical_source_id
                ) != 0) {
                exact_is_ambiguous = TRUE;
            }
        }
    }

    if (!exact_is_ambiguous &&
        unique_exact_id != NULL) {
        state->last_exact_logical_source_id =
            g_strdup (unique_exact_id);
        state->last_exact_repository_id =
            g_strdup (unique_exact_repository);
    }

    state->last_intents = results->intents;
    state->has_previous_turn = TRUE;
}

static gboolean
run_internal (
    AtmRetrievalConversationState *state,
    const char *query,
    guint max_results_per_repository,
    gboolean commit_state,
    AtmRetrievalConversationTurn **out_turn,
    GError **error
)
{
    AtmRetrievalScopeSelection *explicit_selection = NULL;
    AtmNormalizedQuery *current_normalized = NULL;
    AtmRetrievalConversationTurn *turn = NULL;
    GPtrArray *effective_scopes = NULL;
    gchar **tokens = NULL;
    GString *effective_query = NULL;
    gsize query_length;
    gboolean contextual;
    gboolean comparison;
    gboolean substantive;
    gboolean scope_changed = FALSE;

    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (out_turn != NULL, FALSE);
    g_return_val_if_fail (*out_turn == NULL, FALSE);

    query_length = strlen (query);

    if (query_length == 0 ||
        query_length > ATM_RETRIEVAL_ROUTER_MAX_QUERY_BYTES ||
        !g_utf8_validate (query, query_length, NULL) ||
        max_results_per_repository == 0 ||
        max_results_per_repository >
            ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_CONVERSATION_ERROR,
            ATM_RETRIEVAL_CONVERSATION_ERROR_ARGUMENT,
            "Retrieval conversation arguments are invalid."
        );
        return FALSE;
    }

    if (!atm_retrieval_scope_select (
            query,
            state->frozen_repositories,
            &explicit_selection,
            error
        ) ||
        !atm_retrieval_normalize_query (
            query,
            &current_normalized,
            error
        )) {
        goto out;
    }

    tokens = g_str_tokenize_and_fold (
        query,
        NULL,
        NULL
    );
    comparison = query_is_comparison_followup (tokens);
    contextual = query_is_contextual_followup (
        tokens,
        explicit_selection->explicit_scope
    );
    substantive = query_has_substantive_noncontext_terms (
        tokens
    );

    turn = g_new0 (AtmRetrievalConversationTurn, 1);
    turn->comparison_followup =
        comparison && state->has_previous_turn;

    if (explicit_selection->requested_outside_scope) {
        effective_scopes = clone_scopes (
            state->frozen_repositories
        );
        effective_query = g_string_new (query);
        goto retrieve;
    }

    if (contextual && !state->has_previous_turn) {
        turn->needs_clarification = TRUE;
        turn->effective_query = g_strdup (query);
        *out_turn = g_steal_pointer (&turn);
        goto success;
    }

    if (comparison &&
        state->has_previous_turn &&
        explicit_selection->explicit_scope) {
        effective_scopes = union_previous_and_explicit (
            state,
            explicit_selection->repositories
        );
        turn->used_previous_scope = TRUE;
    } else if (explicit_selection->explicit_scope) {
        effective_scopes = clone_scopes (
            explicit_selection->repositories
        );
    } else if (contextual &&
               state->has_previous_turn &&
               state->last_repository_ids->len > 0) {
        effective_scopes = scopes_from_last_ids (state);
        turn->used_previous_scope = TRUE;
    } else {
        effective_scopes = clone_scopes (
            state->frozen_repositories
        );
    }

    if (state->has_previous_turn &&
        state->last_repository_ids->len > 0) {
        if (effective_scopes->len !=
            state->last_repository_ids->len) {
            scope_changed = TRUE;
        } else {
            for (guint i = 0; i < effective_scopes->len; i++) {
                const AtmRetrievalRepositoryScope *scope =
                    g_ptr_array_index (
                        effective_scopes,
                        i
                    );

                if (!ids_contain (
                        state->last_repository_ids,
                        scope->repository_id
                    )) {
                    scope_changed = TRUE;
                    break;
                }
            }
        }
    }

    if (contextual &&
        state->has_previous_turn &&
        !substantive &&
        state->last_exact_logical_source_id != NULL &&
        state->last_intents == ATM_RETRIEVAL_INTENT_GENERAL &&
        scope_changed &&
        !single_scope_matches_anchor (
            effective_scopes,
            state
        )) {
        turn->needs_clarification = TRUE;
        turn->effective_query = g_strdup (query);
        *out_turn = g_steal_pointer (&turn);
        goto success;
    }

    effective_query = g_string_new (query);

    if (contextual && state->has_previous_turn) {
        if (state->last_intents !=
                ATM_RETRIEVAL_INTENT_GENERAL &&
            should_inherit_previous_intent (
                current_normalized->intents
            )) {
            guint inherited_intents =
                state->last_intents &
                ~current_normalized->intents;

            if (inherited_intents !=
                ATM_RETRIEVAL_INTENT_GENERAL) {
                append_intent_keywords (
                    effective_query,
                    inherited_intents
                );
                turn->used_previous_intent = TRUE;
            }
        }

        if (single_scope_matches_anchor (
                effective_scopes,
                state
            )) {
            g_string_append_c (effective_query, ' ');
            g_string_append (
                effective_query,
                state->last_exact_logical_source_id
            );
            turn->used_previous_anchor = TRUE;
        }
    }

    if (comparison &&
        state->has_previous_turn &&
        explicit_selection->explicit_scope) {
        append_scope_ids (
            effective_query,
            effective_scopes
        );
    }

retrieve:
    if (effective_query->len >
        ATM_RETRIEVAL_ROUTER_MAX_QUERY_BYTES) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_CONVERSATION_ERROR,
            ATM_RETRIEVAL_CONVERSATION_ERROR_ARGUMENT,
            "Effective retrieval follow-up query exceeds the configured limit."
        );
        goto out;
    }

    turn->effective_query = g_strdup (
        effective_query->str
    );

    if (!atm_retrieval_run (
            turn->effective_query,
            effective_scopes,
            max_results_per_repository,
            &turn->retrieval,
            error
        )) {
        goto out;
    }

    if (commit_state &&
        !turn->retrieval->requested_outside_scope &&
        turn->retrieval->repositories->len > 0) {
        update_last_state (
            state,
            turn->retrieval
        );
    }

    *out_turn = g_steal_pointer (&turn);

success:
    g_strfreev (tokens);
    if (effective_query != NULL) {
        g_string_free (effective_query, TRUE);
        effective_query = NULL;
    }
    g_clear_pointer (
        &effective_scopes,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &current_normalized,
        atm_normalized_query_free
    );
    g_clear_pointer (
        &explicit_selection,
        atm_retrieval_scope_selection_free
    );
    return TRUE;

out:
    g_strfreev (tokens);

    if (effective_query != NULL) {
        g_string_free (effective_query, TRUE);
    }

    g_clear_pointer (
        &effective_scopes,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &turn,
        atm_retrieval_conversation_turn_free
    );
    g_clear_pointer (
        &current_normalized,
        atm_normalized_query_free
    );
    g_clear_pointer (
        &explicit_selection,
        atm_retrieval_scope_selection_free
    );
    return FALSE;
}


gboolean
atm_retrieval_conversation_prepare (
    AtmRetrievalConversationState *state,
    const char *query,
    guint max_results_per_repository,
    AtmRetrievalConversationTurn **out_turn,
    GError **error
)
{
    return run_internal (
        state,
        query,
        max_results_per_repository,
        FALSE,
        out_turn,
        error
    );
}

gboolean
atm_retrieval_conversation_commit (
    AtmRetrievalConversationState *state,
    const AtmRetrievalConversationTurn *turn,
    GError **error
)
{
    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (turn != NULL, FALSE);

    if (turn->needs_clarification ||
        turn->retrieval == NULL) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_CONVERSATION_ERROR,
            ATM_RETRIEVAL_CONVERSATION_ERROR_ARGUMENT,
            "Only a completed retrieval turn can be committed."
        );
        return FALSE;
    }

    if (!turn->retrieval->requested_outside_scope &&
        turn->retrieval->repositories->len > 0) {
        update_last_state (
            state,
            turn->retrieval
        );
    }

    return TRUE;
}

gboolean
atm_retrieval_conversation_run (
    AtmRetrievalConversationState *state,
    const char *query,
    guint max_results_per_repository,
    AtmRetrievalConversationTurn **out_turn,
    GError **error
)
{
    return run_internal (
        state,
        query,
        max_results_per_repository,
        TRUE,
        out_turn,
        error
    );
}
