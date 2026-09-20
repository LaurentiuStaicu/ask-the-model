namespace AskTheModel {
    namespace ConversationGroundingNative {
        [CCode (
            cname = "atm_conversation_grounding_state_new",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern void* state_new ();

        [CCode (
            cname = "atm_conversation_grounding_state_free",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern void state_free (void* state);

        [CCode (
            cname = "atm_conversation_grounding_add_ready_repository",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool add_ready_repository (
            void* state,
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string snapshot_root,
            string index_path
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_freeze",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool freeze (
            void* state
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_is_frozen",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool is_frozen (void* state);

        [CCode (
            cname = "atm_conversation_grounding_repository_count",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern uint repository_count (void* state);

        [CCode (
            cname = "atm_conversation_grounding_prepare_turn",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool prepare_turn (
            void* state,
            string query,
            out bool has_grounding,
            out bool needs_clarification,
            out string? system_instructions,
            out string? evidence_text,
            out string? post_evidence_reminder
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_commit_turn",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool commit_turn (
            void* state
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_abort_turn",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern void abort_turn (void* state);
    }

    public class ConversationGrounding : Object {
        private void* state = null;

        public ConversationGrounding () {
            state = ConversationGroundingNative.state_new ();
        }

        ~ConversationGrounding () {
            if (state != null) {
                ConversationGroundingNative.state_free (state);
                state = null;
            }
        }

        public bool add_ready_repository (
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string snapshot_root,
            string index_path
        ) throws GLib.Error {
            return ConversationGroundingNative.add_ready_repository (
                state,
                repository_id,
                repository_version,
                snapshot_sha,
                snapshot_root,
                index_path
            );
        }

        public bool freeze () throws GLib.Error {
            return ConversationGroundingNative.freeze (state);
        }

        public bool is_frozen () {
            return ConversationGroundingNative.is_frozen (state);
        }

        public uint repository_count () {
            return ConversationGroundingNative.repository_count (
                state
            );
        }

        public bool prepare_turn (
            string query,
            out bool needs_clarification,
            out string? system_instructions,
            out string? evidence_text,
            out string? post_evidence_reminder
        ) throws GLib.Error {
            bool has_grounding;

            if (!ConversationGroundingNative.prepare_turn (
                    state,
                    query,
                    out has_grounding,
                    out needs_clarification,
                    out system_instructions,
                    out evidence_text,
                    out post_evidence_reminder
                )) {
                return false;
            }

            return has_grounding;
        }

        public bool commit_turn () throws GLib.Error {
            return ConversationGroundingNative.commit_turn (state);
        }

        public void abort_turn () {
            ConversationGroundingNative.abort_turn (state);
        }
    }
}
