namespace AskTheModel {
    [CCode (
        cname = "AtmConversationGroundingState",
        free_function = "atm_conversation_grounding_state_free",
        cheader_filename = "conversation_grounding.h",
        has_type_id = false
    )]
    [Compact]
    public class ConversationGrounding {
        [CCode (cname = "atm_conversation_grounding_state_new")]
        public ConversationGrounding ();

        [CCode (
            cname = "atm_conversation_grounding_add_ready_repository"
        )]
        public extern bool add_ready_repository (
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string snapshot_root,
            string index_path
        ) throws GLib.Error;

        [CCode (cname = "atm_conversation_grounding_freeze")]
        public extern bool freeze () throws GLib.Error;

        [CCode (cname = "atm_conversation_grounding_is_frozen")]
        public extern bool is_frozen ();

        [CCode (
            cname = "atm_conversation_grounding_repository_count"
        )]
        public extern uint repository_count ();
    }
}
