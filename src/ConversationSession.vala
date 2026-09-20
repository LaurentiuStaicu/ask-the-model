namespace AskTheModel {
    public errordomain ConversationSessionError {
        ALREADY_ACTIVE,
        NOT_ACTIVE,
        INVALID_GROUNDING
    }

    public class ConversationSession : Object {
        private ConversationGrounding? grounding = null;

        public bool is_active () {
            return grounding != null;
        }

        public uint repository_count () {
            if (grounding == null) {
                return 0;
            }

            return grounding.repository_count ();
        }

        public void begin (
            ConversationGrounding prepared_grounding
        ) throws GLib.Error {
            if (grounding != null) {
                throw new ConversationSessionError.ALREADY_ACTIVE (
                    "Conversation session is already active."
                );
            }

            if (!prepared_grounding.is_frozen ()) {
                throw new ConversationSessionError.INVALID_GROUNDING (
                    "Conversation grounding must be frozen before the session starts."
                );
            }

            grounding = prepared_grounding;
        }

        public bool prepare_turn (
            string query,
            out bool needs_clarification,
            out string? system_instructions,
            out string? evidence_text,
            out string? post_evidence_reminder
        ) throws GLib.Error {
            if (grounding == null) {
                throw new ConversationSessionError.NOT_ACTIVE (
                    "Conversation session has not started."
                );
            }

            return grounding.prepare_turn (
                query,
                out needs_clarification,
                out system_instructions,
                out evidence_text,
                out post_evidence_reminder
            );
        }

        public bool commit_turn () throws GLib.Error {
            if (grounding == null) {
                throw new ConversationSessionError.NOT_ACTIVE (
                    "Conversation session has not started."
                );
            }

            return grounding.commit_turn ();
        }

        public void abort_turn () {
            if (grounding != null) {
                grounding.abort_turn ();
            }
        }

        public void reset () {
            grounding = null;
        }
    }
}
