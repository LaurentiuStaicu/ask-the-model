#include "conversation_grounding.h"

#include "cff_version.h"
#include "grounding_context.h"
#include "retrieval_conversation.h"
#include "retrieval_index.h"

#include <string.h>

struct AtmConversationGroundingState {
    GPtrArray *repositories;
    AtmRetrievalConversationState *retrieval_conversation;
    AtmRetrievalConversationTurn *pending_retrieval_turn;
    AtmGroundingContext *current_grounding_context;
    gboolean frozen;
};

GQuark
atm_conversation_grounding_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-conversation-grounding-error-quark"
    );
}

static void
repository_pin_free (AtmConversationRepositoryPin *pin)
{
    if (pin == NULL) {
        return;
    }

    g_free (pin->repository_id);
    g_free (pin->repository_version);
    g_free (pin->snapshot_sha);
    g_free (pin->snapshot_root);
    g_free (pin->index_path);
    g_free (pin);
}

AtmConversationGroundingState *
atm_conversation_grounding_state_new (void)
{
    AtmConversationGroundingState *state = g_new0 (
        AtmConversationGroundingState,
        1
    );

    state->repositories = g_ptr_array_new_with_free_func (
        (GDestroyNotify) repository_pin_free
    );

    return state;
}

void
atm_conversation_grounding_state_free (
    AtmConversationGroundingState *state
)
{
    if (state == NULL) {
        return;
    }

    g_clear_pointer (
        &state->pending_retrieval_turn,
        atm_retrieval_conversation_turn_free
    );
    g_clear_pointer (
        &state->current_grounding_context,
        atm_grounding_context_free
    );
    g_clear_pointer (
        &state->retrieval_conversation,
        atm_retrieval_conversation_state_free
    );
    g_clear_pointer (&state->repositories, g_ptr_array_unref);
    g_free (state);
}

static gboolean
known_repository_id (const char *repository_id)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static gint
repository_order (const char *repository_id)
{
    if (g_strcmp0 (repository_id, "ewd") == 0) {
        return 0;
    }

    if (g_strcmp0 (repository_id, "cbd") == 0) {
        return 1;
    }

    if (g_strcmp0 (repository_id, "rmd") == 0) {
        return 2;
    }

    return 99;
}

