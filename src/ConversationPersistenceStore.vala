namespace AskTheModel {
    public errordomain ConversationPersistenceError {
        INVALID_SESSION,
        INVALID_CITATION
    }

    public class ConversationPersistenceStore : Object {
        private string state_path;

        public ConversationPersistenceStore (
            string? state_root = null
        ) {
            string root =
                state_root ??
                GLib.Environment.get_user_state_dir ();

            state_path = GLib.Path.build_filename (
                root,
                "conversations.sqlite3"
            );
        }

        public string path () {
            return state_path;
        }

        public string create_for_session (
            ConversationSession session,
            string title = "New"
        ) throws GLib.Error {
            if (!session.is_active ()) {
                throw new ConversationPersistenceError.INVALID_SESSION (
                    "A durable conversation requires an active pinned session."
                );
            }

            string? model_name =
                session.model_name ();

            if (model_name == null ||
                model_name.strip ().length == 0) {
                throw new ConversationPersistenceError.INVALID_SESSION (
                    "A durable conversation requires a pinned AI model."
                );
            }

            string[] repository_ids = {};
            string[] repository_versions = {};
            string[] repository_shas = {};

            for (
                uint i = 0;
                i < session.repository_count ();
                i++
            ) {
                string? repository_id =
                    session.repository_id_at (i);
                string? repository_version =
                    session.repository_version_at (i);
                string? repository_sha =
                    session.repository_sha_at (i);

                if (repository_id == null ||
                    repository_version == null ||
                    repository_sha == null) {
                    throw new ConversationPersistenceError.INVALID_SESSION (
                        "Pinned repository identity is incomplete."
                    );
                }

                repository_ids += repository_id;
                repository_versions +=
                    repository_version;
                repository_shas += repository_sha;
            }

            int64 generation_id =
                session.repository_generation_id ();

            if ((repository_ids.length == 0 &&
                 generation_id != 0) ||
                (repository_ids.length > 0 &&
                 generation_id <= 0)) {
                throw new ConversationPersistenceError.INVALID_SESSION (
                    "Pinned repository generation does not match the conversation repository scope."
                );
            }

            string conversation_id;

            ConversationStoreNative.create_conversation_values (
                state_path,
                title,
                GLib.get_real_time (),
                model_name,
                session.model_digest (),
                generation_id,
                repository_ids,
                repository_versions,
                repository_shas,
                (size_t) repository_ids.length,
                out conversation_id
            );

            return conversation_id;
        }

        public int64 commit_turn (
            string conversation_id,
            string user_content,
            string assistant_provider_content,
            string assistant_display_content,
            bool grounded,
            CitationResolution? resolution = null
        ) throws GLib.Error {
            if (conversation_id.strip ().length == 0) {
                throw new ConversationPersistenceError.INVALID_SESSION (
                    "Durable conversation identity is empty."
                );
            }

            string[] labels = {};
            string[] repository_ids = {};
            string[] repository_versions = {};
            string[] snapshot_shas = {};
            string[] logical_source_ids = {};
            string[] source_paths = {};
            string[] locators = {};
            string[] titles = {};
            string[] excerpts = {};

            if (resolution != null) {
                if (resolution.unknown_label_count () > 0) {
                    throw new ConversationPersistenceError.INVALID_CITATION (
                        "Citation resolution contains unknown source labels."
                    );
                }

                for (
                    uint i = 0;
                    i < resolution.citation_count ();
                    i++
                ) {
                    CitationReference? citation =
                        resolution.citation_at (i);

                    if (citation == null) {
                        throw new ConversationPersistenceError.INVALID_CITATION (
                            "Resolved citation is unavailable."
                        );
                    }

                    labels += citation.label;
                    repository_ids +=
                        citation.repository_id;
                    repository_versions +=
                        citation.repository_version;
                    snapshot_shas +=
                        citation.snapshot_sha;
                    logical_source_ids +=
                        citation.logical_source_id;
                    source_paths +=
                        citation.source_path;
                    locators += citation.locator;
                    titles += citation.title ?? "";
                    excerpts += citation.excerpt ?? "";
                }
            }

            if (!grounded &&
                labels.length > 0) {
                throw new ConversationPersistenceError.INVALID_CITATION (
                    "Ungrounded durable turns cannot contain repository citations."
                );
            }

            int64 turn_no;

            ConversationStoreNative.commit_turn_values (
                state_path,
                conversation_id,
                user_content,
                assistant_provider_content,
                assistant_display_content,
                grounded,
                GLib.get_real_time (),
                labels,
                repository_ids,
                repository_versions,
                snapshot_shas,
                logical_source_ids,
                source_paths,
                locators,
                titles,
                excerpts,
                (size_t) labels.length,
                out turn_no
            );

            return turn_no;
        }
    }
}
