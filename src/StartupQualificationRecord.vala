namespace AskTheModel {
    public class StartupRepositoryQualification : Object {
        public string repository_id { get; construct; }
        public string status { get; construct; }
        public string reason_code { get; construct; }
        public string? sha { get; construct; }
        public string? version { get; construct; }
        public string? index_path { get; construct; }

        public StartupRepositoryQualification (
            string repository_id,
            string status,
            string reason_code,
            string? sha = null,
            string? version = null,
            string? index_path = null
        ) {
            Object (
                repository_id: repository_id,
                status: status,
                reason_code: reason_code,
                sha: sha,
                version: version,
                index_path: index_path
            );
        }
    }

    public class StartupQualificationRecord : Object {
        public const int SCHEMA_VERSION = 1;

        private string record_path;
        private StartupRepositoryQualification[] repositories = {};

        public int policy_version { get; construct; }
        public string qualified_at_utc { get; construct; }
        public string execution_mode { get; construct; }
        public bool platform_qualified { get; construct; }
        public bool storage_qualified { get; construct; }
        public string state_load_status { get; construct; }
        public string? platform_fingerprint { get; construct; }

        public string? application_id { get; construct; }
        public string? application_ref { get; construct; }
        public string? application_commit { get; construct; }
        public string? runtime_ref { get; construct; }
        public string? runtime_commit { get; construct; }
        public string? architecture { get; construct; }
        public string? branch { get; construct; }
        public string? flatpak_version { get; construct; }

        public StartupQualificationRecord (
            int policy_version,
            string qualified_at_utc,
            string execution_mode,
            bool platform_qualified,
            bool storage_qualified,
            string state_load_status,
            string? platform_fingerprint = null,
            string? application_id = null,
            string? application_ref = null,
            string? application_commit = null,
            string? runtime_ref = null,
            string? runtime_commit = null,
            string? architecture = null,
            string? branch = null,
            string? flatpak_version = null,
            string? state_root = null
        ) {
            Object (
                policy_version: policy_version,
                qualified_at_utc: qualified_at_utc,
                execution_mode: execution_mode,
                platform_qualified: platform_qualified,
                storage_qualified: storage_qualified,
                state_load_status: state_load_status,
                platform_fingerprint: platform_fingerprint,
                application_id: application_id,
                application_ref: application_ref,
                application_commit: application_commit,
                runtime_ref: runtime_ref,
                runtime_commit: runtime_commit,
                architecture: architecture,
                branch: branch,
                flatpak_version: flatpak_version
            );

            string root = state_root ??
                GLib.Environment.get_user_state_dir ();
            record_path = GLib.Path.build_filename (
                root,
                "startup-qualification.json"
            );
        }

        public void add_repository (
            StartupRepositoryQualification result
        ) {
            foreach (
                StartupRepositoryQualification existing
                in repositories
            ) {
                if (existing.repository_id == result.repository_id) {
                    assert_not_reached ();
                }
            }

            repositories += result;
        }

        private static void add_nullable_string (
            Json.Builder builder,
            string member_name,
            string? value
        ) {
            builder.set_member_name (member_name);
            if (value == null) {
                builder.add_null_value ();
            } else {
                builder.add_string_value (value);
            }
        }

        public string to_json () {
            var builder = new Json.Builder ();
            builder.begin_object ();

            builder.set_member_name ("schema_version");
            builder.add_int_value (SCHEMA_VERSION);
            builder.set_member_name ("policy_version");
            builder.add_int_value (policy_version);
            builder.set_member_name ("qualified_at_utc");
            builder.add_string_value (qualified_at_utc);
            builder.set_member_name ("execution_mode");
            builder.add_string_value (execution_mode);
            builder.set_member_name ("platform_qualified");
            builder.add_boolean_value (platform_qualified);
            builder.set_member_name ("storage_qualified");
            builder.add_boolean_value (storage_qualified);
            builder.set_member_name ("repository_state_load_status");
            builder.add_string_value (state_load_status);

            add_nullable_string (
                builder,
                "platform_fingerprint",
                platform_fingerprint
            );

            builder.set_member_name ("deployment");
            builder.begin_object ();
            add_nullable_string (builder, "application_id", application_id);
            add_nullable_string (builder, "application_ref", application_ref);
            add_nullable_string (
                builder,
                "application_commit",
                application_commit
            );
            add_nullable_string (builder, "runtime_ref", runtime_ref);
            add_nullable_string (
                builder,
                "runtime_commit",
                runtime_commit
            );
            add_nullable_string (builder, "architecture", architecture);
            add_nullable_string (builder, "branch", branch);
            add_nullable_string (
                builder,
                "flatpak_version",
                flatpak_version
            );
            builder.end_object ();

            builder.set_member_name ("repositories");
            builder.begin_array ();

            foreach (
                StartupRepositoryQualification result
                in repositories
            ) {
                builder.begin_object ();
                builder.set_member_name ("id");
                builder.add_string_value (result.repository_id);
                builder.set_member_name ("status");
                builder.add_string_value (result.status);
                builder.set_member_name ("reason_code");
                builder.add_string_value (result.reason_code);
                add_nullable_string (builder, "sha", result.sha);
                add_nullable_string (builder, "version", result.version);
                add_nullable_string (
                    builder,
                    "index_path",
                    result.index_path
                );
                builder.end_object ();
            }

            builder.end_array ();
            builder.end_object ();

            var generator = new Json.Generator ();
            generator.set_root (builder.get_root ());
            generator.pretty = true;
            return generator.to_data (null);
        }

        public void save () throws RepositoryError {
            string? parent = GLib.Path.get_dirname (record_path);

            if (parent == null ||
                GLib.DirUtils.create_with_parents (
                    parent,
                    0700
                ) != 0) {
                throw new RepositoryError.STORAGE (
                    "Startup qualification state directory could not be created."
                );
            }

            try {
                GLib.FileUtils.set_contents_full (
                    record_path,
                    to_json (),
                    -1,
                    GLib.FileSetContentsFlags.CONSISTENT |
                        GLib.FileSetContentsFlags.DURABLE,
                    0600
                );
            } catch (GLib.FileError error) {
                throw new RepositoryError.STORAGE (
                    "Startup qualification record could not be written: %s".printf (
                        error.message
                    )
                );
            }
        }
    }
}
