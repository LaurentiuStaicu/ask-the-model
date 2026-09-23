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
    ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
    ATM_CONVERSATION_STORE_ERROR_NOT_FOUND
} AtmConversationStoreError;

#define ATM_CONVERSATION_STORE_ERROR \
    (atm_conversation_store_error_quark ())

typedef struct AtmConversationStore AtmConversationStore;

typedef struct AtmConversationList AtmConversationList;
typedef struct AtmConversationSnapshot AtmConversationSnapshot;

typedef struct {
    const char *repository_id;
    const char *repository_version;
    const char *snapshot_sha;
} AtmConversationRepositoryInput;

typedef struct {
    const char *label;
    const char *repository_id;
    const char *repository_version;
    const char *snapshot_sha;
    const char *logical_source_id;
    const char *source_path;
    const char *locator;
    const char *title;
    const char *excerpt;
    const char *immutable_permalink;
} AtmConversationCitationInput;

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

gboolean atm_conversation_store_create_conversation (
    AtmConversationStore *store,
    const char *title,
    gint64 created_at_us,
    const char *model_name,
    const char *model_digest,
    gint64 repository_generation_id,
    const AtmConversationRepositoryInput *repositories,
    gsize repository_count,
    char **out_conversation_id,
    GError **error
);

gboolean atm_conversation_store_commit_turn (
    AtmConversationStore *store,
    const char *conversation_id,
    const char *user_content,
    const char *assistant_provider_content,
    const char *assistant_display_content,
    gboolean grounded,
    gint64 created_at_us,
    const AtmConversationCitationInput *citations,
    gsize citation_count,
    gint64 *out_turn_no,
    GError **error
);

gboolean atm_conversation_store_update_title (
    AtmConversationStore *store,
    const char *conversation_id,
    const char *title,
    gint64 updated_at_us,
    GError **error
);

gboolean atm_conversation_store_set_archived (
    AtmConversationStore *store,
    const char *conversation_id,
    gboolean archived,
    gint64 updated_at_us,
    GError **error
);

gboolean atm_conversation_store_delete_conversation (
    AtmConversationStore *store,
    const char *conversation_id,
    GError **error
);

gboolean atm_conversation_store_create_conversation_values (
    AtmConversationStore *store,
    const char *title,
    gint64 created_at_us,
    const char *model_name,
    const char *model_digest,
    gint64 repository_generation_id,
    const char * const *repository_ids,
    const char * const *repository_versions,
    const char * const *snapshot_shas,
    gsize repository_count,
    char **out_conversation_id,
    GError **error
);

gboolean atm_conversation_store_commit_turn_values (
    AtmConversationStore *store,
    const char *conversation_id,
    const char *user_content,
    const char *assistant_provider_content,
    const char *assistant_display_content,
    gboolean grounded,
    gint64 created_at_us,
    const char * const *labels,
    const char * const *repository_ids,
    const char * const *repository_versions,
    const char * const *snapshot_shas,
    const char * const *logical_source_ids,
    const char * const *source_paths,
    const char * const *locators,
    const char * const *titles,
    const char * const *excerpts,
    const char * const *immutable_permalinks,
    gsize citation_count,
    gint64 *out_turn_no,
    GError **error
);

gboolean atm_conversation_store_list_conversations (
    AtmConversationStore *store,
    AtmConversationList **out_list,
    GError **error
);

void atm_conversation_list_free (
    AtmConversationList *list
);

guint atm_conversation_list_count (
    const AtmConversationList *list
);

const char *atm_conversation_list_id_at (
    const AtmConversationList *list,
    guint index
);

const char *atm_conversation_list_title_at (
    const AtmConversationList *list,
    guint index
);

gint64 atm_conversation_list_created_at_us_at (
    const AtmConversationList *list,
    guint index
);

gint64 atm_conversation_list_updated_at_us_at (
    const AtmConversationList *list,
    guint index
);

gboolean atm_conversation_list_archived_at (
    const AtmConversationList *list,
    guint index
);

gboolean atm_conversation_store_load_snapshot (
    AtmConversationStore *store,
    const char *conversation_id,
    AtmConversationSnapshot **out_snapshot,
    GError **error
);

void atm_conversation_snapshot_free (
    AtmConversationSnapshot *snapshot
);

const char *atm_conversation_snapshot_id (
    const AtmConversationSnapshot *snapshot
);

const char *atm_conversation_snapshot_title (
    const AtmConversationSnapshot *snapshot
);

gint64 atm_conversation_snapshot_created_at_us (
    const AtmConversationSnapshot *snapshot
);

gint64 atm_conversation_snapshot_updated_at_us (
    const AtmConversationSnapshot *snapshot
);

const char *atm_conversation_snapshot_model_name (
    const AtmConversationSnapshot *snapshot
);

const char *atm_conversation_snapshot_model_digest (
    const AtmConversationSnapshot *snapshot
);

gint64 atm_conversation_snapshot_repository_generation_id (
    const AtmConversationSnapshot *snapshot
);

gboolean atm_conversation_snapshot_archived (
    const AtmConversationSnapshot *snapshot
);

guint atm_conversation_snapshot_repository_count (
    const AtmConversationSnapshot *snapshot
);

const char *atm_conversation_snapshot_repository_id_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

const char *atm_conversation_snapshot_repository_version_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

const char *atm_conversation_snapshot_repository_sha_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

guint atm_conversation_snapshot_message_count (
    const AtmConversationSnapshot *snapshot
);

gint64 atm_conversation_snapshot_message_sequence_no_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

gint64 atm_conversation_snapshot_message_turn_no_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

const char *atm_conversation_snapshot_message_role_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

const char *atm_conversation_snapshot_message_provider_content_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

const char *atm_conversation_snapshot_message_display_content_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

gboolean atm_conversation_snapshot_message_grounded_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

gint64 atm_conversation_snapshot_message_created_at_us_at (
    const AtmConversationSnapshot *snapshot,
    guint index
);

guint atm_conversation_snapshot_message_citation_count_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index
);

const char *atm_conversation_snapshot_citation_label_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_repository_id_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_repository_version_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_snapshot_sha_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_logical_source_id_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_source_path_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_locator_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_title_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_excerpt_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

const char *atm_conversation_snapshot_citation_immutable_permalink_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
);

G_END_DECLS
