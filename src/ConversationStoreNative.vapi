[CCode (cheader_filename = "conversation_store.h")]
namespace AskTheModel.ConversationStoreNative {
    [Compact]
    [CCode (
        cname = "AtmConversationStore",
        free_function = "atm_conversation_store_close",
        has_type_id = false
    )]
    public class Store {
    }

    [CCode (cname = "atm_conversation_store_open")]
    public static bool open (
        string path,
        out Store store
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_create_conversation_values")]
    public static bool create_conversation_values (
        Store store,
        string title,
        int64 created_at_us,
        string model_name,
        string? model_digest,
        int64 repository_generation_id,
        [CCode (array_length = false)] string[] repository_ids,
        [CCode (array_length = false)] string[] repository_versions,
        [CCode (array_length = false)] string[] snapshot_shas,
        size_t repository_count,
        out string conversation_id
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_commit_turn_values")]
    public static bool commit_turn_values (
        Store store,
        string conversation_id,
        string user_content,
        string assistant_provider_content,
        string assistant_display_content,
        bool grounded,
        int64 created_at_us,
        [CCode (array_length = false)] string[] labels,
        [CCode (array_length = false)] string[] repository_ids,
        [CCode (array_length = false)] string[] repository_versions,
        [CCode (array_length = false)] string[] snapshot_shas,
        [CCode (array_length = false)] string[] logical_source_ids,
        [CCode (array_length = false)] string[] source_paths,
        [CCode (array_length = false)] string[] locators,
        [CCode (array_length = false)] string[] titles,
        [CCode (array_length = false)] string[] excerpts,
        [CCode (array_length = false)] string[] immutable_permalinks,
        size_t citation_count,
        out int64 turn_no
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_update_title")]
    public static bool update_title (
        Store store,
        string conversation_id,
        string title,
        int64 updated_at_us
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_set_archived")]
    public static bool set_archived (
        Store store,
        string conversation_id,
        bool archived,
        int64 updated_at_us
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_set_open_on_startup")]
    public static bool set_open_on_startup (
        Store store,
        string conversation_id,
        bool open_on_startup
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_delete_conversation")]
    public static bool delete_conversation (
        Store store,
        string conversation_id
    ) throws GLib.Error;
    [Compact]
    [CCode (
        cname = "AtmConversationList",
        free_function = "atm_conversation_list_free",
        has_type_id = false
    )]
    public class ConversationList {
    }

    [Compact]
    [CCode (
        cname = "AtmConversationSnapshot",
        free_function = "atm_conversation_snapshot_free",
        has_type_id = false
    )]
    public class ConversationSnapshot {
    }

    [CCode (cname = "atm_conversation_store_list_conversations")]
    public static bool list_conversations (
        Store store,
        out ConversationList list
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_list_count")]
    public static uint list_count (
        ConversationList list
    );

    [CCode (cname = "atm_conversation_list_id_at")]
    public static unowned string? list_id_at (
        ConversationList list,
        uint index
    );

    [CCode (cname = "atm_conversation_list_title_at")]
    public static unowned string? list_title_at (
        ConversationList list,
        uint index
    );

    [CCode (cname = "atm_conversation_list_created_at_us_at")]
    public static int64 list_created_at_us_at (
        ConversationList list,
        uint index
    );

    [CCode (cname = "atm_conversation_list_updated_at_us_at")]
    public static int64 list_updated_at_us_at (
        ConversationList list,
        uint index
    );

    [CCode (cname = "atm_conversation_list_archived_at")]
    public static bool list_archived_at (
        ConversationList list,
        uint index
    );

    [CCode (cname = "atm_conversation_list_open_on_startup_at")]
    public static bool list_open_on_startup_at (
        ConversationList list,
        uint index
    );

    [CCode (cname = "atm_conversation_store_load_snapshot")]
    public static bool load_snapshot (
        Store store,
        string conversation_id,
        out ConversationSnapshot snapshot
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_snapshot_id")]
    public static unowned string? snapshot_id (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_title")]
    public static unowned string? snapshot_title (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_created_at_us")]
    public static int64 snapshot_created_at_us (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_updated_at_us")]
    public static int64 snapshot_updated_at_us (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_model_name")]
    public static unowned string? snapshot_model_name (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_model_digest")]
    public static unowned string? snapshot_model_digest (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_repository_generation_id")]
    public static int64 snapshot_repository_generation_id (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_archived")]
    public static bool snapshot_archived (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_repository_count")]
    public static uint snapshot_repository_count (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_repository_id_at")]
    public static unowned string? snapshot_repository_id_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_repository_version_at")]
    public static unowned string? snapshot_repository_version_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_repository_sha_at")]
    public static unowned string? snapshot_repository_sha_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_count")]
    public static uint snapshot_message_count (
        ConversationSnapshot snapshot
    );

    [CCode (cname = "atm_conversation_snapshot_message_sequence_no_at")]
    public static int64 snapshot_message_sequence_no_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_turn_no_at")]
    public static int64 snapshot_message_turn_no_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_role_at")]
    public static unowned string? snapshot_message_role_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_provider_content_at")]
    public static unowned string? snapshot_message_provider_content_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_display_content_at")]
    public static unowned string? snapshot_message_display_content_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_grounded_at")]
    public static bool snapshot_message_grounded_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_created_at_us_at")]
    public static int64 snapshot_message_created_at_us_at (
        ConversationSnapshot snapshot,
        uint index
    );

    [CCode (cname = "atm_conversation_snapshot_message_citation_count_at")]
    public static uint snapshot_message_citation_count_at (
        ConversationSnapshot snapshot,
        uint message_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_label_at")]
    public static unowned string? snapshot_citation_label_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_repository_id_at")]
    public static unowned string? snapshot_citation_repository_id_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_repository_version_at")]
    public static unowned string? snapshot_citation_repository_version_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_snapshot_sha_at")]
    public static unowned string? snapshot_citation_snapshot_sha_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_logical_source_id_at")]
    public static unowned string? snapshot_citation_logical_source_id_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_source_path_at")]
    public static unowned string? snapshot_citation_source_path_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_locator_at")]
    public static unowned string? snapshot_citation_locator_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_title_at")]
    public static unowned string? snapshot_citation_title_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_excerpt_at")]
    public static unowned string? snapshot_citation_excerpt_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

    [CCode (cname = "atm_conversation_snapshot_citation_immutable_permalink_at")]
    public static unowned string? snapshot_citation_immutable_permalink_at (
        ConversationSnapshot snapshot,
        uint message_index,
        uint citation_index
    );

}
