namespace AskTheModel {
    public enum StartupRepositoryStatus {
        BLOCKED,
        STATE_INVALID,
        NOT_INSTALLED,
        SNAPSHOT_MISSING,
        SNAPSHOT_INVALID,
        INDEX_ERROR,
        READY,
        READY_REPAIRED_INDEX
    }

    public class StartupRepositoryOutcome : Object {
        public string repository_id { get; construct; }
        public StartupRepositoryStatus status { get; set; }
        public string reason_code { get; set; default = ""; }
        public string detail { get; set; default = ""; }
        public string? snapshot_sha { get; set; }
        public string? persisted_version { get; set; }
        public string? validated_version { get; set; }
        public string? index_path { get; set; }

        public StartupRepositoryOutcome (string repository_id) {
            Object (repository_id: repository_id);
        }
    }

    public class StartupQualificationReport : Object {
        public uint policy_version { get; set; }
        public string qualified_at_utc { get; set; default = ""; }

        public string execution_mode { get; set; default = "unknown"; }
        public bool platform_qualified { get; set; }
        public string platform_reason_code { get; set; default = ""; }
        public string platform_detail { get; set; default = ""; }

        public string? application_id { get; set; }
        public string? application_ref { get; set; }
        public string? application_commit { get; set; }
        public string? runtime_ref { get; set; }
        public string? runtime_commit { get; set; }
        public string? architecture { get; set; }
        public string? branch { get; set; }
        public string? flatpak_version { get; set; }
        public string[] application_extensions { get; set; default = {}; }
        public string[] runtime_extensions { get; set; default = {}; }
        public string? platform_fingerprint { get; set; }

        public bool storage_qualified { get; set; }
        public bool storage_created { get; set; }
        public uint32 storage_mode { get; set; }
        public uint64 storage_owner_uid { get; set; }
        public string storage_reason_code { get; set; default = ""; }
        public string storage_detail { get; set; default = ""; }
        public string storage_root { get; set; default = ""; }

        public RepositoryStateLoadStatus repository_state_status {
            get;
            set;
            default = RepositoryStateLoadStatus.ABSENT;
        }

        public StartupRepositoryOutcome[] repositories = {};
        public string record_path { get; set; default = ""; }

        public bool installation_qualified {
            get {
                return platform_qualified && storage_qualified;
            }
        }
    }

    public class StartupQualificationService : Object {
        public const uint POLICY_VERSION = 1;
        private const string EXPECTED_APP_ID =
            "io.github.laurentiustaicu.ask_the_model";
        private const string EXPECTED_RUNTIME_ID =
            "io.elementary.Platform";
        private const string EXPECTED_RUNTIME_BRANCH = "8";

        private string flatpak_info_path;
        private string data_root;
        private string cache_root;
        private string state_root;

        public StartupQualificationService (
            string? flatpak_info_path = null,
            string? data_root = null,
            string? cache_root = null,
            string? state_root = null
        ) {
            this.flatpak_info_path =
                flatpak_info_path ?? "/.flatpak-info";
            this.data_root =
                data_root ??
                GLib.Path.build_filename (
                    GLib.Environment.get_home_dir (),
                    "Ask the Model"
                );
            this.cache_root =
                cache_root ??
                GLib.Environment.get_user_cache_dir ();
            this.state_root =
                state_root ??
                GLib.Environment.get_user_state_dir ();
        }

        private static string execution_mode_name (int mode) {
            switch (mode) {
                case 0:
                    return "development";
                case 1:
                    return "flatpak";
                default:
                    return "unknown";
            }
        }

        private static string repository_state_name (
            RepositoryStateLoadStatus status
        ) {
            switch (status) {
                case RepositoryStateLoadStatus.ABSENT:
                    return "ABSENT";
                case RepositoryStateLoadStatus.VALID:
                    return "VALID";
                case RepositoryStateLoadStatus.INVALID:
                    return "INVALID";
                default:
                    assert_not_reached ();
            }
        }

