namespace AskTheModel {
    public enum RepositoryLocalQualificationStatus {
        PENDING,
        BLOCKED,
        STATE_INVALID,
        NOT_INSTALLED,
        SNAPSHOT_MISSING,
        SNAPSHOT_INVALID,
        INDEX_ERROR,
        READY,
        READY_REPAIRED_INDEX
    }

    public class RepositoryRuntimeInfo : Object {
        public RepositoryDescriptor descriptor { get; construct; }
        public RepositoryLocalRecord local { get; construct; }
        public string? remote_sha { get; set; }
        public string? remote_version { get; set; }

        public RepositoryLocalQualificationStatus local_qualification {
            get;
            private set;
            default = RepositoryLocalQualificationStatus.PENDING;
        }
        public string qualification_reason_code {
            get;
            private set;
            default = "startup_pending";
        }
        public string qualification_detail {
            get;
            private set;
            default = "";
        }

        private string data_root;
        private string? qualified_sha;
        private string? qualified_version;

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

        public void apply_local_qualification (
            RepositoryLocalQualificationStatus status,
            string? snapshot_sha,
            string? persisted_version,
            string? validated_version,
            string reason_code,
            string detail
        ) {
            local_qualification = status;
            qualification_reason_code = reason_code;
            qualification_detail = detail;
            qualified_sha = null;
            qualified_version = null;

            if (status != RepositoryLocalQualificationStatus.READY &&
                status != RepositoryLocalQualificationStatus.READY_REPAIRED_INDEX) {
                return;
            }

            bool exact_identity =
                local.is_ready () &&
                snapshot_sha != null &&
                persisted_version != null &&
                validated_version != null &&
                local.current_sha == snapshot_sha &&
                local.version == persisted_version &&
                validated_version == persisted_version;

            if (!exact_identity) {
                local_qualification =
                    RepositoryLocalQualificationStatus.SNAPSHOT_INVALID;
                qualification_reason_code =
                    "runtime_qualification_identity_mismatch";
                qualification_detail =
                    "Startup qualification does not match the current persistent repository identity.";
                return;
            }

            qualified_sha = snapshot_sha;
            qualified_version = validated_version;
        }

        public void mark_local_ready (
            string sha,
            string version
        ) {
            local_qualification =
                RepositoryLocalQualificationStatus.READY;
            qualification_reason_code = "ready";
            qualification_detail =
                "Repository was validated during the explicit install/update operation.";
            qualified_sha = sha;
            qualified_version = version;
        }

        public bool locally_usable () {
            if (local_qualification !=
                    RepositoryLocalQualificationStatus.READY &&
                local_qualification !=
                    RepositoryLocalQualificationStatus.READY_REPAIRED_INDEX) {
                return false;
            }

            if (!local.is_ready () ||
                qualified_sha == null ||
                qualified_version == null ||
                local.current_sha != qualified_sha ||
                local.version != qualified_version) {
                return false;
            }

            string path =
                RepositoryLifecycleService.snapshot_path_for (
                    data_root,
                    descriptor,
                    qualified_sha ?? ""
                );

            return GLib.FileUtils.test (
                path,
                GLib.FileTest.IS_DIR
            );
        }

        public bool download_required () {
            return !locally_usable ();
        }

        public bool update_available () {
            return locally_usable () &&
                remote_sha != null &&
                remote_sha != local.current_sha;
        }
    }

    private class RepositoryInstallResult : Object {
        public string version { get; construct; }
        public string snapshot_path { get; construct; }
        public string index_path { get; construct; }

        public RepositoryInstallResult (
            string version,
            string snapshot_path,
            string index_path
        ) {
            Object (
                version: version,
                snapshot_path: snapshot_path,
                index_path: index_path
            );
        }
    }

    public class RepositoryLifecycleService : Object {
        private RepositoryClient client;
        private RepositoryStateStore state_store;
        private RepositoryRuntimeInfo[] repositories = {};
        private string data_root;
        private bool installation_qualification_complete = false;
        private bool installation_qualified = false;

