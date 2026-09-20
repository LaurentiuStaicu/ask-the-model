namespace AskTheModel {
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

        public RepositoryLocalRecord record_for (
            string repository_id
        ) {
            foreach (RepositoryLocalRecord record in records) {
                if (record.repository_id == repository_id) {
                    return record;
                }
            }

            assert_not_reached ();
        }

        public void set_current (
            string repository_id,
            string sha,
            string version
        ) throws RepositoryError {
            if (!GLib.Regex.match_simple (
                    "^[0-9a-f]{40}$",
                    sha
                )) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Repository state SHA is invalid."
                );
            }

            RepositoryLocalRecord record =
                record_for (repository_id);
            record.current_sha = sha;
            record.version = version;
            save ();
        }

        private void load_best_effort () {
            if (!GLib.FileUtils.test (
                    state_path,
                    GLib.FileTest.EXISTS
                )) {
                return;
            }

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
                if (!root.has_member ("schema_version") ||
                    root.get_int_member ("schema_version") !=
                        SCHEMA_VERSION ||
                    !root.has_member ("repositories")) {
                    return;
                }

                Json.Array repositories =
                    root.get_array_member ("repositories");

                for (
                    uint i = 0;
                    i < repositories.get_length ();
                    i++
                ) {
                    Json.Object item =
                        repositories.get_object_element (i);

                    if (!item.has_member ("id") ||
                        !item.has_member ("sha") ||
                        !item.has_member ("version")) {
                        continue;
                    }

                    string id = item.get_string_member ("id");
                    string sha = item.get_string_member ("sha");
                    string version =
                        item.get_string_member ("version");

                    if (!GLib.Regex.match_simple (
                            "^[0-9a-f]{40}$",
                            sha
                        )) {
                        continue;
                    }

                    foreach (
                        RepositoryLocalRecord record
                        in records
                    ) {
                        if (record.repository_id == id) {
                            record.current_sha = sha;
                            record.version = version;
                            break;
                        }
                    }
                }
            } catch (GLib.Error error) {
                stderr.printf (
                    "AtM: repository state ignored: %s\n",
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
            string temporary_path = state_path + ".part";

            try {
                GLib.FileUtils.set_contents (
                    temporary_path,
                    data
                );

                if (GLib.FileUtils.rename (
                        temporary_path,
                        state_path
                    ) != 0) {
                    throw new RepositoryError.STORAGE (
                        "Repository state could not be promoted atomically."
                    );
                }
            } catch (GLib.Error error) {
                GLib.FileUtils.remove (temporary_path);

                throw new RepositoryError.STORAGE (
                    "Repository state could not be written: %s".printf (
                        error.message
                    )
                );
            }
        }
    }
}
