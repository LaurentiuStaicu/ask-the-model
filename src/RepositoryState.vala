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

        public RepositoryLocalRecord (string repository_id) {
            Object (repository_id: repository_id);
        }

        public bool is_ready () {
            return current_sha != null && version != null;
        }
    }

    public class RepositoryStateStore : Object {
        private const int SCHEMA_VERSION = 1;

        private string state_path;
        private RepositoryLocalRecord[] records = {};

        public RepositoryStateLoadStatus load_status {
            get;
            private set;
            default = RepositoryStateLoadStatus.ABSENT;
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

        public void set_current (
            string repository_id,
            string sha,
            string version
        ) throws RepositoryError {
            if (load_status == RepositoryStateLoadStatus.INVALID) {
                throw new RepositoryError.STORAGE (
                    "Repository state is invalid and cannot be overwritten implicitly."
                );
            }

            if (!GLib.Regex.match_simple (
                    "^[0-9a-f]{40}$",
                    sha
                )) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository state SHA is invalid."
                );
            }

            if (version.length == 0) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository state version is empty."
                );
            }

            RepositoryLocalRecord record =
                record_for (repository_id);
            string? previous_sha = record.current_sha;
            string? previous_version = record.version;

            record.current_sha = sha;
            record.version = version;

            try {
                save ();
                load_status = RepositoryStateLoadStatus.VALID;
            } catch (RepositoryError error) {
                record.current_sha = previous_sha;
                record.version = previous_version;
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

        private void load_best_effort () {
            load_status = RepositoryStateLoadStatus.ABSENT;

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
                    schema_node.get_int () != SCHEMA_VERSION ||
                    repositories_node == null ||
                    repositories_node.get_node_type () !=
                        Json.NodeType.ARRAY) {
                    return;
                }

                Json.Array repositories =
                    repositories_node.get_array ();
                string[] ids = {};
                string[] shas = {};
                string[] versions = {};

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

                    if (record_for_optional (id) == null ||
                        !GLib.Regex.match_simple (
                            "^[0-9a-f]{40}$",
                            sha
                        ) ||
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
                }

                for (int i = 0; i < ids.length; i++) {
                    RepositoryLocalRecord? record =
                        record_for_optional (ids[i]);

                    if (record == null) {
                        return;
                    }

                    record.current_sha = shas[i];
                    record.version = versions[i];
                }

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
            builder.add_int_value (SCHEMA_VERSION);
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
