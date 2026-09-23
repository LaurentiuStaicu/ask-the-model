[CCode (cheader_filename = "conversation_store.h")]
namespace AskTheModel.ConversationStoreNative {
    [CCode (cname = "atm_conversation_store_create_conversation_values")]
    public static bool create_conversation_values (
        string path,
        string title,
        int64 created_at_us,
        string model_name,
        string? model_digest,
        int64 repository_generation_id,
        [CCode (array_length = false)] string[] repository_ids,
        [CCode (array_length = false)] string[] repository_versions,
        [CCode (array_length = false)] string[] repository_shas,
        size_t repository_count,
        out string conversation_id
    ) throws GLib.Error;

    [CCode (cname = "atm_conversation_store_commit_turn_values")]
    public static bool commit_turn_values (
        string path,
        string conversation_id,
        string user_content,
        string assistant_provider_content,
        string assistant_display_content,
        bool grounded,
        int64 created_at_us,
        [CCode (array_length = false)] string[] citation_labels,
        [CCode (array_length = false)] string[] citation_repository_ids,
        [CCode (array_length = false)] string[] citation_repository_versions,
        [CCode (array_length = false)] string[] citation_snapshot_shas,
        [CCode (array_length = false)] string[] citation_logical_source_ids,
        [CCode (array_length = false)] string[] citation_source_paths,
        [CCode (array_length = false)] string[] citation_locators,
        [CCode (array_length = false)] string[] citation_titles,
        [CCode (array_length = false)] string[] citation_excerpts,
        size_t citation_count,
        out int64 turn_no
    ) throws GLib.Error;
}
