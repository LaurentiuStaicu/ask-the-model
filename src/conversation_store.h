#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CONVERSATION_STORE_APPLICATION_ID 0x41544331
#define ATM_CONVERSATION_STORE_SCHEMA_VERSION 1
#define ATM_CONVERSATION_STORE_SCHEMA_ID "atm-conversation-store/1"

typedef enum {
    ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
    ATM_CONVERSATION_STORE_ERROR_IO,
    ATM_CONVERSATION_STORE_ERROR_SQLITE,
    ATM_CONVERSATION_STORE_ERROR_IDENTITY,
    ATM_CONVERSATION_STORE_ERROR_SCHEMA,
    ATM_CONVERSATION_STORE_ERROR_INTEGRITY
} AtmConversationStoreError;

#define ATM_CONVERSATION_STORE_ERROR \
    (atm_conversation_store_error_quark ())

typedef struct AtmConversationStore AtmConversationStore;

GQuark atm_conversation_store_error_quark (void);

gboolean atm_conversation_store_open (
    const char *path,
    AtmConversationStore **out_store,
    GError **error
);

void atm_conversation_store_close (
    AtmConversationStore *store
);

gboolean atm_conversation_store_validate (
    AtmConversationStore *store,
    GError **error
);

const char *atm_conversation_store_path (
    const AtmConversationStore *store
);

gint64 atm_conversation_store_application_id (
    const AtmConversationStore *store
);

gint atm_conversation_store_schema_version (
    const AtmConversationStore *store
);

G_END_DECLS
