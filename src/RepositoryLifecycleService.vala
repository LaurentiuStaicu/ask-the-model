namespace AskTheModel {
    public class RepositoryRuntimeInfo : Object {
        public RepositoryDescriptor descriptor { get; construct; }
        public RepositoryLocalRecord local { get; construct; }
        public string? remote_sha { get; set; }
        public string? remote_version { get; set; }

        public RepositoryRuntimeInfo (
            RepositoryDescriptor descriptor,
            RepositoryLocalRecord local
        ) {
            Object (
                descriptor: descriptor,
                local: local
            );
        }

        public bool update_available () {
            return local.is_ready () &&
                remote_sha != null &&
                remote_sha != local.current_sha;
        }

        public bool download_required () {
            return !local.is_ready ();
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

        public signal void progress (string message);

        public RepositoryLifecycleService (
            string? state_root = null
        ) {
            client = new RepositoryClient ();
            state_store = new RepositoryStateStore (
                state_root
            );

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                repositories += new RepositoryRuntimeInfo (
                    descriptor,
                    state_store.record_for (descriptor.id)
                );
            }
        }

        public static string visible_data_root () {
            return GLib.Path.build_filename (
                GLib.Environment.get_home_dir (),
                "Ask the Model"
            );
        }

        public static string snapshot_path (
            RepositoryDescriptor descriptor,
            string sha
        ) {
            return GLib.Path.build_filename (
                visible_data_root (),
                "Repositories",
                descriptor.id,
                "snapshots",
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

        public async RepositoryRuntimeInfo refresh (
            RepositoryDescriptor descriptor,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            RepositoryRuntimeInfo info =
                info_for (descriptor.id);

            string sha = yield client.resolve_branch_sha (
                descriptor,
                cancellable
            );
            string version =
                yield client.resolve_remote_version (
                    descriptor,
                    sha,
                    cancellable
                );

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
            string data_root = visible_data_root ();
            string cache_root =
                GLib.Environment.get_user_cache_dir ();
            string expected_snapshot =
                snapshot_path (descriptor, sha);

            var worker = new GLib.Thread<void*> (
                "atm-repository-install",
                () => {
                    try {
                        string snapshot = expected_snapshot;
                        string ingest_version = "";
                        string index_path;
                        string index_version;
                        RepositoryNative.RetrievalEnsureResult
                            ensure_result;

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
                                out index_version,
                                out ensure_result
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
                    failure
                );
            }

            if (worker_result == null) {
                throw new RepositoryError.STORAGE (
                    "Repository preparation returned no result."
                );
            }

            return worker_result;
        }

        public async uint download_or_update (
            RepositoryDescriptor[] selected,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            uint changed = 0;

            foreach (RepositoryDescriptor descriptor in selected) {
                RepositoryRuntimeInfo info =
                    yield refresh (
                        descriptor,
                        cancellable
                    );

                if (info.remote_sha == null ||
                    info.remote_version == null) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "Remote repository identity is incomplete."
                    );
                }

                if (info.local.current_sha ==
                    info.remote_sha) {
                    continue;
                }

                string sha = info.remote_sha;
                string expected_snapshot =
                    snapshot_path (descriptor, sha);
                string? archive_path = null;

                if (!GLib.FileUtils.test (
                        expected_snapshot,
                        GLib.FileTest.IS_DIR
                    )) {
                    progress (
                        "Downloading %s…".printf (
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

                state_store.set_current (
                    descriptor.id,
                    sha,
                    result.version
                );

                info.remote_sha = sha;
                info.remote_version = result.version;
                changed++;

                if (archive_path != null) {
                    GLib.FileUtils.remove (archive_path);
                }

                stdout.printf (
                    "AtM: repository %s ready version=%s sha=%s snapshot=%s index=%s\n",
                    descriptor.acronym,
                    result.version,
                    sha,
                    result.snapshot_path,
                    result.index_path
                );
            }

            return changed;
        }
    }
}