        public signal void progress (string message);

        public RepositoryLifecycleService (
            string? state_root = null,
            string? data_root = null
        ) {
            client = new RepositoryClient ();
            this.data_root =
                data_root ?? visible_data_root ();
            state_store = new RepositoryStateStore (
                state_root
            );

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

        public static string visible_data_root () {
            return GLib.Path.build_filename (
                GLib.Environment.get_home_dir (),
                "Ask the Model"
            );
        }

        public static string snapshot_path_for (
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
            return snapshot_path_for (
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
            installation_qualified = qualified;
        }

        public bool repository_mutations_allowed () {
            return installation_qualification_complete &&
                installation_qualified;
        }

        public void apply_local_qualification (
            string repository_id,
            RepositoryLocalQualificationStatus status,
            string? snapshot_sha,
            string? persisted_version,
            string? validated_version,
            string reason_code,
            string detail
        ) {
            info_for (repository_id).apply_local_qualification (
                status,
                snapshot_sha,
                persisted_version,
                validated_version,
                reason_code,
                detail
            );
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
            string? archive_path
        ) throws RepositoryError {
            SourceFunc callback = prepare_snapshot.callback;
            RepositoryInstallResult? worker_result = null;
            string? failure = null;
            string cache_root =
                GLib.Environment.get_user_cache_dir ();
            string expected_snapshot =
                snapshot_path_for (
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

                        worker_result =
                            new RepositoryInstallResult (
                                index_version,
                                snapshot,
                                index_path
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

        public async ConversationGrounding
        prepare_conversation_grounding (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            var grounding = new ConversationGrounding ();

            foreach (RepositoryDescriptor descriptor in selected) {
                RepositoryRuntimeInfo info =
                    info_for (descriptor.id);

                if (!info.locally_usable () ||
                    info.local.current_sha == null ||
                    info.local.version == null) {
                    throw new RepositoryError.NOT_READY (
                        "Repository %s is not ready for this conversation.".printf (
                            descriptor.acronym
                        )
                    );
                }

                string sha = info.local.current_sha ?? "";
                string local_version =
                    info.local.version ?? "";

                RepositoryInstallResult result =
                    yield prepare_snapshot (
                        descriptor,
                        sha,
                        null
                    );

                if (result.version != local_version) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Repository %s local state version does not match the validated snapshot.".printf (
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

            if (!grounding.freeze ()) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Conversation repository scope could not be frozen."
                );
            }

            return grounding;
        }

        public async uint download_or_update (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (!repository_mutations_allowed ()) {
                throw new RepositoryError.NOT_READY (
                    "Repository download/update is blocked until the installation passes startup qualification."
                );
            }

            uint changed = 0;

            foreach (RepositoryDescriptor descriptor in selected) {
                RepositoryRuntimeInfo info =
                    info_for (descriptor.id);
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
                    snapshot_path_for (
                        data_root,
                        descriptor,
                        sha
                    );
                string? archive_path = null;

                try {
                    if (!GLib.FileUtils.test (
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

                    progress (
                        "Validating %s…".printf (
                            descriptor.acronym
                        )
                    );

                    RepositoryInstallResult result =
                        yield prepare_snapshot (
                            descriptor,
                            sha,
                            archive_path
                        );

                    if (result.version != info.remote_version) {
                        throw new RepositoryError.INVALID_RESPONSE (
                            "Validated repository version does not match the exact-SHA remote metadata."
                        );
                    }

                    state_store.set_current (
                        descriptor.id,
                        sha,
                        result.version
                    );

                    info.remote_sha = sha;
                    info.remote_version = result.version;
                    info.mark_local_ready (
                        sha,
                        result.version
                    );
                    changed++;

                    stdout.printf (
                        "AtM: repository %s ready version=%s sha=%s snapshot=%s index=%s\n",
                        descriptor.acronym,
                        result.version,
                        sha,
                        result.snapshot_path,
                        result.index_path
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
