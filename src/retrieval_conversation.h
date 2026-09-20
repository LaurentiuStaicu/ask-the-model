#pragma once

#include <glib.h>

#include "retrieval_router.h"

G_BEGIN_DECLS

typedef enum {
    ATM_RETRIEVAL_CONVERSATION_ERROR_ARGUMENT
} AtmRetrievalConversationError;

#define ATM_RETRIEVAL_CONVERSATION_ERROR \
    (atm_retrieval_conversation_error_quark ())

typedef struct AtmRetrievalConversationState
    AtmRetrievalConversationState;

typedef struct {
    AtmRetrievalResultSet *retrieval;
    char *effective_query;
    gboolean used_previous_scope;
    gboolean used_previous_intent;
    gboolean used_previous_anchor;
    gboolean comparison_followup;
    gboolean needs_clarification;
} AtmRetrievalConversationTurn;

GQuark atm_retrieval_conversation_error_quark (void);

AtmRetrievalConversationState *
atm_retrieval_conversation_state_new (
    const GPtrArray *frozen_repositories,
    GError **error
);

void atm_retrieval_conversation_state_free (
    AtmRetrievalConversationState *state
);

void atm_retrieval_conversation_turn_free (
    AtmRetrievalConversationTurn *turn
);

gboolean atm_retrieval_conversation_run (
    AtmRetrievalConversationState *state,
    const char *query,
    guint max_results_per_repository,
    AtmRetrievalConversationTurn **out_turn,
    GError **error
);

G_END_DECLS
