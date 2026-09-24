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
        public string? immutable_permalink { get; construct; }

        public ConversationPersistenceCitation (
            string label,
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string logical_source_id,
            string source_path,
            string locator,
            string? title = null,
            string? excerpt = null,
            string? immutable_permalink = null
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
                excerpt: excerpt,
                immutable_permalink: immutable_permalink
            );
        }
    }

    public class ConversationPersistenceSummary : Object {
        public string conversation_id { get; construct; }
        public string title { get; construct; }
        public int64 created_at_us { get; construct; }
        public int64 updated_at_us { get; construct; }
        public bool archived { get; construct; }
        public bool open_on_startup { get; construct; }

        public ConversationPersistenceSummary (
            string conversation_id,
            string title,
            int64 created_at_us,
            int64 updated_at_us,
            bool archived,
            bool open_on_startup
        ) {
            Object (
                conversation_id: conversation_id,
                title: title,
                created_at_us: created_at_us,
                updated_at_us: updated_at_us,
                archived: archived,
                open_on_startup: open_on_startup
            );
        }
    }

    public class ConversationPersistenceMessage : Object {
        public int64 sequence_no { get; construct; }
        public int64 turn_no { get; construct; }
        public string role { get; construct; }
        public string provider_content { get; construct; }
        public string display_content { get; construct; }
        public bool grounded { get; construct; }
        public int64 created_at_us { get; construct; }
        public ConversationPersistenceCitation[] citations;

        public ConversationPersistenceMessage (
            int64 sequence_no,
            int64 turn_no,
            string role,
            string provider_content,
            string display_content,
            bool grounded,
            int64 created_at_us,
            ConversationPersistenceCitation[] citations
        ) {
            Object (
                sequence_no: sequence_no,
                turn_no: turn_no,
                role: role,
                provider_content: provider_content,
                display_content: display_content,
                grounded: grounded,
                created_at_us: created_at_us
            );

            this.citations = citations;
        }
    }

    public class ConversationPersistenceSnapshot : Object {
        public string conversation_id { get; construct; }
        public string title { get; construct; }
        public int64 created_at_us { get; construct; }
        public int64 updated_at_us { get; construct; }
        public string model_name { get; construct; }
        public string? model_digest { get; construct; }
        public int64 repository_generation_id { get; construct; }
        public bool archived { get; construct; }
        public ConversationPersistenceRepository[] repositories;
        public ConversationPersistenceMessage[] messages;

        public ConversationPersistenceSnapshot (
            string conversation_id,
            string title,
            int64 created_at_us,
            int64 updated_at_us,
            string model_name,
            string? model_digest,
            int64 repository_generation_id,
            bool archived,
            ConversationPersistenceRepository[] repositories,
            ConversationPersistenceMessage[] messages
        ) {
            Object (
                conversation_id: conversation_id,
                title: title,
                created_at_us: created_at_us,
                updated_at_us: updated_at_us,
                model_name: model_name,
                model_digest: model_digest,
                repository_generation_id: repository_generation_id,
                archived: archived
            );

            this.repositories = repositories;
            this.messages = messages;
        }

        public bool model_identity_available (
            string[] available_names,
            string[] available_digests
        ) {
            if (available_names.length !=
                available_digests.length) {
                return false;
            }

            string expected_name =
                model_name.strip ();

            if (expected_name.length == 0) {
                return false;
            }

            string expected_digest =
                model_digest != null
                    ? model_digest.strip ()
                    : "";

            for (
                int i = 0;
                i < available_names.length;
                i++
            ) {
                if (available_names[i] !=
                    expected_name) {
                    continue;
                }

                if (expected_digest.length == 0 ||
                    available_digests[i] ==
                        expected_digest) {
                    return true;
                }
            }

            return false;
        }

        public void require_continuation_identity (
            string[] available_model_names,
            string[] available_model_digests,
            int64 qualified_repository_generation_id,
            ConversationPersistenceRepository[]
                qualified_repositories
        ) throws GLib.Error {
            if (!model_identity_available (
                    available_model_names,
                    available_model_digests
                )) {
                throw new GLib.IOError.NOT_FOUND (
                    "The exact AI model pinned to this conversation is not available."
                );
            }

            if (qualified_repository_generation_id !=
                repository_generation_id) {
                throw new GLib.IOError.INVALID_DATA (
                    "The qualified repository generation differs from the conversation's pinned generation."
                );
            }

            if (qualified_repositories.length !=
                repositories.length) {
                throw new GLib.IOError.INVALID_DATA (
                    "The qualified repository scope differs from the conversation's pinned scope."
                );
            }

            foreach (
                ConversationPersistenceRepository expected
                in repositories
            ) {
                uint matches = 0;

                foreach (
                    ConversationPersistenceRepository qualified
                    in qualified_repositories
                ) {
                    if (qualified.repository_id !=
                        expected.repository_id) {
                        continue;
                    }

                    if (qualified.repository_version !=
                            expected.repository_version ||
                        qualified.snapshot_sha !=
                            expected.snapshot_sha) {
                        throw new GLib.IOError.INVALID_DATA (
                            "A qualified repository pin differs from the durable conversation identity."
                        );
                    }

                    matches++;
                }

                if (matches != 1) {
                    throw new GLib.IOError.INVALID_DATA (
                        "The qualified repository set does not uniquely match the durable conversation identity."
                    );
                }
            }
        }

        public void restore_provider_history (
            OllamaConversation conversation
        ) throws GLib.Error {
            if ((messages.length % 2) != 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "Durable conversation snapshot has an incomplete provider-history turn."
                );
            }

            conversation.reset ();

            for (
                int i = 0;
                i < messages.length;
                i += 2
            ) {
                ConversationPersistenceMessage user =
                    messages[i];
                ConversationPersistenceMessage assistant =
                    messages[i + 1];

                if (user.role != "user" ||
                    assistant.role != "assistant" ||
                    user.sequence_no + 1 !=
                        assistant.sequence_no ||
                    user.turn_no !=
                        assistant.turn_no ||
                    user.grounded) {
                    conversation.reset ();
                    throw new GLib.IOError.INVALID_DATA (
                        "Durable conversation snapshot cannot rebuild provider history."
                    );
                }

                conversation.commit_exchange (
                    user.provider_content,
                    assistant.provider_content
                );
            }
        }
    }

    public class ConversationPersistenceStore : Object {
        private ConversationStoreNative.Store native_store;
        public string path { get; private set; }
        public string export_root { get; private set; }

        public ConversationPersistenceStore (
            string? state_root = null,
            string? automatic_export_root = null
        ) throws GLib.Error {
            string root = state_root ??
                GLib.Environment.get_user_state_dir ();

            path = GLib.Path.build_filename (
                root,
                "conversations.sqlite3"
            );

            if (automatic_export_root != null) {
                export_root = automatic_export_root;
            } else if (state_root != null) {
                export_root = GLib.Path.build_filename (
                    root,
                    "Conversation Exports"
                );
            } else {
                export_root = GLib.Path.build_filename (
                    GLib.Environment.get_home_dir (),
                    "Ask the Model",
                    "Conversation Exports"
                );
            }

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

            if (automatic_export_directory_ready ()) {
                sync_all_automatic_exports_best_effort ();
            }
        }

        private void sync_all_automatic_exports_best_effort () {
            try {
                foreach (
                    ConversationPersistenceSummary summary
                    in list_conversations ()
                ) {
                    sync_automatic_export_best_effort (
                        summary.conversation_id
                    );
                }
            } catch (GLib.Error error) {
                warning (
                    "AtM: existing automatic conversation exports could not be synchronized: %s",
                    error.message
                );
            }
        }

        private bool automatic_export_directory_ready () {
            if (GLib.FileUtils.test (
                    export_root,
                    GLib.FileTest.IS_SYMLINK
                )) {
                warning (
                    "AtM: automatic conversation export directory is a symbolic link: %s",
                    export_root
                );
                return false;
            }

            if (GLib.FileUtils.test (
                    export_root,
                    GLib.FileTest.IS_DIR
                )) {
                return true;
            }

            if (GLib.DirUtils.create_with_parents (
                    export_root,
                    0700
                ) != 0) {
                warning (
                    "AtM: automatic conversation export directory could not be created: %s",
                    export_root
                );
                return false;
            }

            return true;
        }

        private string automatic_export_path (
            string conversation_id
        ) {
            return GLib.Path.build_filename (
                export_root,
                "%s.json".printf (conversation_id)
            );
        }

        private void sync_automatic_export_best_effort (
            string conversation_id
        ) {
            if (!GLib.Regex.match_simple (
                    "^[A-Za-z0-9-]+$",
                    conversation_id
                )) {
                warning (
                    "AtM: automatic conversation export refused an invalid conversation id."
                );
                return;
            }

            if (!automatic_export_directory_ready ()) {
                return;
            }

            try {
                string json =
                    export_conversation_json (
                        conversation_id
                    );

                GLib.FileUtils.set_contents_full (
                    automatic_export_path (
                        conversation_id
                    ),
                    json,
                    -1,
                    GLib.FileSetContentsFlags.CONSISTENT,
                    0600
                );
            } catch (GLib.Error error) {
                warning (
                    "AtM: automatic conversation export failed for %s: %s",
                    conversation_id,
                    error.message
                );
            }
        }

        private void remove_automatic_export_required (
            string conversation_id
        ) throws GLib.Error {
            if (!GLib.Regex.match_simple (
                    "^[A-Za-z0-9-]+$",
                    conversation_id
                )) {
                throw new GLib.IOError.INVALID_DATA (
                    "Conversation identity is invalid for managed export deletion."
                );
            }

            string export_path =
                automatic_export_path (
                    conversation_id
                );

            if (!GLib.FileUtils.test (
                    export_path,
                    GLib.FileTest.EXISTS
                )) {
                return;
            }

            if (GLib.FileUtils.remove (export_path) != 0) {
                throw new GLib.IOError.FAILED (
                    "Automatic conversation export could not be deleted."
                );
            }
        }

        private static string require_native_text (
            string? value,
            string field_name
        ) throws GLib.Error {
            if (value == null) {
                throw new GLib.IOError.INVALID_DATA (
                    "Durable conversation snapshot is missing %s.".printf (
                        field_name
                    )
                );
            }

            return value;
        }

        public ConversationPersistenceSummary[]
        list_conversations () throws GLib.Error {
            ConversationStoreNative.ConversationList native_list;

            if (!ConversationStoreNative.list_conversations (
                    native_store,
                    out native_list
                )) {
                throw new GLib.IOError.FAILED (
                    "Durable conversations could not be listed."
                );
            }

            ConversationPersistenceSummary[] result = {};

            for (
                uint i = 0;
                i < ConversationStoreNative.list_count (
                    native_list
                );
                i++
            ) {
                string conversation_id =
                    require_native_text (
                        ConversationStoreNative.list_id_at (
                            native_list,
                            i
                        ),
                        "conversation id"
                    );
                string title =
                    require_native_text (
                        ConversationStoreNative.list_title_at (
                            native_list,
                            i
                        ),
                        "conversation title"
                    );

                result +=
                    new ConversationPersistenceSummary (
                        conversation_id,
                        title,
                        ConversationStoreNative.list_created_at_us_at (
                            native_list,
                            i
                        ),
                        ConversationStoreNative.list_updated_at_us_at (
                            native_list,
                            i
                        ),
                        ConversationStoreNative.list_archived_at (
                            native_list,
                            i
                        ),
                        ConversationStoreNative.list_open_on_startup_at (
                            native_list,
                            i
                        )
                    );
            }

            return result;
        }

        public ConversationPersistenceSnapshot load_snapshot (
            string conversation_id
        ) throws GLib.Error {
            ConversationStoreNative.ConversationSnapshot native_snapshot;

            if (!ConversationStoreNative.load_snapshot (
                    native_store,
                    conversation_id,
                    out native_snapshot
                )) {
                throw new GLib.IOError.FAILED (
                    "Durable conversation snapshot could not be loaded."
                );
            }

            string loaded_id =
                require_native_text (
                    ConversationStoreNative.snapshot_id (
                        native_snapshot
                    ),
                    "conversation id"
                );
            string title =
                require_native_text (
                    ConversationStoreNative.snapshot_title (
                        native_snapshot
                    ),
                    "conversation title"
                );
            string model_name =
                require_native_text (
                    ConversationStoreNative.snapshot_model_name (
                        native_snapshot
                    ),
                    "model name"
                );
            string? model_digest =
                ConversationStoreNative.snapshot_model_digest (
                    native_snapshot
                );

            ConversationPersistenceRepository[] repositories = {};

            for (
                uint i = 0;
                i < ConversationStoreNative.snapshot_repository_count (
                    native_snapshot
                );
                i++
            ) {
                repositories +=
                    new ConversationPersistenceRepository (
                        require_native_text (
                            ConversationStoreNative.snapshot_repository_id_at (
                                native_snapshot,
                                i
                            ),
                            "repository id"
                        ),
                        require_native_text (
                            ConversationStoreNative.snapshot_repository_version_at (
                                native_snapshot,
                                i
                            ),
                            "repository version"
                        ),
                        require_native_text (
                            ConversationStoreNative.snapshot_repository_sha_at (
                                native_snapshot,
                                i
                            ),
                            "repository snapshot SHA"
                        )
                    );
            }

            ConversationPersistenceMessage[] messages = {};

            for (
                uint message_index = 0;
                message_index <
                    ConversationStoreNative.snapshot_message_count (
                        native_snapshot
                    );
                message_index++
            ) {
                ConversationPersistenceCitation[] citations = {};

                for (
                    uint citation_index = 0;
                    citation_index <
                        ConversationStoreNative.snapshot_message_citation_count_at (
                            native_snapshot,
                            message_index
                        );
                    citation_index++
                ) {
                    citations +=
                        new ConversationPersistenceCitation (
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_label_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation label"
                            ),
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_repository_id_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation repository id"
                            ),
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_repository_version_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation repository version"
                            ),
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_snapshot_sha_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation snapshot SHA"
                            ),
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_logical_source_id_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation logical source id"
                            ),
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_source_path_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation source path"
                            ),
                            require_native_text (
                                ConversationStoreNative.snapshot_citation_locator_at (
                                    native_snapshot,
                                    message_index,
                                    citation_index
                                ),
                                "citation locator"
                            ),
                            ConversationStoreNative.snapshot_citation_title_at (
                                native_snapshot,
                                message_index,
                                citation_index
                            ),
                            ConversationStoreNative.snapshot_citation_excerpt_at (
                                native_snapshot,
                                message_index,
                                citation_index
                            ),
                            ConversationStoreNative.snapshot_citation_immutable_permalink_at (
                                native_snapshot,
                                message_index,
                                citation_index
                            )
                        );
                }

                messages +=
                    new ConversationPersistenceMessage (
                        ConversationStoreNative.snapshot_message_sequence_no_at (
                            native_snapshot,
                            message_index
                        ),
                        ConversationStoreNative.snapshot_message_turn_no_at (
                            native_snapshot,
                            message_index
                        ),
                        require_native_text (
                            ConversationStoreNative.snapshot_message_role_at (
                                native_snapshot,
                                message_index
                            ),
                            "message role"
                        ),
                        require_native_text (
                            ConversationStoreNative.snapshot_message_provider_content_at (
                                native_snapshot,
                                message_index
                            ),
                            "provider content"
                        ),
                        require_native_text (
                            ConversationStoreNative.snapshot_message_display_content_at (
                                native_snapshot,
                                message_index
                            ),
                            "display content"
                        ),
                        ConversationStoreNative.snapshot_message_grounded_at (
                            native_snapshot,
                            message_index
                        ),
                        ConversationStoreNative.snapshot_message_created_at_us_at (
                            native_snapshot,
                            message_index
                        ),
                        citations
                    );
            }

            string? digest_copy =
                model_digest != null
                    ? model_digest
                    : null;

            return new ConversationPersistenceSnapshot (
                loaded_id,
                title,
                ConversationStoreNative.snapshot_created_at_us (
                    native_snapshot
                ),
                ConversationStoreNative.snapshot_updated_at_us (
                    native_snapshot
                ),
                model_name,
                digest_copy,
                ConversationStoreNative.snapshot_repository_generation_id (
                    native_snapshot
                ),
                ConversationStoreNative.snapshot_archived (
                    native_snapshot
                ),
                repositories,
                messages
            );
        }

        public string export_conversation_json (
            string conversation_id
        ) throws GLib.Error {
            return ConversationExport.serialize_snapshot (
                load_snapshot (conversation_id)
            );
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

            sync_automatic_export_best_effort (
                conversation_id
            );

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
            string[] immutable_permalinks = {};

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
                immutable_permalinks +=
                    citation.immutable_permalink ?? "";
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
                    immutable_permalinks,
                    (size_t) citations.length,
                    out turn_no
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation turn could not be persisted."
                );
            }

            sync_automatic_export_best_effort (
                conversation_id
            );

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

            sync_automatic_export_best_effort (
                conversation_id
            );
        }

        public void set_archived (
            string conversation_id,
            bool archived,
            int64 updated_at_us
        ) throws GLib.Error {
            if (!ConversationStoreNative.set_archived (
                    native_store,
                    conversation_id,
                    archived,
                    updated_at_us
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation archive state could not be persisted."
                );
            }

            sync_automatic_export_best_effort (
                conversation_id
            );
        }

        public void set_open_on_startup (
            string conversation_id,
            bool open_on_startup
        ) throws GLib.Error {
            if (!ConversationStoreNative.set_open_on_startup (
                    native_store,
                    conversation_id,
                    open_on_startup
                )) {
                throw new GLib.IOError.FAILED (
                    "Conversation startup-open state could not be persisted."
                );
            }
        }

        public void delete_conversation (
            string conversation_id
        ) throws GLib.Error {
            remove_automatic_export_required (
                conversation_id
            );

            if (!ConversationStoreNative.delete_conversation (
                    native_store,
                    conversation_id
                )) {
                sync_automatic_export_best_effort (
                    conversation_id
                );

                throw new GLib.IOError.FAILED (
                    "Conversation could not be deleted."
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
