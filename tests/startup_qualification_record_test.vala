namespace AskTheModel.Tests {
    private static string new_temp_root () {
        try {
            return GLib.DirUtils.make_tmp (
                "atm-startup-record-test-XXXXXX"
            );
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static string record_path (string root) {
        return GLib.Path.build_filename (
            root,
            "startup-qualification.json"
        );
    }

    public static int main (string[] args) {
        string root = new_temp_root ();

        try {
            var record = new StartupQualificationRecord (
                1,
                "2026-09-22T06:39:00Z",
                "flatpak",
                true,
                true,
                "VALID",
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "io.github.laurentiustaicu.ask_the_model",
                "app/io.github.laurentiustaicu.ask_the_model/x86_64/master",
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "io.elementary.Platform/x86_64/8",
                "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                "x86_64",
                "master",
                "1.16.0",
                root
            );

            record.add_repository (
                new StartupRepositoryQualification (
                    "ewd",
                    "READY",
                    "ready",
                    "1111111111111111111111111111111111111111",
                    "0.1.0",
                    "/cache/retrieval/ewd/index.sqlite"
                )
            );
            record.add_repository (
                new StartupRepositoryQualification (
                    "cbd",
                    "NOT_INSTALLED",
                    "not_installed"
                )
            );
            record.add_repository (
                new StartupRepositoryQualification (
                    "rmd",
                    "READY_REPAIRED_INDEX",
                    "index_rebuilt",
                    "2222222222222222222222222222222222222222",
                    "0.1.0",
                    "/cache/retrieval/rmd/index.sqlite"
                )
            );

            record.save ();

            string contents;
            GLib.FileUtils.get_contents (
                record_path (root),
                out contents
            );

            var parser = new Json.Parser ();
            parser.load_from_data (contents, -1);
            Json.Node root_node = parser.get_root ();
            assert (
                root_node.get_node_type () ==
                Json.NodeType.OBJECT
            );

            Json.Object object = root_node.get_object ();
            assert (
                object.get_int_member ("schema_version") == 1
            );
            assert (
                object.get_int_member ("policy_version") == 1
            );
            assert (
                object.get_string_member ("execution_mode") ==
                "flatpak"
            );
            assert (
                object.get_boolean_member ("platform_qualified")
            );
            assert (
                object.get_boolean_member ("storage_qualified")
            );
            assert (
                object.get_string_member (
                    "repository_state_load_status"
                ) == "VALID"
            );

            Json.Object deployment =
                object.get_object_member ("deployment");
            assert (
                deployment.get_string_member (
                    "application_id"
                ) ==
                "io.github.laurentiustaicu.ask_the_model"
            );
            assert (
                deployment.get_string_member ("architecture") ==
                "x86_64"
            );

            Json.Array repositories =
                object.get_array_member ("repositories");
            assert (repositories.get_length () == 3);

            Json.Object ewd =
                repositories.get_object_element (0);
            assert (ewd.get_string_member ("id") == "ewd");
            assert (
                ewd.get_string_member ("status") == "READY"
            );
            assert (
                ewd.get_string_member ("reason_code") == "ready"
            );

            Json.Object cbd =
                repositories.get_object_element (1);
            assert (
                cbd.get_string_member ("status") ==
                "NOT_INSTALLED"
            );
            assert (
                cbd.get_member ("sha").get_node_type () ==
                Json.NodeType.NULL
            );

            GLib.FileUtils.remove (record_path (root));
            GLib.DirUtils.remove (root);
            GLib.FileUtils.set_contents (
                root,
                "blocking-file"
            );

            bool failed = false;
            try {
                record.save ();
            } catch (RepositoryError error) {
                failed =
                    error.code == RepositoryError.STORAGE;
            }
            assert (failed);

            string blocking;
            GLib.FileUtils.get_contents (root, out blocking);
            assert (blocking == "blocking-file");

            GLib.FileUtils.remove (root);
            root = new_temp_root ();

            var development = new StartupQualificationRecord (
                1,
                "2026-09-22T06:40:00Z",
                "development",
                false,
                true,
                "ABSENT",
                null,
                null,
                null,
                null,
                null,
                null,
                null,
                null,
                null,
                root
            );
            development.add_repository (
                new StartupRepositoryQualification (
                    "ewd",
                    "NOT_INSTALLED",
                    "not_installed"
                )
            );
            development.save ();

            GLib.FileUtils.get_contents (
                record_path (root),
                out contents
            );
            parser.load_from_data (contents, -1);
            object = parser.get_root ().get_object ();
            assert (
                object.get_string_member ("execution_mode") ==
                "development"
            );
            assert (
                !object.get_boolean_member (
                    "platform_qualified"
                )
            );
            assert (
                object.get_member (
                    "platform_fingerprint"
                ).get_node_type () == Json.NodeType.NULL
            );

            GLib.FileUtils.remove (record_path (root));
            GLib.DirUtils.remove (root);

        } catch (GLib.Error error) {
            stderr.printf ("%s\n", error.message);
            return 1;
        }

        return 0;
    }
}
