namespace AskTheModel {
    public errordomain StartupQualificationError {
        RECORD
    }

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

    public class StartupRepositoryQualification : Object {
        public string repository_id { get; construct; }
        public StartupRepositoryStatus status { get; set; }
        public string reason_code { get; set; default = ""; }
        public string detail { get; set; default = ""; }
        public string? snapshot_sha { get; set; }
        public string? persisted_version { get; set; }
        public string? validated_version { get; set; }
        public string? index_path { get; set; }

        public StartupRepositoryQualification (
            string repository_id
        ) {
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

        public StartupRepositoryQualification[] repositories {
            get;
            set;
            default = {};
        }

        public bool installation_qualified {
            get {
                return platform_qualified && storage_qualified;
            }
        }

        public string record_path { get; set; default = ""; }
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

        private static string[] copy_strv (
            string[]? values
        ) {
            string[] copy = {};

            if (values == null) {
                return copy;
            }

            foreach (unowned string value in values) {
                copy += value;
            }

            return copy;
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

        private static string execution_mode_name (
            StartupQualificationNative.ExecutionMode mode
        ) {
            switch (mode) {
                case StartupQualificationNative.ExecutionMode.DEVELOPMENT:
                    return "development";
                case StartupQualificationNative.ExecutionMode.FLATPAK:
                    return "flatpak";
                default:
                    assert_not_reached ();
            }
        }

        private static void add_nullable_string (
            Json.Builder builder,
            string? value
        ) {
            if (value == null) {
                builder.add_null_value ();
            } else {
                builder.add_string_value (value);
            }
        }

        private static void add_string_array (
            Json.Builder builder,
            string[] values
        ) {
            builder.begin_array ();

            foreach (string value in values) {
                builder.add_string_value (value);
            }

            builder.end_array ();
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

        private StartupRepositoryQualification reconcile_one (
            RepositoryDescriptor descriptor,
            RepositoryLocalRecord local
        ) {
            var result = new StartupRepositoryQualification (
                descriptor.id
            );
            result.snapshot_sha = local.current_sha;
            result.persisted_version = local.version;

            string sha = local.current_sha ?? "";
            string version = local.version ?? "";

            try {
                StartupQualificationNative.RepositoryReconcileResult
                    native_result;

                bool completed =
                    StartupQualificationNative.reconcile_local (
                        cache_root,
                        snapshot_path (descriptor, sha),
                        descriptor.id,
                        descriptor.acronym,
                        descriptor.display_name,
                        sha,
                        version,
                        out native_result
                    );

                if (!completed) {
                    result.status =
                        StartupRepositoryStatus.INDEX_ERROR;
                    result.reason_code =
                        "reconcile_incomplete";
                    result.detail =
                        "Repository reconciliation returned no result.";
                    return result;
                }

                result.reason_code =
                    native_result.reason_code ??
                    "unknown";
                result.detail =
                    native_result.detail ??
                    "";
                result.validated_version =
                    native_result.repository_version;
                result.index_path =
                    native_result.index_path;

                switch (native_result.status) {
                    case StartupQualificationNative.RepositoryReconcileStatus.SNAPSHOT_MISSING:
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_MISSING;
                        break;
                    case StartupQualificationNative.RepositoryReconcileStatus.SNAPSHOT_INVALID:
                        result.status =
                            StartupRepositoryStatus.SNAPSHOT_INVALID;
                        break;
                    case StartupQualificationNative.RepositoryReconcileStatus.INDEX_ERROR:
                        result.status =
                            StartupRepositoryStatus.INDEX_ERROR;
                        break;
                    case StartupQualificationNative.RepositoryReconcileStatus.READY:
                        result.status =
                            StartupRepositoryStatus.READY;
                        break;
                    case StartupQualificationNative.RepositoryReconcileStatus.READY_REPAIRED_INDEX:
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
            } catch (GLib.Error error) {
                result.status =
                    StartupRepositoryStatus.INDEX_ERROR;
                result.reason_code =
                    "reconcile_error";
                result.detail = error.message;
            }

            return result;
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
                "installation-qualification.json"
            );

            try {
                StartupQualificationNative.DeploymentQualification
                    deployment;

                bool completed =
                    StartupQualificationNative.qualify_deployment (
                        flatpak_info_path,
                        EXPECTED_APP_ID,
                        EXPECTED_RUNTIME_ID,
                        EXPECTED_RUNTIME_BRANCH,
                        POLICY_VERSION,
                        out deployment
                    );

                if (completed) {
                    report.execution_mode =
                        execution_mode_name (
                            deployment.execution_mode
                        );
                    report.platform_qualified =
                        deployment.platform_qualified;

                    report.application_id =
                        deployment.application_id;
                    report.application_ref =
                        deployment.application_ref;
                    report.application_commit =
                        deployment.application_commit;
                    report.runtime_ref =
                        deployment.runtime_ref;
                    report.runtime_commit =
                        deployment.runtime_commit;
                    report.architecture =
                        deployment.architecture;
                    report.branch =
                        deployment.branch;
                    report.flatpak_version =
                        deployment.flatpak_version;
                    report.application_extensions =
                        copy_strv (
                            deployment.application_extensions
                        );
                    report.runtime_extensions =
                        copy_strv (
                            deployment.runtime_extensions
                        );
                    report.platform_fingerprint =
                        deployment.platform_fingerprint;

                    if (deployment.platform_qualified) {
                        report.platform_reason_code =
                            "qualified";
                    } else {
                        report.platform_reason_code =
                            deployment.execution_mode ==
                                StartupQualificationNative.ExecutionMode.DEVELOPMENT
                                ? "development_execution"
                                : "platform_unqualified";
                    }
                } else {
                    report.platform_qualified = false;
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
                StartupQualificationNative.StorageQualification
                    storage;

                bool completed =
                    StartupQualificationNative.qualify_storage_root (
                        data_root,
                        out storage
                    );

                if (completed) {
                    report.storage_qualified =
                        storage.qualified;
                    report.storage_created =
                        storage.created;
                    report.storage_mode =
                        storage.mode;
                    report.storage_owner_uid =
                        storage.owner_uid;
                    report.storage_reason_code =
                        storage.qualified
                            ? "qualified"
                            : "storage_unqualified";
                } else {
                    report.storage_qualified = false;
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

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                RepositoryLocalRecord local =
                    state_store.record_for (descriptor.id);
                StartupRepositoryQualification repository;

                if (state_store.load_status ==
                    RepositoryStateLoadStatus.INVALID) {
                    repository =
                        new StartupRepositoryQualification (
                            descriptor.id
                        );
                    repository.status =
                        StartupRepositoryStatus.STATE_INVALID;
                    repository.reason_code =
                        "repository_state_invalid";
                    repository.detail =
                        "Persistent repository state is invalid; no snapshot was selected heuristically.";
                } else if (!report.installation_qualified) {
                    repository =
                        new StartupRepositoryQualification (
                            descriptor.id
                        );
                    repository.status =
                        StartupRepositoryStatus.BLOCKED;
                    repository.reason_code =
                        "installation_unqualified";
                    repository.detail =
                        "Repository reconciliation is blocked until platform and storage qualification pass.";
                    repository.snapshot_sha =
                        local.current_sha;
                    repository.persisted_version =
                        local.version;
                } else if (!local.is_ready ()) {
                    repository =
                        new StartupRepositoryQualification (
                            descriptor.id
                        );
                    repository.status =
                        StartupRepositoryStatus.NOT_INSTALLED;
                    repository.reason_code =
                        "not_installed";
                    repository.detail =
                        "No persisted repository snapshot is installed.";
                } else {
                    repository = reconcile_one (
                        descriptor,
                        local
                    );
                }

                report.repositories += repository;
            }

            write_record (report);
            return report;
        }

        private void write_record (
            StartupQualificationReport report
        ) throws GLib.Error {
            if (GLib.DirUtils.create_with_parents (
                    state_root,
                    0700
                ) != 0) {
                throw new StartupQualificationError.RECORD (
                    "Startup qualification state directory could not be created."
                );
            }

            var builder = new Json.Builder ();
            builder.begin_object ();

            builder.set_member_name ("schema_version");
            builder.add_int_value (1);

            builder.set_member_name ("policy_version");
            builder.add_int_value (
                (int64) report.policy_version
            );

            builder.set_member_name ("qualified_at_utc");
            builder.add_string_value (
                report.qualified_at_utc
            );

            builder.set_member_name ("execution_mode");
            builder.add_string_value (
                report.execution_mode
            );

            builder.set_member_name ("platform");
            builder.begin_object ();
            builder.set_member_name ("qualified");
            builder.add_boolean_value (
                report.platform_qualified
            );
            builder.set_member_name ("reason_code");
            builder.add_string_value (
                report.platform_reason_code
            );
            builder.set_member_name ("detail");
            builder.add_string_value (
                report.platform_detail
            );
            builder.set_member_name ("application_id");
            add_nullable_string (
                builder,
                report.application_id
            );
            builder.set_member_name ("application_ref");
            add_nullable_string (
                builder,
                report.application_ref
            );
            builder.set_member_name ("application_commit");
            add_nullable_string (
                builder,
                report.application_commit
            );
            builder.set_member_name ("runtime_ref");
            add_nullable_string (
                builder,
                report.runtime_ref
            );
            builder.set_member_name ("runtime_commit");
            add_nullable_string (
                builder,
                report.runtime_commit
            );
            builder.set_member_name ("architecture");
            add_nullable_string (
                builder,
                report.architecture
            );
            builder.set_member_name ("branch");
            add_nullable_string (
                builder,
                report.branch
            );
            builder.set_member_name ("flatpak_version");
            add_nullable_string (
                builder,
                report.flatpak_version
            );
            builder.set_member_name ("application_extensions");
            add_string_array (
                builder,
                report.application_extensions
            );
            builder.set_member_name ("runtime_extensions");
            add_string_array (
                builder,
                report.runtime_extensions
            );
            builder.set_member_name ("fingerprint");
            add_nullable_string (
                builder,
                report.platform_fingerprint
            );
            builder.end_object ();

            builder.set_member_name ("storage");
            builder.begin_object ();
            builder.set_member_name ("qualified");
            builder.add_boolean_value (
                report.storage_qualified
            );
            builder.set_member_name ("reason_code");
            builder.add_string_value (
                report.storage_reason_code
            );
            builder.set_member_name ("detail");
            builder.add_string_value (
                report.storage_detail
            );
            builder.set_member_name ("root");
            builder.add_string_value (
                report.storage_root
            );
            builder.set_member_name ("created");
            builder.add_boolean_value (
                report.storage_created
            );
            builder.set_member_name ("mode");
            builder.add_int_value (
                (int64) report.storage_mode
            );
            builder.set_member_name ("owner_uid");
            builder.add_int_value (
                (int64) report.storage_owner_uid
            );
            builder.end_object ();

            builder.set_member_name ("repository_state");
            builder.begin_object ();
            builder.set_member_name ("status");
            builder.add_string_value (
                repository_state_name (
                    report.repository_state_status
                )
            );
            builder.end_object ();

            builder.set_member_name ("repositories");
            builder.begin_array ();

            foreach (
                StartupRepositoryQualification repository
                in report.repositories
            ) {
                builder.begin_object ();
                builder.set_member_name ("id");
                builder.add_string_value (
                    repository.repository_id
                );
                builder.set_member_name ("status");
                builder.add_string_value (
                    repository_status_name (
                        repository.status
                    )
                );
                builder.set_member_name ("reason_code");
                builder.add_string_value (
                    repository.reason_code
                );
                builder.set_member_name ("detail");
                builder.add_string_value (
                    repository.detail
                );
                builder.set_member_name ("snapshot_sha");
                add_nullable_string (
                    builder,
                    repository.snapshot_sha
                );
                builder.set_member_name ("persisted_version");
                add_nullable_string (
                    builder,
                    repository.persisted_version
                );
                builder.set_member_name ("validated_version");
                add_nullable_string (
                    builder,
                    repository.validated_version
                );
                builder.set_member_name ("index_path");
                add_nullable_string (
                    builder,
                    repository.index_path
                );
                builder.end_object ();
            }

            builder.end_array ();

            builder.set_member_name ("overall");
            builder.begin_object ();
            builder.set_member_name (
                "installation_qualified"
            );
            builder.add_boolean_value (
                report.installation_qualified
            );
            builder.end_object ();

            builder.end_object ();

            var generator = new Json.Generator ();
            generator.set_root (builder.get_root ());
            generator.pretty = true;

            string data = generator.to_data (null);

            try {
                GLib.FileUtils.set_contents_full (
                    report.record_path,
                    data,
                    -1,
                    GLib.FileSetContentsFlags.CONSISTENT |
                        GLib.FileSetContentsFlags.DURABLE,
                    0600
                );
            } catch (GLib.FileError error) {
                throw new StartupQualificationError.RECORD (
                    "Startup qualification record could not be written: %s".printf (
                        error.message
                    )
                );
            }
        }
    }
}