static gboolean
lowercase_sha_is_valid (const char *sha)
{
    if (sha == NULL || strlen (sha) != 40) {
        return FALSE;
    }

    for (guint i = 0; i < 40; i++) {
        if (!g_ascii_isxdigit (sha[i]) ||
            (sha[i] >= 'A' && sha[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
nonempty_utf8 (const char *value)
{
    return value != NULL &&
        value[0] != '\0' &&
        g_utf8_validate (value, -1, NULL);
}

static gboolean
read_snapshot_version (
    const char *snapshot_root,
    char **out_version,
    GError **error
)
{
    char *path = g_build_filename (
        snapshot_root,
        "CITATION.cff",
        NULL
    );
    char *contents = NULL;
    gsize length = 0;
    GError *local_error = NULL;
    gboolean ok = FALSE;

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VERSION,
            "Could not read pinned snapshot CITATION.cff: %s",
            local_error != NULL
                ? local_error->message
                : "unknown file error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    if (!atm_cff_extract_version (
            (const guint8 *) contents,
            length,
            out_version,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VERSION,
            "Could not read pinned repository version: %s",
            local_error != NULL
                ? local_error->message
                : "unknown CFF error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    ok = TRUE;

out:
    g_free (contents);
    g_free (path);
    return ok;
}

static gboolean
repository_already_present (
    const AtmConversationGroundingState *state,
    const char *repository_id
)
{
    for (guint i = 0; i < state->repositories->len; i++) {
        const AtmConversationRepositoryPin *pin =
            g_ptr_array_index (
                state->repositories,
                i
            );

        if (g_strcmp0 (
                pin->repository_id,
                repository_id
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
insert_pin_canonical_order (
    AtmConversationGroundingState *state,
    AtmConversationRepositoryPin *pin
)
{
    gint rank = repository_order (pin->repository_id);
    guint position = state->repositories->len;

    for (guint i = 0; i < state->repositories->len; i++) {
        const AtmConversationRepositoryPin *existing =
            g_ptr_array_index (
                state->repositories,
                i
            );

        if (repository_order (existing->repository_id) > rank) {
            position = i;
            break;
        }
    }

    g_ptr_array_insert (
        state->repositories,
        position,
        pin
    );
}

gboolean
atm_conversation_grounding_add_ready_repository (
    AtmConversationGroundingState *state,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *snapshot_root,
    const char *index_path,
    GError **error
)
{
    char *snapshot_version = NULL;
    GError *validation_error = NULL;

    g_return_val_if_fail (state != NULL, FALSE);

    if (state->frozen) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_FROZEN,
            "Repository scope is frozen for this conversation."
        );
        return FALSE;
    }

    if (!known_repository_id (repository_id) ||
        !nonempty_utf8 (repository_version) ||
        !lowercase_sha_is_valid (snapshot_sha) ||
        !nonempty_utf8 (snapshot_root) ||
        !nonempty_utf8 (index_path)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_ARGUMENT,
            "Conversation repository pin arguments are invalid."
        );
        return FALSE;
    }

    if (repository_already_present (
            state,
            repository_id
        )) {
        g_set_error (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_DUPLICATE,
            "Repository '%s' is already selected for this conversation.",
            repository_id
        );
        return FALSE;
    }

    if (!read_snapshot_version (
            snapshot_root,
            &snapshot_version,
            error
        )) {
        return FALSE;
    }

    if (g_strcmp0 (
            snapshot_version,
            repository_version
        ) != 0) {
        g_set_error (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VERSION,
            "Selected version '%s' does not match snapshot version '%s'.",
            repository_version,
            snapshot_version
        );
        g_free (snapshot_version);
        return FALSE;
    }

    g_free (snapshot_version);

    if (!atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &validation_error
        )) {
        g_set_error (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
            "Repository '%s' is not ready for conversation pinning: %s",
            repository_id,
            validation_error != NULL
                ? validation_error->message
                : "index/snapshot validation failed"
        );
        g_clear_error (&validation_error);
        return FALSE;
    }

    AtmConversationRepositoryPin *pin = g_new0 (
        AtmConversationRepositoryPin,
        1
    );

    pin->repository_id = g_strdup (repository_id);
    pin->repository_version = g_strdup (repository_version);
    pin->snapshot_sha = g_strdup (snapshot_sha);
    pin->snapshot_root = g_strdup (snapshot_root);
    pin->index_path = g_strdup (index_path);

    insert_pin_canonical_order (state, pin);
    return TRUE;
}

gboolean
atm_conversation_grounding_freeze (
    AtmConversationGroundingState *state,
    GError **error
)
{
    GPtrArray *scopes = NULL;

    g_return_val_if_fail (state != NULL, FALSE);

    if (state->frozen) {
        return TRUE;
    }

    state->frozen = TRUE;

    if (state->repositories->len == 0) {
        return TRUE;
    }

    if (!atm_conversation_grounding_create_retrieval_scopes (
            state,
            &scopes,
            error
        )) {
        state->frozen = FALSE;
        return FALSE;
    }

    state->retrieval_conversation =
        atm_retrieval_conversation_state_new (
            scopes,
            error
        );

    g_ptr_array_unref (scopes);

    if (state->retrieval_conversation == NULL) {
        state->frozen = FALSE;
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_conversation_grounding_is_frozen (
    const AtmConversationGroundingState *state
)
{
    g_return_val_if_fail (state != NULL, FALSE);
    return state->frozen;
}

guint
atm_conversation_grounding_repository_count (
    const AtmConversationGroundingState *state
)
{
    g_return_val_if_fail (state != NULL, 0);
    return state->repositories->len;
}

const AtmConversationRepositoryPin *
atm_conversation_grounding_repository_at (
    const AtmConversationGroundingState *state,
    guint index
)
{
    g_return_val_if_fail (state != NULL, NULL);

    if (index >= state->repositories->len) {
        return NULL;
    }

    return g_ptr_array_index (
        state->repositories,
        index
    );
}

gboolean
atm_conversation_grounding_create_retrieval_scopes (
    const AtmConversationGroundingState *state,
    GPtrArray **out_scopes,
    GError **error
)
{
    GPtrArray *scopes = NULL;

    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (out_scopes != NULL, FALSE);
    g_return_val_if_fail (*out_scopes == NULL, FALSE);

    if (!state->frozen) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_NOT_FROZEN,
            "Conversation repository scope must be frozen before retrieval."
        );
        return FALSE;
    }

    scopes = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );

    for (guint i = 0; i < state->repositories->len; i++) {
        const AtmConversationRepositoryPin *pin =
            g_ptr_array_index (
                state->repositories,
                i
            );

        g_ptr_array_add (
            scopes,
            atm_retrieval_repository_scope_new (
                pin->repository_id,
                pin->index_path
            )
        );
    }

    *out_scopes = g_steal_pointer (&scopes);
    return TRUE;
}


gboolean
atm_conversation_grounding_prepare_turn (
    AtmConversationGroundingState *state,
    const char *query,
    gboolean *out_has_grounding,
    gboolean *out_needs_clarification,
    char **out_system_instructions,
    char **out_evidence_text,
    char **out_post_evidence_reminder,
    GError **error
)
{
    AtmRetrievalConversationTurn *turn = NULL;
    AtmGroundingContext *context = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (out_has_grounding != NULL, FALSE);
    g_return_val_if_fail (out_needs_clarification != NULL, FALSE);
    g_return_val_if_fail (out_system_instructions != NULL, FALSE);
    g_return_val_if_fail (*out_system_instructions == NULL, FALSE);
    g_return_val_if_fail (out_evidence_text != NULL, FALSE);
    g_return_val_if_fail (*out_evidence_text == NULL, FALSE);
    g_return_val_if_fail (out_post_evidence_reminder != NULL, FALSE);
    g_return_val_if_fail (*out_post_evidence_reminder == NULL, FALSE);

    *out_has_grounding = FALSE;
    *out_needs_clarification = FALSE;

    if (state->pending_retrieval_turn != NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
            "The previous grounded turn must be committed or aborted before preparing another turn."
        );
        return FALSE;
    }

    g_clear_pointer (
        &state->current_grounding_context,
        atm_grounding_context_free
    );

    if (!state->frozen) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_NOT_FROZEN,
            "Conversation repository scope must be frozen before preparing a turn."
        );
        return FALSE;
    }

    if (state->repositories->len == 0) {
        return TRUE;
    }

    if (state->retrieval_conversation == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
            "Conversation retrieval state is unavailable."
        );
        return FALSE;
    }

    if (!atm_retrieval_conversation_prepare (
            state->retrieval_conversation,
            query,
            6,
            &turn,
            error
        )) {
        goto out;
    }

    if (turn->needs_clarification) {
        *out_needs_clarification = TRUE;
        ok = TRUE;
        goto out;
    }

    if (turn->retrieval == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
            "Conversation retrieval produced no result set."
        );
        goto out;
    }

    if (!atm_grounding_context_build (
            turn->retrieval,
            12,
            32 * 1024,
            &context,
            error
        )) {
        goto out;
    }

    state->current_grounding_context =
        g_steal_pointer (&context);
    state->pending_retrieval_turn =
        g_steal_pointer (&turn);

    *out_system_instructions = g_strdup (
        atm_grounding_system_instructions ()
    );
    *out_evidence_text = g_strdup (
        state->current_grounding_context->evidence_text
    );
    *out_post_evidence_reminder = g_strdup (
        atm_grounding_post_evidence_reminder ()
    );
    *out_has_grounding = TRUE;
    ok = TRUE;

