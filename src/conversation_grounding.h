#pragma once

#include <glib.h>

#include "retrieval_scope.h"

G_BEGIN_DECLS

typedef enum {
    ATM_CONVERSATION_GROUNDING_ERROR_ARGUMENT,
    ATM_CONVERSATION_GROUNDING_ERROR_FROZEN,
    ATM_CONVERSATION_GROUNDING_ERROR_DUPLICATE,
    ATM_CONVERSATION_GROUNDING_ERROR_VERSION,
    ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION,
    ATM_CONVERSATION_GROUNDING_ERROR_NOT_FROZEN
} AtmConversationGroundingError;

#define ATM_CONVERSATION_GROUNDING_ERROR \
    (atm_conversation_grounding_error_quark ())

typedef struct {
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *snapshot_root;
    char *index_path;
} AtmConversationRepositoryPin;

typedef struct AtmConversationGroundingState
    AtmConversationGroundingState;

GQuark atm_conversation_grounding_error_quark (void);

AtmConversationGroundingState *
atm_conversation_grounding_state_new (void);

void atm_conversation_grounding_state_free (
    AtmConversationGroundingState *state
);

gboolean atm_conversation_grounding_add_ready_repository (
    AtmConversationGroundingState *state,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *snapshot_root,
    const char *index_path,
    GError **error
);

gboolean atm_conversation_grounding_freeze (
    AtmConversationGroundingState *state,
    GError **error
);

gboolean atm_conversation_grounding_is_frozen (
    const AtmConversationGroundingState *state
);

guint atm_conversation_grounding_repository_count (
    const AtmConversationGroundingState *state
);

const AtmConversationRepositoryPin *
atm_conversation_grounding_repository_at (
    const AtmConversationGroundingState *state,
    guint index
);

gboolean atm_conversation_grounding_create_retrieval_scopes (
    const AtmConversationGroundingState *state,
    GPtrArray **out_scopes,
    GError **error
);

G_END_DECLS
