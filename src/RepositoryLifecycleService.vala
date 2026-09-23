namespace AskTheModel {
    public class RepositoryRuntimeInfo : Object {
        public RepositoryDescriptor descriptor { get; construct; }
        public RepositoryLocalRecord local { get; construct; }
        public string? remote_sha { get; set; }
        public string? remote_version { get; set; }
        public bool integrity_invalid {
            get;
            private set;
            default = false;
        }
        private string data_root;

        public RepositoryRuntimeInfo (
            RepositoryDescriptor descriptor,
            RepositoryLocalRecord local,
            string data_root
        ) {
            Object (
                descriptor: descriptor,
                local: local
            );
            this.data_root = data_root;
        }

        public void clear_remote_identity () {
            remote_sha = null;
            remote_version = null;
        }

        public void mark_integrity_invalid () {
            integrity_invalid = true;
        }

        public void clear_integrity_invalid () {
            integrity_invalid = false;
        }

        public bool download_required () {
            if (integrity_invalid || !local.is_ready ()) {
                return true;
            }

            string sha = local.current_sha ?? "";
            string path =
                RepositoryLifecycleService.snapshot_path_for_root (
                    data_root,
                    descriptor,
                    sha
                );

            return !GLib.FileUtils.test (
                path,
                GLib.FileTest.IS_DIR
            );
        }

        public bool update_available () {
            return !download_required () &&
                remote_sha != null &&
                remote_sha != local.current_sha;
        }
    }

    private class RepositoryInstallResult : Object {
        public string version { get; construct; }
        public string snapshot_path { get; construct; }
        public string index_path { get; construct; }
        public string snapshot_seal_sha256 { get; construct; }

        public RepositoryInstallResult (
            string version,
            string snapshot_path,
            string index_path,
            string snapshot_seal_sha256
        ) {
            Object (
                version: version,
                snapshot_path: snapshot_path,
                index_path: index_path,
                snapshot_seal_sha256: snapshot_seal_sha256
            );
        }
    }

    public class RepositoryLifecycleService : Object {
        private RepositoryClient client;
        private ControlRepositoryStateStore state_store;
        private RepositoryRuntimeInfo[] repositories = {};
        private string state_root;
        private string data_root;
        private string cache_root;
        private bool installation_qualification_complete = false;
        private bool installation_qualified = false;

        public signal void progress (string message);

        public RepositoryLifecycleService (
            string? state_root = null,
            string? data_root = null,
            string? cache_root = null
        ) {
            client = new RepositoryClient ();
            this.state_root =
                state_root ??
                GLib.Environment.get_user_state_dir ();
            this.data_root =
                data_root ?? visible_data_root ();
            this.cache_root =
                cache_root ??
                GLib.Environment.get_user_cache_dir ();
            state_store =
                new ControlRepositoryStateStore (
                    this.state_root
                );
            rebuild_repository_runtime ();
        }

        private void rebuild_repository_runtime () {
            repositories = {};

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                repositories += new RepositoryRuntimeInfo (
                    descriptor,
                    state_store.record_for (descriptor.id),
                    this.data_root
                );
            }
        }

        public bool reload_control_state () {
            state_store =
                new ControlRepositoryStateStore (
                    state_root
                );
            rebuild_repository_runtime ();
            installation_qualification_complete = false;
            installation_qualified = false;

            return state_store.load_status ==
                RepositoryStateLoadStatus.VALID;
        }

        public RepositoryStateLoadStatus
        repository_state_status () {
            return state_store.load_status;
        }

        public static string visible_data_root () {
            return GLib.Path.build_filename (
                GLib.Environment.get_home_dir (),
                "Ask the Model"
            );
        }

        public static string snapshot_path_for_root (
            string data_root,
            RepositoryDescriptor descriptor,
            string sha
        ) {
            return GLib.Path.build_filename (
                data_root,
                "Repositories",
                descriptor.id,
                "snapshots",
                sha
            );
        }

        public static string snapshot_path (
            RepositoryDescriptor descriptor,
            string sha
        ) {
            return snapshot_path_for_root (
                visible_data_root (),
                descriptor,
                sha
            );
        }

        public RepositoryRuntimeInfo info_for (
            string repository_id
        ) {
            foreach (
                RepositoryRuntimeInfo info
                in repositories
            ) {
                if (info.descriptor.id == repository_id) {
                    return info;
                }
            }

            assert_not_reached ();
        }

        public void apply_installation_qualification (
            bool qualified
        ) {
            installation_qualification_complete = true;
            installation_qualified =
                qualified &&
                state_store.load_status ==
                    RepositoryStateLoadStatus.VALID;
        }

        public bool repository_operations_allowed () {
            return installation_qualification_complete &&
                installation_qualified;
        }

        public void mark_integrity_invalid (
            string repository_id
        ) {
            info_for (repository_id).mark_integrity_invalid ();
        }

        public void clear_remote_identities (
            RepositoryDescriptor[] selected
        ) {
            foreach (RepositoryDescriptor descriptor in selected) {
                info_for (descriptor.id).clear_remote_identity ();
            }
        }

        public async RepositoryRuntimeInfo refresh (
            RepositoryDescriptor descriptor,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            RepositoryRuntimeInfo info =
                info_for (descriptor.id);

            // A remote identity is valid only for the most recent
            // successful refresh. Fail closed if the new check cannot
            // resolve both the SHA and repository version.
            info.clear_remote_identity ();

            string sha = yield client.resolve_branch_sha (
                descriptor,
                cancellable
            );

            string version;

            if (!info.download_required () &&
                info.local.current_sha == sha &&
                info.local.version != null) {
                version = info.local.version;
            } else {
                version =
                    yield client.resolve_remote_version (
                        descriptor,
                        sha,
                        cancellable
                    );
            }

            // Publish the remote identity as one coherent pair only after
            // both values have been resolved successfully.
            info.remote_sha = sha;
            info.remote_version = version;
            return info;
        }

        public bool selection_needs_action (
            RepositoryDescriptor[] selected
        ) {
            foreach (RepositoryDescriptor descriptor in selected) {
                RepositoryRuntimeInfo info =
                    info_for (descriptor.id);

                if (info.download_required () ||
                    info.update_available ()) {
                    return true;
                }
            }

            return false;
        }

        public string action_tooltip (
            RepositoryDescriptor[] selected
        ) {
            bool needs_download = false;
            bool needs_update = false;

            foreach (RepositoryDescriptor descriptor in selected) {
                RepositoryRuntimeInfo info =
                    info_for (descriptor.id);
                needs_download =
                    needs_download || info.download_required ();
                needs_update =
                    needs_update || info.update_available ();
            }

            if (needs_download && needs_update) {
                return "Download or update selected repositories";
            }

            if (needs_download) {
                return "Download selected repositories";
            }

            if (needs_update) {
                return "Update selected repositories";
            }

            return "Selected repositories are current";
        }

        private async RepositoryInstallResult
        prepare_snapshot (
            RepositoryDescriptor descriptor,
            string sha,
            string? archive_path,
            string? expected_seal = null
        ) throws RepositoryError {
            SourceFunc callback = prepare_snapshot.callback;
            RepositoryInstallResult? worker_result = null;
            string? failure = null;
            bool integrity_failure = false;
            string expected_snapshot =
                snapshot_path_for_root (
                    data_root,
                    descriptor,
                    sha
                );

            var worker = new GLib.Thread<void*> (
                "atm-repository-install",
                () => {
                    try {
                        string snapshot = expected_snapshot;
                        string ingest_version = "";
                        string index_path;
                        string index_version;
                        if (!GLib.FileUtils.test (
                                snapshot,
                                GLib.FileTest.IS_DIR
                            )) {
                            if (archive_path == null) {
                                throw new RepositoryError.STORAGE (
                                    "Repository archive is missing."
                                );
                            }

                            uint64 entries;
                            uint64 total_bytes;

                            if (!RepositoryNative.ingest_archive (
                                    data_root,
                                    archive_path,
                                    descriptor.id,
                                    descriptor.acronym,
                                    descriptor.display_name,
                                    sha,
                                    out ingest_version,
                                    out snapshot,
                                    out entries,
                                    out total_bytes
                                )) {
                                throw new RepositoryError.STORAGE (
                                    "Repository snapshot validation failed."
                                );
                            }
                        }

                        string pre_snapshot_seal;
                        uint64 pre_sealed_files;
                        uint64 pre_sealed_bytes;

                        if (!RepositoryNative.compute_snapshot_seal (
                                snapshot,
                                out pre_snapshot_seal,
                                out pre_sealed_files,
                                out pre_sealed_bytes
                            )) {
                            throw new RepositoryError.STORAGE (
                                "Repository snapshot integrity seal could not be computed before indexing."
                            );
                        }

                        if (expected_seal != null &&
                            expected_seal != pre_snapshot_seal) {
                            integrity_failure = true;
                            throw new RepositoryError.NOT_READY (
                                "Repository snapshot integrity seal does not match persistent state."
                            );
                        }

                        if (!RepositoryNative.ensure_index (
                                cache_root,
                                snapshot,
                                descriptor.id,
                                sha,
                                out index_path,
                                out index_version
                            )) {
                            throw new RepositoryError.STORAGE (
                                "Repository retrieval index could not be prepared."
                            );
                        }

                        string post_snapshot_seal;
                        uint64 post_sealed_files;
                        uint64 post_sealed_bytes;

                        if (!RepositoryNative.compute_snapshot_seal (
                                snapshot,
                                out post_snapshot_seal,
                                out post_sealed_files,
                                out post_sealed_bytes
                            )) {
                            throw new RepositoryError.STORAGE (
                                "Repository snapshot integrity seal could not be computed after indexing."
                            );
                        }

                        if (pre_snapshot_seal != post_snapshot_seal ||
                            pre_sealed_files != post_sealed_files ||
                            pre_sealed_bytes != post_sealed_bytes) {
                            integrity_failure = true;
                            throw new RepositoryError.NOT_READY (
                                "Repository snapshot changed while it was being prepared."
                            );
                        }

                        worker_result =
                            new RepositoryInstallResult (
                                index_version,
                                snapshot,
                                index_path,
                                post_snapshot_seal
                            );
                    } catch (GLib.Error error) {
                        failure = error.message;
                    }

                    GLib.Idle.add ((owned) callback);
                    return null;
                }
            );

            yield;
            worker.join ();

            if (failure != null) {
                if (integrity_failure) {
                    throw new RepositoryError.NOT_READY (
                        failure ??
                        "Repository snapshot integrity verification failed."
                    );
                }

                throw new RepositoryError.STORAGE (
                    failure ??
                    "Repository preparation failed."
                );
            }

            if (worker_result == null) {
                throw new RepositoryError.STORAGE (
                    "Repository preparation returned no result."
                );
            }

            return worker_result;
        }

        private async ConversationGrounding
        prepare_conversation_grounding_for_generation (
            RepositoryDescriptor[] selected,
            int64 generation_id,
            bool mark_runtime_integrity_failure,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (selected.length > 0 &&
                !repository_operations_allowed ()) {
                throw new RepositoryError.NOT_READY (
                    "Repository grounding is blocked until the installation passes startup qualification."
                );
            }

            if (selected.length == 0) {
                if (generation_id != 0) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Zero-repository conversation grounding must use repository generation 0."
                    );
                }

                var zero_grounding =
                    new ConversationGrounding ();

                if (!zero_grounding.freeze ()) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Conversation repository scope could not be frozen."
                    );
                }

                return zero_grounding;
            }

            if (generation_id <= 0) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository-backed conversation grounding requires a positive repository generation identifier."
                );
            }

            string control_state_path =
                GLib.Path.build_filename (
                    state_root,
                    "control-state.sqlite3"
                );
            var grounding = new ConversationGrounding ();

            foreach (
                RepositoryDescriptor descriptor
                in selected
            ) {
                RepositoryRuntimeInfo info =
                    info_for (descriptor.id);

                if (mark_runtime_integrity_failure &&
                    info.integrity_invalid) {
                    throw new RepositoryError.NOT_READY (
                        "Repository %s is not ready for this conversation.".printf (
                            descriptor.acronym
                        )
                    );
                }

                bool present;
                string? sha_value;
                string? version_value;
                string? seal_value;

                ControlStateNative.load_repository_values_at_generation (
                    control_state_path,
                    generation_id,
                    descriptor.id,
                    out present,
                    out sha_value,
                    out version_value,
                    out seal_value
                );

                if (!present ||
                    sha_value == null ||
                    version_value == null) {
                    throw new RepositoryError.NOT_READY (
                        "Repository %s is absent from the pinned repository generation.".printf (
                            descriptor.acronym
                        )
                    );
                }

                string sha = sha_value ?? "";
                string local_version =
                    version_value ?? "";
                string? expected_seal =
                    seal_value;
                string expected_snapshot =
                    snapshot_path_for_root (
                        data_root,
                        descriptor,
                        sha
                    );

                if (!GLib.FileUtils.test (
                        expected_snapshot,
                        GLib.FileTest.IS_DIR
                    )) {
                    throw new RepositoryError.NOT_READY (
                        "Repository %s pinned snapshot is missing.".printf (
                            descriptor.acronym
                        )
                    );
                }

                if (expected_seal == null) {
                    throw new RepositoryError.NOT_READY (
                        "Repository %s has no persistent snapshot integrity seal in the pinned Control DB generation.".printf (
                            descriptor.acronym
                        )
                    );
                }

                RepositoryInstallResult result;
                try {
                    result = yield prepare_snapshot (
                        descriptor,
                        sha,
                        null,
                        expected_seal
                    );
                } catch (RepositoryError error) {
                    if (mark_runtime_integrity_failure &&
                        error.code ==
                            RepositoryError.NOT_READY) {
                        info.mark_integrity_invalid ();
                    }
                    throw error;
                }

                if (result.version != local_version) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Repository %s pinned state version does not match the validated snapshot.".printf (
                            descriptor.acronym
                        )
                    );
                }

                if (expected_seal !=
                    result.snapshot_seal_sha256) {
                    if (mark_runtime_integrity_failure) {
                        info.mark_integrity_invalid ();
                    }

                    throw new RepositoryError.NOT_READY (
                        "Repository %s local snapshot integrity seal does not match the pinned Control DB generation.".printf (
                            descriptor.acronym
                        )
                    );
                }

                if (!grounding.add_ready_repository (
                        descriptor.id,
                        result.version,
                        sha,
                        result.snapshot_path,
                        result.index_path
                    )) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Repository %s could not be pinned for the conversation.".printf (
                            descriptor.acronym
                        )
                    );
                }
            }

            grounding.pin_repository_generation (
                generation_id
            );

            if (!grounding.freeze ()) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Conversation repository scope could not be frozen."
                );
            }

            return grounding;
        }

        public async ConversationGrounding
        prepare_conversation_grounding (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (selected.length == 0) {
                return yield
                    prepare_conversation_grounding_for_generation (
                        selected,
                        0,
                        false,
                        cancellable
                    );
            }

            if (!repository_operations_allowed ()) {
                throw new RepositoryError.NOT_READY (
                    "Repository grounding is blocked until the installation passes startup qualification."
                );
            }

            var pinned_state_store =
                new ControlRepositoryStateStore (
                    state_root
                );

            if (pinned_state_store.load_status !=
                    RepositoryStateLoadStatus.VALID ||
                pinned_state_store.repository_generation_id <= 0) {
                throw new RepositoryError.NOT_READY (
                    "Repository grounding requires one valid active Control DB generation."
                );
            }

            return yield
                prepare_conversation_grounding_for_generation (
                    selected,
                    pinned_state_store.repository_generation_id,
                    true,
                    cancellable
                );
        }

        public async ConversationGrounding
        prepare_conversation_grounding_at_generation (
            RepositoryDescriptor[] selected,
            int64 generation_id,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            return yield
                prepare_conversation_grounding_for_generation (
                    selected,
                    generation_id,
                    false,
                    cancellable
                );
        }

        public async uint download_or_update (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (!repository_operations_allowed ()) {
                throw new RepositoryError.NOT_READY (
                    "Repository download/update is blocked until the installation passes startup qualification."
                );
            }

            uint changed = 0;

            foreach (RepositoryDescriptor descriptor in selected) {
                RepositoryRuntimeInfo info =
                    info_for (descriptor.id);
                bool integrity_repair =
                    info.integrity_invalid;
                bool updating_existing =
                    !info.download_required ();

                if (info.remote_sha == null ||
                    info.remote_version == null) {
                    info = yield refresh (
                        descriptor,
                        cancellable
                    );
                }

                if (info.remote_sha == null ||
                    info.remote_version == null) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Remote repository identity is incomplete."
                    );
                }

                if (!info.download_required () &&
                    info.local.current_sha ==
                    info.remote_sha) {
                    continue;
                }

                string sha = info.remote_sha ?? "";
                string expected_snapshot =
                    snapshot_path_for_root (
                        data_root,
                        descriptor,
                        sha
                    );
                string? archive_path = null;
                bool repairing_same_snapshot =
                    integrity_repair &&
                    info.local.current_sha == sha &&
                    GLib.FileUtils.test (
                        expected_snapshot,
                        GLib.FileTest.EXISTS
                    );

                try {
                    if (repairing_same_snapshot ||
                        !GLib.FileUtils.test (
                            expected_snapshot,
                            GLib.FileTest.IS_DIR
                        )) {
                        progress (
                            updating_existing
                                ? "Updating %s…".printf (
                                    descriptor.acronym
                                )
                                : "Downloading %s…".printf (
                                    descriptor.acronym
                                )
                        );
                        archive_path =
                            yield client.download_archive_to_staging (
                                descriptor,
                                sha,
                                cancellable
                            );
                    }

                    if (repairing_same_snapshot) {
                        progress (
                            "Repairing %s…".printf (
                                descriptor.acronym
                            )
                        );

                        string quarantine_path;
                        if (!RepositoryNative.quarantine_snapshot (
                                data_root,
                                descriptor.id,
                                sha,
                                out quarantine_path
                            )) {
                            throw new RepositoryError.STORAGE (
                                "Invalid repository snapshot could not be quarantined for repair."
                            );
                        }

                        stdout.printf (
                            "AtM: quarantined invalid repository %s snapshot=%s path=%s\n",
                            descriptor.acronym,
                            sha,
                            quarantine_path
                        );
                    }

                    progress (
                        "Validating %s…".printf (
                            descriptor.acronym
                        )
                    );

                    RepositoryInstallResult result =
                        yield prepare_snapshot (
                            descriptor,
                            sha,
                            archive_path,
                            null
                        );

                    if (result.version != info.remote_version) {
                        throw new RepositoryError.INVALID_RESPONSE (
                            "Validated repository version does not match the exact-SHA remote metadata."
                        );
                    }

                    state_store.set_current (
                        descriptor.id,
                        sha,
                        result.version,
                        result.snapshot_seal_sha256
                    );
                    info.clear_integrity_invalid ();

                    info.remote_sha = sha;
                    info.remote_version = result.version;
                    changed++;

                    stdout.printf (
                        "AtM: repository %s ready version=%s sha=%s snapshot=%s index=%s seal=%s\n",
                        descriptor.acronym,
                        result.version,
                        sha,
                        result.snapshot_path,
                        result.index_path,
                        result.snapshot_seal_sha256
                    );
                } finally {
                    if (archive_path != null) {
                        GLib.FileUtils.remove (archive_path);
                    }
                }
            }

            return changed;
        }
    }
}