        private static string repository_status_name (
            StartupRepositoryStatus status
        ) {
            switch (status) {
                case StartupRepositoryStatus.BLOCKED:
                    return "BLOCKED";
                case StartupRepositoryStatus.STATE_INVALID:
                    return "STATE_INVALID";
                case StartupRepositoryStatus.NOT_INSTALLED:
                    return "NOT_INSTALLED";
                case StartupRepositoryStatus.SNAPSHOT_MISSING:
                    return "SNAPSHOT_MISSING";
                case StartupRepositoryStatus.SNAPSHOT_INVALID:
                    return "SNAPSHOT_INVALID";
                case StartupRepositoryStatus.INDEX_ERROR:
                    return "INDEX_ERROR";
                case StartupRepositoryStatus.READY:
                    return "READY";
                case StartupRepositoryStatus.READY_REPAIRED_INDEX:
                    return "READY_REPAIRED_INDEX";
                default:
                    assert_not_reached ();
            }
        }

        private static string[] extension_lines (string serialized) {
            string[] values = {};

            if (serialized.length == 0) {
                return values;
            }

            foreach (string value in serialized.split ("\n")) {
                if (value.length > 0) {
                    values += value;
                }
            }

            return values;
        }

        private string snapshot_path (
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

        private StartupRepositoryOutcome reconcile_one (
            RepositoryDescriptor descriptor,
            RepositoryLocalRecord local,
            RepositoryStateStore state_store
        ) {
            var result = new StartupRepositoryOutcome (
                descriptor.id
            );
            result.snapshot_sha = local.current_sha;
            result.persisted_version = local.version;

            string sha = local.current_sha ?? "";
            string version = local.version ?? "";
            string snapshot = snapshot_path (
                descriptor,
                sha
            );
            string? pre_seal = null;

            try {
                string current_seal;
                uint64 sealed_files;
                uint64 sealed_bytes;

                bool sealed =
                    RepositoryNative.compute_snapshot_seal (
                        snapshot,
                        out current_seal,
                        out sealed_files,
                        out sealed_bytes
                    );

                if (!sealed) {
                    result.status =
                        StartupRepositoryStatus.SNAPSHOT_INVALID;
                    result.reason_code =
                        "snapshot_seal_check_incomplete";
                    result.detail =
                        "Snapshot integrity seal could not be computed before reconciliation.";
                    return result;
                }

                pre_seal = current_seal;

                if (local.snapshot_seal_sha256 != null &&
                    local.snapshot_seal_sha256 != current_seal) {
                    result.status =
                        StartupRepositoryStatus.SNAPSHOT_INVALID;
                    result.reason_code =
                        "snapshot_seal_mismatch";
                    result.detail =
                        "Snapshot integrity seal does not match persistent repository state.";
                    return result;
                }
            } catch (GLib.Error error) {
                result.status =
                    StartupRepositoryStatus.SNAPSHOT_INVALID;
                result.reason_code =
                    "snapshot_seal_check_failed";
                result.detail = error.message;
                return result;
            }

            try {
                int native_status;
                string native_reason_code;
                string native_detail;
                string? native_version;
                string? native_index_path;

                bool completed =
                    StartupQualificationNative.reconcile_local_values (
                        cache_root,
                        snapshot,
                        descriptor.id,
                        descriptor.acronym,
                        descriptor.display_name,
                        sha,
                        version,
                        out native_status,
                        out native_reason_code,
                        out native_detail,
                        out native_version,
                        out native_index_path
                    );

                if (!completed) {
                    result.status =
                        StartupRepositoryStatus.INDEX_ERROR;
                    result.reason_code = "reconcile_incomplete";
                    result.detail =
                        "Repository reconciliation returned no result.";
                    return result;
                }

                result.reason_code = native_reason_code;
                result.detail = native_detail;
                result.validated_version = native_version;
                result.index_path = native_index_path;

                switch (native_status) {
                    case 0:
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_MISSING;
                        break;
                    case 1:
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_INVALID;
                        break;
                    case 2:
                        result.status =
                            StartupRepositoryStatus.INDEX_ERROR;
                        break;
                    case 3:
                        result.status =
                            StartupRepositoryStatus.READY;
                        break;
                    case 4:
                        result.status =
                            StartupRepositoryStatus.READY_REPAIRED_INDEX;
                        break;
                    default:
                        result.status =
                            StartupRepositoryStatus.INDEX_ERROR;
                        result.reason_code =
                            "unknown_reconcile_status";
                        result.detail =
                            "Repository reconciler returned an unknown status.";
                        break;
                }

                if (result.status ==
                        StartupRepositoryStatus.READY ||
                    result.status ==
                        StartupRepositoryStatus.READY_REPAIRED_INDEX) {
                    string post_seal;
                    uint64 sealed_files;
                    uint64 sealed_bytes;

                    if (!RepositoryNative.compute_snapshot_seal (
                            snapshot,
                            out post_seal,
                            out sealed_files,
                            out sealed_bytes
                        )) {
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_INVALID;
                        result.reason_code =
                            "snapshot_seal_check_incomplete";
                        result.detail =
                            "Snapshot integrity seal could not be recomputed after reconciliation.";
                        result.index_path = null;
                        return result;
                    }

                    if (pre_seal == null ||
                        pre_seal != post_seal) {
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_INVALID;
                        result.reason_code =
                            "snapshot_changed_during_reconciliation";
                        result.detail =
                            "Snapshot content changed while startup reconciliation was running.";
                        result.index_path = null;
                        return result;
                    }

                    if (local.snapshot_seal_sha256 != null &&
                        local.snapshot_seal_sha256 != post_seal) {
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_INVALID;
                        result.reason_code =
                            "snapshot_seal_mismatch";
                        result.detail =
                            "Snapshot integrity seal changed during reconciliation.";
                        result.index_path = null;
                        return result;
                    }

                    if (local.snapshot_seal_sha256 == null) {
                        try {
                            state_store.set_snapshot_seal (
                                descriptor.id,
                                sha,
                                post_seal
                            );
                        } catch (RepositoryError error) {
                            result.status =
                                StartupRepositoryStatus.SNAPSHOT_INVALID;
                            result.reason_code =
                                "snapshot_seal_persist_failed";
                            result.detail = error.message;
                            result.index_path = null;
                            return result;
                        }
                    }
                }
            } catch (GLib.Error error) {
                result.status =
                    StartupRepositoryStatus.INDEX_ERROR;
                result.reason_code = "reconcile_error";
                result.detail = error.message;
            }

            return result;
        }

        private void persist_report (
            StartupQualificationReport report
        ) throws RepositoryError {
            var record = new StartupQualificationRecord (
                (int) report.policy_version,
                report.qualified_at_utc,
                report.execution_mode,
                report.platform_qualified,
                report.storage_qualified,
                repository_state_name (
                    report.repository_state_status
                ),
                report.platform_fingerprint,
                report.application_id,
                report.application_ref,
                report.application_commit,
                report.runtime_ref,
                report.runtime_commit,
                report.architecture,
                report.branch,
                report.flatpak_version,
                state_root
            );

            record.platform_reason_code =
                report.platform_reason_code;
            record.platform_detail =
                report.platform_detail;
            record.storage_reason_code =
                report.storage_reason_code;
            record.storage_detail =
                report.storage_detail;
            record.storage_root =
                report.storage_root;
            record.storage_created =
                report.storage_created;
            record.storage_mode =
                report.storage_mode;
            record.storage_owner_uid =
                report.storage_owner_uid;

            foreach (
                string extension
                in report.application_extensions
            ) {
                record.add_application_extension (extension);
            }

            foreach (
                string extension
                in report.runtime_extensions
            ) {
                record.add_runtime_extension (extension);
            }

            foreach (
                StartupRepositoryOutcome outcome
                in report.repositories
            ) {
                record.add_repository (
                    new StartupRepositoryQualification (
                        outcome.repository_id,
                        repository_status_name (
                            outcome.status
                        ),
                        outcome.reason_code,
                        outcome.snapshot_sha,
                        outcome.validated_version ??
                            outcome.persisted_version,
                        outcome.index_path
                    )
                );
            }

            record.save ();
        }

        public StartupQualificationReport run ()
            throws GLib.Error {
            var report = new StartupQualificationReport ();
            report.policy_version = POLICY_VERSION;
            report.qualified_at_utc =
                new GLib.DateTime.now_utc ().format_iso8601 ();
            report.storage_root = data_root;
            report.record_path = GLib.Path.build_filename (
                state_root,
                "startup-qualification.json"
            );

            try {
                int execution_mode;
                bool platform_qualified;
                string? application_id;
                string? application_ref;
                string? application_commit;
                string? runtime_ref;
                string? runtime_commit;
                string? architecture;
                string? branch;
                string? flatpak_version;
                string application_extensions;
                string runtime_extensions;
                string? platform_fingerprint;

                bool completed =
                    StartupQualificationNative.qualify_deployment_values (
                        flatpak_info_path,
                        EXPECTED_APP_ID,
                        EXPECTED_RUNTIME_ID,
                        EXPECTED_RUNTIME_BRANCH,
                        POLICY_VERSION,
                        out execution_mode,
                        out platform_qualified,
                        out application_id,
                        out application_ref,
                        out application_commit,
                        out runtime_ref,
                        out runtime_commit,
                        out architecture,
                        out branch,
                        out flatpak_version,
                        out application_extensions,
                        out runtime_extensions,
                        out platform_fingerprint
                    );

                if (completed) {
                    report.execution_mode =
                        execution_mode_name (execution_mode);
                    report.platform_qualified =
                        platform_qualified;
                    report.application_id = application_id;
                    report.application_ref = application_ref;
                    report.application_commit =
                        application_commit;
                    report.runtime_ref = runtime_ref;
                    report.runtime_commit = runtime_commit;
                    report.architecture = architecture;
                    report.branch = branch;
                    report.flatpak_version = flatpak_version;
                    report.application_extensions =
                        extension_lines (
                            application_extensions
                        );
                    report.runtime_extensions =
                        extension_lines (
                            runtime_extensions
                        );
                    report.platform_fingerprint =
                        platform_fingerprint;
                    report.platform_reason_code =
                        platform_qualified
                            ? "qualified"
                            : execution_mode == 0
                                ? "development_execution"
                                : "platform_unqualified";
                } else {
                    report.platform_reason_code =
                        "qualification_incomplete";
                    report.platform_detail =
                        "Deployment qualification returned no result.";
                }
            } catch (GLib.Error error) {
                report.platform_qualified = false;
                report.platform_reason_code =
                    "deployment_invalid";
                report.platform_detail = error.message;
            }

            try {
                bool storage_qualified;
                bool storage_created;
                uint32 storage_mode;
                uint64 storage_owner_uid;

                bool completed =
                    StartupQualificationNative.qualify_storage_root_values (
                        data_root,
                        out storage_qualified,
                        out storage_created,
                        out storage_mode,
                        out storage_owner_uid
                    );

                if (completed) {
                    report.storage_qualified =
                        storage_qualified;
                    report.storage_created =
                        storage_created;
                    report.storage_mode =
                        storage_mode;
                    report.storage_owner_uid =
                        storage_owner_uid;
                    report.storage_reason_code =
                        storage_qualified
                            ? "qualified"
                            : "storage_unqualified";
                } else {
                    report.storage_reason_code =
                        "qualification_incomplete";
                    report.storage_detail =
                        "Storage qualification returned no result.";
                }
            } catch (GLib.Error error) {
                report.storage_qualified = false;
                report.storage_reason_code =
                    "storage_invalid";
                report.storage_detail = error.message;
            }

            var state_store = new RepositoryStateStore (
                state_root
            );
            report.repository_state_status =
                state_store.load_status;

            StartupRepositoryOutcome[] outcomes = {};

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                RepositoryLocalRecord local =
                    state_store.record_for (descriptor.id);
                StartupRepositoryOutcome outcome;

                if (state_store.load_status ==
                    RepositoryStateLoadStatus.INVALID) {
                    outcome = new StartupRepositoryOutcome (
                        descriptor.id
                    );
                    outcome.status =
                        StartupRepositoryStatus.STATE_INVALID;
                    outcome.reason_code =
                        "repository_state_invalid";
                    outcome.detail =
                        "Persistent repository state is invalid; no snapshot was selected heuristically.";
                } else if (!report.installation_qualified) {
                    outcome = new StartupRepositoryOutcome (
                        descriptor.id
                    );
                    outcome.status =
                        StartupRepositoryStatus.BLOCKED;
                    outcome.reason_code =
                        "installation_unqualified";
                    outcome.detail =
                        "Repository reconciliation is blocked until platform and storage qualification pass.";
                    outcome.snapshot_sha =
                        local.current_sha;
                    outcome.persisted_version =
                        local.version;
                } else if (!local.is_ready ()) {
                    outcome = new StartupRepositoryOutcome (
                        descriptor.id
                    );
                    outcome.status =
                        StartupRepositoryStatus.NOT_INSTALLED;
                    outcome.reason_code =
                        "not_installed";
                    outcome.detail =
                        "No persisted repository snapshot is installed.";
                } else {
                    outcome = reconcile_one (
                        descriptor,
                        local,
                        state_store
                    );
                }

                outcomes += outcome;
            }

            report.repositories = outcomes;
            persist_report (report);
            return report;
        }
    }
}