out:
    if (!ok) {
        g_clear_pointer (out_system_instructions, g_free);
        g_clear_pointer (out_evidence_text, g_free);
        g_clear_pointer (out_post_evidence_reminder, g_free);
    }

    g_clear_pointer (
        &context,
        atm_grounding_context_free
    );
    g_clear_pointer (
        &turn,
        atm_retrieval_conversation_turn_free
    );
    return ok;
}


gboolean
atm_conversation_grounding_commit_turn (
    AtmConversationGroundingState *state,
    GError **error
)
{
    g_return_val_if_fail (state != NULL, FALSE);

    if (state->pending_retrieval_turn == NULL ||
        state->current_grounding_context == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
            "No prepared grounded turn is available to commit."
        );
        return FALSE;
    }

    if (!atm_retrieval_conversation_commit (
            state->retrieval_conversation,
            state->pending_retrieval_turn,
            error
        )) {
        return FALSE;
    }

    g_clear_pointer (
        &state->pending_retrieval_turn,
        atm_retrieval_conversation_turn_free
    );
    g_clear_pointer (
        &state->current_grounding_context,
        atm_grounding_context_free
    );
    return TRUE;
}

void
atm_conversation_grounding_abort_turn (
    AtmConversationGroundingState *state
)
{
    if (state == NULL) {
        return;
    }

    g_clear_pointer (
        &state->pending_retrieval_turn,
        atm_retrieval_conversation_turn_free
    );
    g_clear_pointer (
        &state->current_grounding_context,
        atm_grounding_context_free
    );
}


gboolean
atm_conversation_grounding_resolve_turn_citations (
    AtmConversationGroundingState *state,
    const char *model_output,
    AtmCitationResolution **out_resolution,
    GError **error
)
{
    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (model_output != NULL, FALSE);
    g_return_val_if_fail (out_resolution != NULL, FALSE);
    g_return_val_if_fail (*out_resolution == NULL, FALSE);

    if (state->pending_retrieval_turn == NULL ||
        state->current_grounding_context == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_GROUNDING_ERROR,
            ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
            "No grounded conversation turn is available for citation resolution."
        );
        return FALSE;
    }

    return atm_citation_resolve_labels (
        model_output,
        state->current_grounding_context,
        out_resolution,
        error
    );
}
