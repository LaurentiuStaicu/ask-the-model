namespace AskTheModel {
    public errordomain ConversationSessionError {
        ALREADY_ACTIVE,
        NOT_ACTIVE,
        INVALID_GROUNDING,
        INVALID_MODEL,
        MODEL_MISMATCH
    }

    public class ConversationSession : Object {
        private ConversationGrounding? grounding = null;
        private string? pinned_model = null;

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
            ConversationGrounding prepared_grounding,
            string model_name
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

            string normalized_model = model_name.strip ();

            if (normalized_model.length == 0) {
                throw new ConversationSessionError.INVALID_MODEL (
                    "Conversation AI model must be known before the session starts."
                );
            }

            grounding = prepared_grounding;
            pinned_model = normalized_model;
        }

        public string? model_name () {
            return pinned_model;
        }

        public void require_model (
            string current_model
        ) throws GLib.Error {
            if (grounding == null || pinned_model == null) {
                throw new ConversationSessionError.NOT_ACTIVE (
                    "Conversation session has not started."
                );
            }

            if (current_model.strip () != pinned_model) {
                throw new ConversationSessionError.MODEL_MISMATCH (
                    "The active AI model differs from the model pinned to this conversation."
                );
            }
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
            if (grounding != null) {
                grounding.abort_turn ();
                grounding = null;
            }

            pinned_model = null;
        }
    }
}
