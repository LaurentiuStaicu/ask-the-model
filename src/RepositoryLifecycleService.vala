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

    public class RepositoryMutationOutcome : Object {
        public uint changed { get; construct; }
        public bool optimized_operation { get; construct; }

        public RepositoryMutationOutcome (
            uint changed,
            bool optimized_operation
        ) {
            Object (
                changed: changed,
                optimized_operation: optimized_operation
            );
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
        private OptimizationPolicy optimization_policy =
            new OptimizationPolicy ();
#if ATM_M12_TEST
        private string? archive_download_override_for_test = null;
#endif
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

        public void set_optimization_policy (
            OptimizationPolicy policy
        ) {
            optimization_policy = policy;
        }

        public bool optimization_mode_snapshot () {
            return optimization_policy.snapshot_enabled ();
        }

#if ATM_M12_TEST
        internal void set_archive_download_override_for_test (
            string? archive_path
        ) {
            archive_download_override_for_test = archive_path;
        }
#endif

        private void require_download_capacity (
            RepositoryDescriptor descriptor,
            string sha
        ) throws RepositoryError {
            string partial_path =
                RepositoryClient.staging_archive_path (
                    descriptor,
                    sha.down (),
                    true
                );
            string? staging_directory =
                GLib.Path.get_dirname (
                    partial_path
                );

            if (staging_directory == null ||
                GLib.DirUtils.create_with_parents (
                    staging_directory,
                    0700
                ) != 0) {
                throw new RepositoryError.STORAGE (
                    "Repository staging directory could not be prepared for local-capacity admission."
                );
            }

            try {
                bool admitted;
                bool byte_prediction_qualified;
                string detail;

                if (!RepositoryNative.capacity_download_preflight (
                        staging_directory,
                        descriptor.id,
                        sha,
                        out admitted,
                        out byte_prediction_qualified,
                        out detail
                    )) {
                    throw new RepositoryError.STORAGE (
                        "Pre-download local-capacity admission returned no decision."
                    );
                }

                stdout.printf (
                    "AtM: capacity A repository=%s sha=%s byte_profile=%s admitted=%s\n",
                    descriptor.id,
                    sha,
                    byte_prediction_qualified ? "exact" : "unqualified",
                    admitted ? "yes" : "no"
                );

                if (!admitted) {
                    throw new RepositoryError.NO_SPACE (
                        "%s: %s".printf (
                            descriptor.acronym,
                            detail
                        )
                    );
                }
            } catch (RepositoryError error) {
                throw error;
            } catch (GLib.Error error) {
                throw new RepositoryError.STORAGE (
                    "Pre-download local-capacity admission could not be evaluated: %s".printf (
                        error.message
                    )
                );
            }
        }

        internal static RepositoryNative.CapacityOperationKind
        capacity_operation_kind (
            RepositoryRuntimeInfo info,
            bool repairing_same_snapshot
        ) {
            if (repairing_same_snapshot) {
                return RepositoryNative.CapacityOperationKind.
                    SAME_SHA_REPAIR;
            }

            if (!info.local.is_ready ()) {
                return RepositoryNative.CapacityOperationKind.
                    FRESH_INSTALL;
            }

            return RepositoryNative.CapacityOperationKind.
                DIFFERENT_SHA_UPDATE;
        }

        internal static bool
        reject_preexisting_final_target (
            bool optimized_operation,
            bool repairing_same_snapshot,
            bool target_exists
        ) {
            return optimized_operation &&
                target_exists &&
                !repairing_same_snapshot;
        }

        private void require_mutation_capacity (
            RepositoryDescriptor descriptor,
            string sha,
            string archive_path,
            RepositoryNative.CapacityOperationKind operation_kind
        ) throws RepositoryError {
            try {
                bool admitted;
                bool byte_prediction_qualified;
                bool must_admit_before_quarantine;
                string detail;

                if (!RepositoryNative.capacity_mutation_preflight (
                        data_root,
                        cache_root,
                        state_root,
                        archive_path,
                        descriptor.id,
                        sha,
                        operation_kind,
                        out admitted,
                        out byte_prediction_qualified,
                        out must_admit_before_quarantine,
                        out detail
                    )) {
                    throw new RepositoryError.STORAGE (
                        "Post-download local-capacity admission returned no decision."
                    );
                }

                bool expected_pre_quarantine =
                    operation_kind ==
                    RepositoryNative.CapacityOperationKind.
                        SAME_SHA_REPAIR;

                if (must_admit_before_quarantine !=
                    expected_pre_quarantine) {
                    throw new RepositoryError.STORAGE (
                        "Post-download local-capacity admission returned inconsistent quarantine semantics."
                    );
                }

                stdout.printf (
                    "AtM: capacity B repository=%s sha=%s byte_profile=%s admitted=%s pre_quarantine=%s\n",
                    descriptor.id,
                    sha,
                    byte_prediction_qualified ? "exact" : "unqualified",
                    admitted ? "yes" : "no",
                    must_admit_before_quarantine ? "yes" : "no"
                );

                if (!admitted) {
                    throw new RepositoryError.NO_SPACE (
                        "%s: %s".printf (
                            descriptor.acronym,
                            detail
                        )
                    );
                }
            } catch (RepositoryError error) {
                throw error;
            } catch (GLib.Error error) {
                throw new RepositoryError.STORAGE (
                    "Post-download local-capacity admission could not be evaluated: %s".printf (
                        error.message
                    )
                );
            }
        }

        private void require_state_commit_capacity (
            RepositoryDescriptor descriptor
        ) throws RepositoryError {
            try {
                bool admitted;
                string detail;

                if (!RepositoryNative.capacity_state_commit_preflight (
                        state_root,
                        out admitted,
                        out detail
                    )) {
                    throw new RepositoryError.STORAGE (
                        "State-publication local-capacity admission returned no decision."
                    );
                }

                stdout.printf (
                    "AtM: capacity state repository=%s admitted=%s\n",
                    descriptor.id,
                    admitted ? "yes" : "no"
                );

                if (!admitted) {
                    throw new RepositoryError.NO_SPACE (
                        "%s: %s".printf (
                            descriptor.acronym,
                            detail
                        )
                    );
                }
            } catch (RepositoryError error) {
                throw error;
            } catch (GLib.Error error) {
                throw new RepositoryError.STORAGE (
                    "State-publication local-capacity admission could not be evaluated: %s".printf (
                        error.message
                    )
                );
            }
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
            string? expected_seal,
            bool durable_ingest,
            bool coordinated_index
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
                        string? durable_pre_barrier_seal = null;
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

                            if (durable_ingest) {
                                string barrier_seal;

                                if (!RepositoryNative.ingest_archive_durable (
                                        data_root,
                                        archive_path,
                                        descriptor.id,
                                        descriptor.acronym,
                                        descriptor.display_name,
                                        sha,
                                        out barrier_seal,
                                        out ingest_version,
                                        out snapshot,
                                        out entries,
                                        out total_bytes
                                    )) {
                                    throw new RepositoryError.STORAGE (
                                        "Durable repository snapshot validation failed."
                                    );
                                }

                                durable_pre_barrier_seal =
                                    barrier_seal;
                            } else if (!RepositoryNative.ingest_archive (
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

                        if (durable_pre_barrier_seal != null &&
                            durable_pre_barrier_seal !=
                                pre_snapshot_seal) {
                            integrity_failure = true;
                            throw new RepositoryError.NOT_READY (
                                "Durable repository pre-barrier seal does not match the promoted snapshot."
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
                                state_root,
                                coordinated_index,
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

            bool optimized_operation =
                optimization_mode_snapshot ();

            string control_state_path =
                GLib.Path.build_filename (
                    state_root,
                    "control-state.sqlite3"
                );
            var grounding = new ConversationGrounding ();

            try {
                RepositoryGenerationLease lease =
                    RepositoryGenerationLease.
                        acquire_shared (
                            state_root,
                            generation_id
                        );

                grounding.hold_generation_lease (
                    lease
                );

                stdout.printf (
                    "AtM: generation lease shared generation=%" + int64.FORMAT + "\n",
                    generation_id
                );
            } catch (
                RepositoryGenerationLeaseError error
            ) {
                if (error is
                    RepositoryGenerationLeaseError.BUSY) {
                    throw new RepositoryError.BUSY (
                        error.message
                    );
                }

                throw new RepositoryError.STORAGE (
                    error.message
                );
            }

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
                        expected_seal,
                        false,
                        optimized_operation
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

        public async RepositoryMutationOutcome
        download_or_update_with_context (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            bool optimized_operation =
                optimization_mode_snapshot ();

            uint changed =
                yield download_or_update_for_operation (
                    selected,
                    optimized_operation,
                    cancellable
                );

            return new RepositoryMutationOutcome (
                changed,
                optimized_operation
            );
        }

        public async uint download_or_update (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            RepositoryMutationOutcome outcome =
                yield download_or_update_with_context (
                    selected,
                    cancellable
                );

            return outcome.changed;
        }

        public RepositoryGcIsolationResult?
        run_post_mutation_isolation (
            RepositoryMutationOutcome operation,
            ConversationPersistenceStore conversation_store,
            out string? failure
        ) {
            failure = null;

            if (!operation.optimized_operation ||
                operation.changed == 0) {
                return null;
            }

            string control_state_path =
                GLib.Path.build_filename (
                    state_root,
                    "control-state.sqlite3"
                );

            try {
                return RepositoryGcIsolationOrchestrator.
                    isolate_one (
                        true,
                        data_root,
                        state_root,
                        control_state_path,
                        conversation_store
                    );
            } catch (GLib.Error error) {
                failure = error.message;
                return null;
            }
        }


        private async uint download_or_update_for_operation (
            RepositoryDescriptor[] selected,
            bool optimized_operation,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (!repository_operations_allowed ()) {
                throw new RepositoryError.NOT_READY (
                    "Repository download/update is blocked until the installation passes startup qualification."
                );
            }

            int mutation_lease_fd = -1;

            bool mutation_contended = false;
            bool mutation_acquired = false;

            try {
                mutation_acquired =
                    RepositoryNative.try_acquire_mutation_lease (
                        state_root,
                        out mutation_lease_fd,
                        out mutation_contended
                    );
            } catch (GLib.Error error) {
                throw new RepositoryError.STORAGE (
                    "Repository mutation coordination could not be established."
                );
            }

            if (!mutation_acquired) {
                throw new RepositoryError.STORAGE (
                    "Repository mutation coordination could not be established."
                );
            }

            if (mutation_contended) {
                throw new RepositoryError.BUSY (
                    "Another Ask the Model instance is currently updating repository state. Try again after that operation finishes."
                );
            }

            if (mutation_lease_fd < 0) {
                throw new RepositoryError.STORAGE (
                    "Repository mutation coordination returned no lease."
                );
            }

            try {
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
                    bool final_target_exists =
                        GLib.FileUtils.test (
                            expected_snapshot,
                            GLib.FileTest.EXISTS
                        );
                    bool repairing_same_snapshot =
                        integrity_repair &&
                        info.local.current_sha == sha &&
                        final_target_exists;

                    if (reject_preexisting_final_target (
                            optimized_operation,
                            repairing_same_snapshot,
                            final_target_exists
                        )) {
                        throw new RepositoryError.STORAGE (
                            "Optimizations ON refuses an unqualified pre-existing final repository snapshot target."
                        );
                    }

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
                            if (optimized_operation) {
                                require_download_capacity (
                                    descriptor,
                                    sha
                                );
                            }

#if ATM_M12_TEST
                            if (archive_download_override_for_test !=
                                null) {
                                archive_path =
                                    archive_download_override_for_test;
                            } else {
#endif
                                archive_path =
                                    yield client.download_archive_to_staging (
                                        descriptor,
                                        sha,
                                        cancellable
                                    );
#if ATM_M12_TEST
                            }
#endif
                        }

                        if (optimized_operation &&
                            archive_path != null) {
                            RepositoryNative.CapacityOperationKind
                                operation_kind =
                                capacity_operation_kind (
                                    info,
                                    repairing_same_snapshot
                                );

                            require_mutation_capacity (
                                descriptor,
                                sha,
                                archive_path,
                                operation_kind
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
                                null,
                                optimized_operation,
                                optimized_operation
                            );

                        if (result.version != info.remote_version) {
                            throw new RepositoryError.INVALID_RESPONSE (
                                "Validated repository version does not match the exact-SHA remote metadata."
                            );
                        }

                        if (optimized_operation) {
                            require_state_commit_capacity (
                                descriptor
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
            } finally {
                if (mutation_lease_fd >= 0) {
                    RepositoryNative.release_mutation_lease (
                        mutation_lease_fd
                    );
                }
            }
        }

    }
}
