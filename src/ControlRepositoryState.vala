namespace AskTheModel {
    public class ControlRepositoryStateStore : Object {
        public const int CONTROL_SCHEMA_VERSION = 1;

        private string state_path;
        private RepositoryLocalRecord[] records = {};

        public RepositoryStateLoadStatus load_status {
            get;
            private set;
            default = RepositoryStateLoadStatus.ABSENT;
        }

        public int control_schema_version {
            get;
            private set;
            default = 0;
        }

        public ControlRepositoryStateStore (
            string? state_root = null
        ) {
            string root = state_root ??
                GLib.Environment.get_user_state_dir ();
            state_path = GLib.Path.build_filename (
                root,
                "control-state.sqlite3"
            );

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                records += new RepositoryLocalRecord (
                    descriptor.id
                );
            }

            load_best_effort ();
        }

        private RepositoryLocalRecord? record_for_optional (
            string repository_id
        ) {
            foreach (RepositoryLocalRecord record in records) {
                if (record.repository_id == repository_id) {
                    return record;
                }
            }

            return null;
        }

        public RepositoryLocalRecord record_for (
            string repository_id
        ) {
            RepositoryLocalRecord? record =
                record_for_optional (repository_id);

            if (record != null) {
                return record;
            }

            assert_not_reached ();
        }

        private static bool sha40_is_valid (string value) {
            return GLib.Regex.match_simple (
                "^[0-9a-f]{40}$",
                value
            );
        }

        private static bool seal_is_valid (string value) {
            return GLib.Regex.match_simple (
                "^[0-9a-f]{64}$",
                value
            );
        }

        private void clear_records () {
            foreach (RepositoryLocalRecord record in records) {
                record.current_sha = null;
                record.version = null;
                record.snapshot_seal_sha256 = null;
            }
        }

        private void load_best_effort () {
            load_status = RepositoryStateLoadStatus.ABSENT;
            control_schema_version = 0;
            clear_records ();

            if (!GLib.FileUtils.test (
                    state_path,
                    GLib.FileTest.EXISTS
                )) {
                return;
            }

            load_status = RepositoryStateLoadStatus.INVALID;

            try {
                foreach (
                    RepositoryDescriptor descriptor
                    in RepositoryCatalog.all ()
                ) {
                    bool present;
                    string? sha;
                    string? version;
                    string? seal;

                    ControlStateNative.load_repository_values (
                        state_path,
                        descriptor.id,
                        out present,
                        out sha,
                        out version,
                        out seal
                    );

                    if (!present) {
                        continue;
                    }

                    if (sha == null ||
                        version == null) {
                        clear_records ();
                        return;
                    }

                    RepositoryLocalRecord record =
                        record_for (descriptor.id);
                    record.current_sha = sha;
                    record.version = version;
                    record.snapshot_seal_sha256 = seal;
                }

                load_status = RepositoryStateLoadStatus.VALID;
                control_schema_version =
                    CONTROL_SCHEMA_VERSION;
            } catch (GLib.Error error) {
                clear_records ();
                stderr.printf (
                    "AtM: control repository state invalid: %s\n",
                    error.message
                );
            }
        }

        public void set_current (
            string repository_id,
            string sha,
            string version,
            string? snapshot_seal_sha256 = null
        ) throws RepositoryError {
            if (load_status !=
                RepositoryStateLoadStatus.VALID) {
                throw new RepositoryError.STORAGE (
                    "Control repository state authority is not valid and cannot be created or repaired implicitly."
                );
            }

            if (!sha40_is_valid (sha)) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository state SHA is invalid."
                );
            }

            if (version.length == 0) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository state version is empty."
                );
            }

            if (snapshot_seal_sha256 != null &&
                !seal_is_valid (
                    snapshot_seal_sha256
                )) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository snapshot seal is invalid."
                );
            }

            RepositoryLocalRecord record =
                record_for (repository_id);
            string? previous_sha = record.current_sha;
            string? previous_version = record.version;
            string? previous_seal =
                record.snapshot_seal_sha256;

            record.current_sha = sha;
            record.version = version;
            record.snapshot_seal_sha256 =
                snapshot_seal_sha256;

            try {
                ControlStateNative.set_current_values (
                    state_path,
                    repository_id,
                    sha,
                    version,
                    snapshot_seal_sha256
                );
            } catch (GLib.Error error) {
                record.current_sha = previous_sha;
                record.version = previous_version;
                record.snapshot_seal_sha256 =
                    previous_seal;

                throw new RepositoryError.STORAGE (
                    "Control repository state could not be written: %s".printf (
                        error.message
                    )
                );
            }
        }

        public void set_snapshot_seal (
            string repository_id,
            string expected_sha,
            string snapshot_seal_sha256
        ) throws RepositoryError {
            if (load_status !=
                RepositoryStateLoadStatus.VALID) {
                throw new RepositoryError.STORAGE (
                    "Control repository state authority is not valid and cannot be repaired implicitly."
                );
            }

            if (!sha40_is_valid (expected_sha) ||
                !seal_is_valid (
                    snapshot_seal_sha256
                )) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository snapshot identity or seal is invalid."
                );
            }

            RepositoryLocalRecord record =
                record_for (repository_id);

            if (!record.is_ready () ||
                record.current_sha != expected_sha) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository snapshot seal cannot be attached to a different or unready snapshot."
                );
            }

            string? previous_seal =
                record.snapshot_seal_sha256;
            record.snapshot_seal_sha256 =
                snapshot_seal_sha256;

            try {
                ControlStateNative.set_snapshot_seal_values (
                    state_path,
                    repository_id,
                    expected_sha,
                    snapshot_seal_sha256
                );
            } catch (GLib.Error error) {
                record.snapshot_seal_sha256 =
                    previous_seal;

                throw new RepositoryError.STORAGE (
                    "Control repository snapshot seal could not be written: %s".printf (
                        error.message
                    )
                );
            }
        }
    }
}
