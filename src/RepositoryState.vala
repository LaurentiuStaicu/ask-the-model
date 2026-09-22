namespace AskTheModel {
    public enum RepositoryStateLoadStatus {
        ABSENT,
        VALID,
        INVALID
    }

    public class RepositoryLocalRecord : Object {
        public string repository_id { get; construct; }
        public string? current_sha { get; set; }
        public string? version { get; set; }
        public string? snapshot_seal_sha256 { get; set; }

        public RepositoryLocalRecord (string repository_id) {
            Object (repository_id: repository_id);
        }

        public bool is_ready () {
            return current_sha != null && version != null;
        }

        public bool has_snapshot_seal () {
            return snapshot_seal_sha256 != null;
        }
    }

    public class RepositoryStateStore : Object {
        private const int CURRENT_SCHEMA_VERSION = 2;
        private const int LEGACY_SCHEMA_VERSION = 1;

        private string state_path;
        private RepositoryLocalRecord[] records = {};

        public RepositoryStateLoadStatus load_status {
            get;
            private set;
            default = RepositoryStateLoadStatus.ABSENT;
        }

        public int loaded_schema_version {
            get;
            private set;
            default = 0;
        }

        public RepositoryStateStore (string? state_root = null) {
            string root = state_root ??
                GLib.Environment.get_user_state_dir ();
            state_path = GLib.Path.build_filename (
                root,
                "repository-state.json"
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

        public void set_current (
            string repository_id,
            string sha,
            string version,
            string? snapshot_seal_sha256 = null
        ) throws RepositoryError {
            if (load_status == RepositoryStateLoadStatus.INVALID) {
                throw new RepositoryError.STORAGE (
                    "Repository state is invalid and cannot be overwritten implicitly."
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
                !seal_is_valid (snapshot_seal_sha256)) {
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
                save ();
                load_status = RepositoryStateLoadStatus.VALID;
                loaded_schema_version =
                    CURRENT_SCHEMA_VERSION;
            } catch (RepositoryError error) {
                record.current_sha = previous_sha;
                record.version = previous_version;
                record.snapshot_seal_sha256 =
                    previous_seal;
                throw error;
            }
        }

        public void set_snapshot_seal (
            string repository_id,
            string expected_sha,
            string snapshot_seal_sha256
        ) throws RepositoryError {
            if (load_status == RepositoryStateLoadStatus.INVALID) {
                throw new RepositoryError.STORAGE (
                    "Repository state is invalid and cannot be repaired implicitly."
                );
            }

            if (!sha40_is_valid (expected_sha) ||
                !seal_is_valid (snapshot_seal_sha256)) {
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
                save ();
                load_status = RepositoryStateLoadStatus.VALID;
                loaded_schema_version =
                    CURRENT_SCHEMA_VERSION;
            } catch (RepositoryError error) {
                record.snapshot_seal_sha256 =
                    previous_seal;
                throw error;
            }
        }

        private static bool node_is_string (
            Json.Node? node
        ) {
            return node != null &&
                node.get_node_type () == Json.NodeType.VALUE &&
                node.get_value_type () == typeof (string);
        }

        private static bool node_is_int64 (
            Json.Node? node
        ) {
            return node != null &&
                node.get_node_type () == Json.NodeType.VALUE &&
                node.get_value_type () == typeof (int64);
        }

        private static bool node_is_null (
            Json.Node? node
        ) {
            return node != null &&
                node.get_node_type () == Json.NodeType.NULL;
        }

        private void load_best_effort () {
            load_status = RepositoryStateLoadStatus.ABSENT;
            loaded_schema_version = 0;

            if (!GLib.FileUtils.test (
                    state_path,
                    GLib.FileTest.EXISTS
                )) {
                return;
            }

            load_status = RepositoryStateLoadStatus.INVALID;

            try {
                string contents;
                GLib.FileUtils.get_contents (
                    state_path,
                    out contents
                );

                var parser = new Json.Parser ();
                parser.load_from_data (contents, -1);

                Json.Node root_node = parser.get_root ();
                if (root_node.get_node_type () !=
                    Json.NodeType.OBJECT) {
                    return;
                }

                Json.Object root = root_node.get_object ();
                Json.Node? schema_node =
                    root.get_member ("schema_version");
                Json.Node? repositories_node =
                    root.get_member ("repositories");

                if (!node_is_int64 (schema_node) ||
                    repositories_node == null ||
                    repositories_node.get_node_type () !=
                        Json.NodeType.ARRAY) {
                    return;
                }

                int schema_version =
                    (int) schema_node.get_int ();
                if (schema_version != LEGACY_SCHEMA_VERSION &&
                    schema_version != CURRENT_SCHEMA_VERSION) {
                    return;
                }

                Json.Array repositories =
                    repositories_node.get_array ();
                string[] ids = {};
                string[] shas = {};
                string[] versions = {};
                string?[] seals = {};

                for (
                    uint i = 0;
                    i < repositories.get_length ();
                    i++
                ) {
                    Json.Node? item_node =
                        repositories.get_element (i);

                    if (item_node == null ||
                        item_node.get_node_type () !=
                            Json.NodeType.OBJECT) {
                        return;
                    }

                    Json.Object item = item_node.get_object ();
                    Json.Node? id_node = item.get_member ("id");
                    Json.Node? sha_node = item.get_member ("sha");
                    Json.Node? version_node =
                        item.get_member ("version");

                    if (!node_is_string (id_node) ||
                        !node_is_string (sha_node) ||
                        !node_is_string (version_node)) {
                        return;
                    }

                    string id = id_node.get_string ();
                    string sha = sha_node.get_string ();
                    string version = version_node.get_string ();
                    string? seal = null;

                    if (schema_version ==
                        CURRENT_SCHEMA_VERSION) {
                        Json.Node? seal_node =
                            item.get_member (
                                "snapshot_seal_sha256"
                            );

                        if (seal_node == null) {
                            return;
                        }

                        if (node_is_string (seal_node)) {
                            seal = seal_node.get_string ();
                            if (!seal_is_valid (seal)) {
                                return;
                            }
                        } else if (!node_is_null (seal_node)) {
                            return;
                        }
                    }

                    if (record_for_optional (id) == null ||
                        !sha40_is_valid (sha) ||
                        version.length == 0) {
                        return;
                    }

                    foreach (string seen_id in ids) {
                        if (seen_id == id) {
                            return;
                        }
                    }

                    ids += id;
                    shas += sha;
                    versions += version;
                    seals += seal;
                }

                for (int i = 0; i < ids.length; i++) {
                    RepositoryLocalRecord? record =
                        record_for_optional (ids[i]);

                    if (record == null) {
                        return;
                    }

                    record.current_sha = shas[i];
                    record.version = versions[i];
                    record.snapshot_seal_sha256 = seals[i];
                }

                loaded_schema_version = schema_version;
                load_status = RepositoryStateLoadStatus.VALID;
            } catch (GLib.Error error) {
                stderr.printf (
                    "AtM: repository state invalid: %s\n",
                    error.message
                );
            }
        }

        private void save () throws RepositoryError {
            string? parent = GLib.Path.get_dirname (
                state_path
            );

            if (parent == null ||
                GLib.DirUtils.create_with_parents (
                    parent,
                    0700
                ) != 0) {
                throw new RepositoryError.STORAGE (
                    "Repository state directory could not be created."
                );
            }

            var builder = new Json.Builder ();
            builder.begin_object ();
            builder.set_member_name ("schema_version");
            builder.add_int_value (CURRENT_SCHEMA_VERSION);
            builder.set_member_name ("repositories");
            builder.begin_array ();

            foreach (RepositoryLocalRecord record in records) {
                if (!record.is_ready ()) {
                    continue;
                }

                builder.begin_object ();
                builder.set_member_name ("id");
                builder.add_string_value (
                    record.repository_id
                );
                builder.set_member_name ("sha");
                builder.add_string_value (
                    record.current_sha ?? ""
                );
                builder.set_member_name ("version");
                builder.add_string_value (
                    record.version ?? ""
                );
                builder.set_member_name (
                    "snapshot_seal_sha256"
                );

                if (record.snapshot_seal_sha256 == null) {
                    builder.add_null_value ();
                } else {
                    builder.add_string_value (
                        record.snapshot_seal_sha256
                    );
                }

                builder.end_object ();
            }

            builder.end_array ();
            builder.end_object ();

            var generator = new Json.Generator ();
            generator.set_root (builder.get_root ());
            generator.pretty = true;

            string data = generator.to_data (null);

            try {
                GLib.FileUtils.set_contents_full (
                    state_path,
                    data,
                    -1,
                    GLib.FileSetContentsFlags.CONSISTENT |
                        GLib.FileSetContentsFlags.DURABLE,
                    0600
                );
            } catch (GLib.FileError error) {
                throw new RepositoryError.STORAGE (
                    "Repository state could not be written: %s".printf (
                        error.message
                    )
                );
            }
        }
    }
}
