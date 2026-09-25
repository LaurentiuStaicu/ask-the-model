namespace AskTheModel {
    public class RepositoryGcSnapshotRoot : Object {
        public string repository_id { get; construct; }
        public string snapshot_sha { get; construct; }

        public RepositoryGcSnapshotRoot (
            string repository_id,
            string snapshot_sha
        ) {
            Object (
                repository_id: repository_id,
                snapshot_sha: snapshot_sha
            );
        }
    }

    public class RepositoryGcDurableRoots : Object {
        private int64[] generation_ids_store = {};
        private RepositoryGcSnapshotRoot[] snapshot_roots_store = {};

        internal void add_generation (int64 generation_id) {
            if (generation_id <= 0 ||
                protects_generation (generation_id)) {
                return;
            }

            generation_ids_store += generation_id;
        }

        internal void add_snapshot (
            string repository_id,
            string snapshot_sha
        ) {
            if (protects_snapshot (
                    repository_id,
                    snapshot_sha
                )) {
                return;
            }

            snapshot_roots_store +=
                new RepositoryGcSnapshotRoot (
                    repository_id,
                    snapshot_sha
                );
        }

        public uint generation_count () {
            return (uint) generation_ids_store.length;
        }

        public int64 generation_at (uint index) {
            if (index >= generation_ids_store.length) {
                return 0;
            }

            return generation_ids_store[index];
        }

        public bool protects_generation (
            int64 generation_id
        ) {
            foreach (
                int64 protected_generation
                in generation_ids_store
            ) {
                if (protected_generation == generation_id) {
                    return true;
                }
            }

            return false;
        }

        public uint snapshot_count () {
            return (uint) snapshot_roots_store.length;
        }

        public RepositoryGcSnapshotRoot? snapshot_at (
            uint index
        ) {
            if (index >= snapshot_roots_store.length) {
                return null;
            }

            return snapshot_roots_store[index];
        }

        public bool protects_snapshot (
            string repository_id,
            string snapshot_sha
        ) {
            foreach (
                RepositoryGcSnapshotRoot root
                in snapshot_roots_store
            ) {
                if (root.repository_id == repository_id &&
                    root.snapshot_sha == snapshot_sha) {
                    return true;
                }
            }

            return false;
        }
    }

    public class RepositoryGcDurableRootCollector : Object {
        private static bool sha40_is_valid (
            string value
        ) {
            return GLib.Regex.match_simple (
                "^[0-9a-f]{40}$",
                value
            );
        }

        private static bool repository_id_is_known (
            string repository_id
        ) {
            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                if (descriptor.id == repository_id) {
                    return true;
                }
            }

            return false;
        }

        private static void
        require_conversation_pin_matches_generation (
            string control_state_path,
            ConversationPersistenceSnapshot snapshot
        ) throws GLib.Error {
            if (snapshot.repository_generation_id == 0) {
                if (snapshot.repositories.length != 0) {
                    throw new GLib.IOError.INVALID_DATA (
                        "A durable conversation with repository pins has no positive repository generation."
                    );
                }

                return;
            }

            if (snapshot.repository_generation_id < 0 ||
                snapshot.repositories.length == 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "A durable conversation contains an invalid repository generation identity."
                );
            }

            foreach (
                ConversationPersistenceRepository repository
                in snapshot.repositories
            ) {
                if (!repository_id_is_known (
                        repository.repository_id
                    ) ||
                    !sha40_is_valid (
                        repository.snapshot_sha
                    ) ||
                    repository.repository_version.length == 0) {
                    throw new GLib.IOError.INVALID_DATA (
                        "A durable conversation contains an invalid repository pin."
                    );
                }

                bool present;
                string? authority_sha;
                string? authority_version;
                string? authority_seal;

                ControlStateNative.
                    load_repository_values_at_generation_readonly (
                        control_state_path,
                        snapshot.repository_generation_id,
                        repository.repository_id,
                        out present,
                        out authority_sha,
                        out authority_version,
                        out authority_seal
                    );

                if (!present ||
                    authority_sha == null ||
                    authority_version == null ||
                    authority_sha != repository.snapshot_sha ||
                    authority_version !=
                        repository.repository_version) {
                    throw new GLib.IOError.INVALID_DATA (
                        "A durable conversation repository pin does not match its Control DB generation."
                    );
                }
            }
        }

        private static void
        add_generation_snapshots (
            RepositoryGcDurableRoots roots,
            string control_state_path,
            int64 generation_id
        ) throws GLib.Error {
            if (generation_id <= 0) {
                return;
            }

            uint present_count = 0;

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                bool present;
                string? snapshot_sha;
                string? repository_version;
                string? snapshot_seal;

                ControlStateNative.
                    load_repository_values_at_generation_readonly (
                        control_state_path,
                        generation_id,
                        descriptor.id,
                        out present,
                        out snapshot_sha,
                        out repository_version,
                        out snapshot_seal
                    );

                if (!present) {
                    continue;
                }

                present_count++;

                if (snapshot_sha == null ||
                    repository_version == null ||
                    !sha40_is_valid (snapshot_sha) ||
                    repository_version.length == 0) {
                    throw new GLib.IOError.INVALID_DATA (
                        "A protected Control DB generation contains invalid repository identity."
                    );
                }

                roots.add_snapshot (
                    descriptor.id,
                    snapshot_sha
                );
            }

            if (present_count == 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "A positive protected repository generation contains no catalog repository state."
                );
            }
        }

        public static RepositoryGcDurableRoots collect (
            string control_state_path,
            ConversationPersistenceStore conversation_store
        ) throws GLib.Error {
            if (control_state_path.length == 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC durable-root collection requires a Control DB path."
                );
            }

            var roots = new RepositoryGcDurableRoots ();

            int64 active_generation;
            if (!ControlStateNative.active_generation_id_readonly (
                    control_state_path,
                    out active_generation
                )) {
                throw new GLib.IOError.FAILED (
                    "GC durable-root collection could not read the active Control DB generation."
                );
            }

            if (active_generation < 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "GC durable-root collection observed an invalid active generation."
                );
            }

            roots.add_generation (
                active_generation
            );

            ConversationPersistenceSnapshot[] snapshots = {};

            foreach (
                ConversationPersistenceSummary summary
                in conversation_store.list_conversations ()
            ) {
                ConversationPersistenceSnapshot snapshot =
                    conversation_store.load_snapshot (
                        summary.conversation_id
                    );

                require_conversation_pin_matches_generation (
                    control_state_path,
                    snapshot
                );

                snapshots += snapshot;
                roots.add_generation (
                    snapshot.repository_generation_id
                );
            }

            for (
                uint i = 0;
                i < roots.generation_count ();
                i++
            ) {
                add_generation_snapshots (
                    roots,
                    control_state_path,
                    roots.generation_at (i)
                );
            }

            foreach (
                ConversationPersistenceSnapshot snapshot
                in snapshots
            ) {
                foreach (
                    ConversationPersistenceRepository repository
                    in snapshot.repositories
                ) {
                    if (!roots.protects_snapshot (
                            repository.repository_id,
                            repository.snapshot_sha
                        )) {
                        throw new GLib.IOError.INVALID_DATA (
                            "A durable conversation pin was not retained by GC root collection."
                        );
                    }
                }
            }

            return roots;
        }
    }
}
