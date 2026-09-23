namespace AskTheModel {
    public class ConversationPersistenceRepository : Object {
        public string repository_id { get; construct; }
        public string repository_version { get; construct; }
        public string snapshot_sha { get; construct; }

        public ConversationPersistenceRepository (
            string repository_id,
            string repository_version,
            string snapshot_sha
        ) {
            Object (
                repository_id: repository_id,
                repository_version: repository_version,
                snapshot_sha: snapshot_sha
            );
        }
    }

    public class ConversationPersistenceCitation : Object {
        public string label { get; construct; }
        public string repository_id { get; construct; }
        public string repository_version { get; construct; }
        public string snapshot_sha { get; construct; }
        public string logical_source_id { get; construct; }
        public string source_path { get; construct; }
        public string locator { get; construct; }
        public string? title { get; construct; }
        public string? excerpt { get; construct; }

        public ConversationPersistenceCitation (
            string label,
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string logical_source_id,
            string source_path,
            string locator,
            string? title = null,
            string? excerpt = null
        ) {
            Object (
                label: label,
                repository_id: repository_id,
                repository_version: repository_version,
                snapshot_sha: snapshot_sha,
                logical_source_id: logical_source_id,
                source_path: source_path,
                locator: locator,
                title: title,
                excerpt: excerpt
            );
        }
    }

    public class ConversationPersistenceStore : Object {
        private ConversationStoreNative.Store native_store;
        public string path { get; private set; }

        public ConversationPersistenceStore (
            string? state_root = null
        ) throws GLib.Error {
            string root = state_root ??
                GLib.Environment.get_user_state_dir ();

            path = GLib.Path.build_filename (
                root,
                "conversations.sqlite3"
            );

            ConversationStoreNative.Store opened;

            if (!ConversationStoreNative.open (
                    path,
                    out opened
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation persistence store could not be opened."
                );
            }

            native_store = (owned) opened;
        }

        public string create_conversation (
            string title,
            int64 created_at_us,
            string model_name,
            string? model_digest,
            int64 repository_generation_id,
            ConversationPersistenceRepository[] repositories
        ) throws GLib.Error {
            string[] repository_ids = {};
            string[] repository_versions = {};
            string[] snapshot_shas = {};

            foreach (
                ConversationPersistenceRepository repository
                in repositories
            ) {
                repository_ids += repository.repository_id;
                repository_versions +=
                    repository.repository_version;
                snapshot_shas += repository.snapshot_sha;
            }

            string conversation_id;

            if (!ConversationStoreNative.create_conversation_values (
                    native_store,
                    title,
                    created_at_us,
                    model_name,
                    model_digest,
                    repository_generation_id,
                    repository_ids,
                    repository_versions,
                    snapshot_shas,
                    (size_t) repositories.length,
                    out conversation_id
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation identity could not be persisted."
                );
            }

            return conversation_id;
        }

        public int64 commit_turn (
            string conversation_id,
            string user_content,
            string assistant_provider_content,
            string assistant_display_content,
            bool grounded,
            int64 created_at_us,
            ConversationPersistenceCitation[] citations
        ) throws GLib.Error {
            string[] labels = {};
            string[] repository_ids = {};
            string[] repository_versions = {};
            string[] snapshot_shas = {};
            string[] logical_source_ids = {};
            string[] source_paths = {};
            string[] locators = {};
            string[] titles = {};
            string[] excerpts = {};

            foreach (
                ConversationPersistenceCitation citation
                in citations
            ) {
                labels += citation.label;
                repository_ids += citation.repository_id;
                repository_versions +=
                    citation.repository_version;
                snapshot_shas += citation.snapshot_sha;
                logical_source_ids +=
                    citation.logical_source_id;
                source_paths += citation.source_path;
                locators += citation.locator;
                titles += citation.title ?? "";
                excerpts += citation.excerpt ?? "";
            }

            int64 turn_no;

            if (!ConversationStoreNative.commit_turn_values (
                    native_store,
                    conversation_id,
                    user_content,
                    assistant_provider_content,
                    assistant_display_content,
                    grounded,
                    created_at_us,
                    labels,
                    repository_ids,
                    repository_versions,
                    snapshot_shas,
                    logical_source_ids,
                    source_paths,
                    locators,
                    titles,
                    excerpts,
                    (size_t) citations.length,
                    out turn_no
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation turn could not be persisted."
                );
            }

            return turn_no;
        }

        public void update_title (
            string conversation_id,
            string title,
            int64 updated_at_us
        ) throws GLib.Error {
            if (!ConversationStoreNative.update_title (
                    native_store,
                    conversation_id,
                    title,
                    updated_at_us
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation title could not be persisted."
                );
            }
        }
    }

    public class ConversationTurnCommitter : Object {
        public static int64 commit (
            ConversationPersistenceStore? store,
            string? conversation_id,
            OllamaConversation conversation,
            string prompt,
            string assistant_provider_content,
            string assistant_display_content,
            bool grounded,
            int64 created_at_us,
            ConversationPersistenceCitation[] citations
        ) throws GLib.Error {
            if ((store == null) !=
                (conversation_id == null)) {
                throw new GLib.IOError.INVALID_DATA (
                    "Conversation persistence store and durable conversation identity must be available together."
                );
            }

            int64 turn_no = -1;

            if (store != null &&
                conversation_id != null) {
                turn_no = store.commit_turn (
                    conversation_id,
                    prompt,
                    assistant_provider_content,
                    assistant_display_content,
                    grounded,
                    created_at_us,
                    citations
                );
            }

            conversation.commit_exchange (
                prompt,
                assistant_provider_content
            );

            return turn_no;
        }
    }
}
