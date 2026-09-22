namespace AskTheModel.Tests {
    private static string new_temp_root (string pattern) {
        try {
            return GLib.DirUtils.make_tmp (pattern);
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static string repeat_char (char value, int count) {
        var builder = new GLib.StringBuilder ();
        for (int i = 0; i < count; i++) {
            builder.append_c (value);
        }
        return builder.str;
    }

    private static string write_flatpak_fixture (
        string root
    ) throws GLib.Error {
        string path = GLib.Path.build_filename (
            root,
            "flatpak-info"
        );
        string contents =
            "[Application]\n" +
            "name=io.github.laurentiustaicu.ask_the_model\n" +
            "runtime=io.elementary.Platform/x86_64/8\n" +
            "\n" +
            "[Instance]\n" +
            "app-commit=%s\n".printf (
                repeat_char ('a', 64)
            ) +
            "app-extensions=\n" +
            "branch=master\n" +
            "arch=x86_64\n" +
            "flatpak-version=1.16.0\n" +
            "runtime-commit=%s\n".printf (
                repeat_char ('b', 64)
            ) +
            "runtime-extensions=\n";

        GLib.FileUtils.set_contents (path, contents);
        return path;
    }

    private static StartupRepositoryOutcome repository_for (
        StartupQualificationReport report,
        string repository_id
    ) {
        foreach (
            StartupRepositoryOutcome repository
            in report.repositories
        ) {
            if (repository.repository_id == repository_id) {
                return repository;
            }
        }
        assert_not_reached ();
    }

    private static Json.Object read_record (
        string path
    ) throws GLib.Error {
        string contents;
        GLib.FileUtils.get_contents (path, out contents);
        var parser = new Json.Parser ();
        parser.load_from_data (contents, -1);
        return parser.get_root ().get_object ();
    }

    private static void test_clean_packaged_install ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);

        var service = new StartupQualificationService (
            flatpak_info,
            data_root,
            cache_root,
            state_root
        );
        StartupQualificationReport report =
            service.run ();

        assert (report.installation_qualified);
        assert (report.platform_qualified);
        assert (report.storage_qualified);
        assert (report.storage_created);
        assert (report.execution_mode == "flatpak");
        assert (
            report.repository_state_status ==
            RepositoryStateLoadStatus.ABSENT
        );
        assert (report.repositories.length == 3);

        foreach (
            StartupRepositoryOutcome repository
            in report.repositories
        ) {
            assert (
                repository.status ==
                StartupRepositoryStatus.NOT_INSTALLED
            );
            assert (
                repository.reason_code ==
                "not_installed"
            );
        }

        Json.Object root = read_record (
            report.record_path
        );
        assert (
            root.get_int_member ("schema_version") == 1
        );
        assert (
            root.get_int_member ("policy_version") == 1
        );
        assert (
            root.get_string_member ("execution_mode") ==
            "flatpak"
        );
        assert (
            root.get_boolean_member ("platform_qualified")
        );
        assert (
            root.get_boolean_member ("storage_qualified")
        );
        assert (
            root.get_string_member (
                "repository_state_load_status"
            ) == "ABSENT"
        );
        assert (!root.has_member ("provider"));

        Json.Array repositories =
            root.get_array_member ("repositories");
        assert (repositories.get_length () == 3);
    }

    private static void test_development_is_unqualified ()
        throws GLib.Error {
        string data_parent = new_temp_root (
            "atm-gs0-dev-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-dev-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-dev-state-XXXXXX"
        );

        var service = new StartupQualificationService (
            "/definitely/not/flatpak-info",
            data_root,
            cache_root,
            state_root
        );
        StartupQualificationReport report =
            service.run ();

        assert (!report.installation_qualified);
        assert (!report.platform_qualified);
        assert (report.storage_qualified);
        assert (report.execution_mode == "development");
        assert (
            report.platform_reason_code ==
            "development_execution"
        );

        foreach (
            StartupRepositoryOutcome repository
            in report.repositories
        ) {
            assert (
                repository.status ==
                StartupRepositoryStatus.BLOCKED
            );
        }
    }

    private static void test_invalid_state_is_preserved ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-invalid-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-invalid-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-invalid-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-invalid-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);
        string state_path = GLib.Path.build_filename (
            state_root,
            "repository-state.json"
        );
        string malformed = "{ not valid json";

        GLib.FileUtils.set_contents (
            state_path,
            malformed
        );

        var service = new StartupQualificationService (
            flatpak_info,
            data_root,
            cache_root,
            state_root
        );
        StartupQualificationReport report =
            service.run ();

        assert (report.installation_qualified);
        assert (
            report.repository_state_status ==
            RepositoryStateLoadStatus.INVALID
        );

        foreach (
            StartupRepositoryOutcome repository
            in report.repositories
        ) {
            assert (
                repository.status ==
                StartupRepositoryStatus.STATE_INVALID
            );
            assert (
                repository.reason_code ==
                "repository_state_invalid"
            );
        }

        string preserved;
        GLib.FileUtils.get_contents (
            state_path,
            out preserved
        );
        assert (preserved == malformed);
    }

    private static void test_missing_snapshot_is_reported ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-missing-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-missing-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-missing-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-missing-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);
        string state_path = GLib.Path.build_filename (
            state_root,
            "repository-state.json"
        );
        string sha =
            "1111111111111111111111111111111111111111";

        string state =
            "{\n" +
            "  \"schema_version\": 1,\n" +
            "  \"repositories\": [\n" +
            "    {\n" +
            "      \"id\": \"rmd\",\n" +
            "      \"sha\": \"%s\",\n".printf (sha) +
            "      \"version\": \"0.1.0\"\n" +
            "    }\n" +
            "  ]\n" +
            "}\n";

        GLib.FileUtils.set_contents (
            state_path,
            state
        );

        var service = new StartupQualificationService (
            flatpak_info,
            data_root,
            cache_root,
            state_root
        );
        StartupQualificationReport report =
            service.run ();

        assert (report.installation_qualified);
        assert (
            report.repository_state_status ==
            RepositoryStateLoadStatus.VALID
        );

        StartupRepositoryOutcome rmd =
            repository_for (report, "rmd");
        assert (
            rmd.status ==
            StartupRepositoryStatus.SNAPSHOT_MISSING
        );
        assert (
            rmd.reason_code == "snapshot_missing"
        );
        assert (rmd.snapshot_sha == sha);
        assert (rmd.persisted_version == "0.1.0");

        assert (
            repository_for (report, "ewd").status ==
            StartupRepositoryStatus.NOT_INSTALLED
        );
        assert (
            repository_for (report, "cbd").status ==
            StartupRepositoryStatus.NOT_INSTALLED
        );
    }

    public static int main (string[] args) {
        GLib.Test.init (ref args);

        GLib.Test.add_func (
            "/gs0/clean-packaged-install",
            () => {
                try {
                    test_clean_packaged_install ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );
        GLib.Test.add_func (
            "/gs0/development-unqualified",
            () => {
                try {
                    test_development_is_unqualified ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );
        GLib.Test.add_func (
            "/gs0/invalid-state-preserved",
            () => {
                try {
                    test_invalid_state_is_preserved ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );
        GLib.Test.add_func (
            "/gs0/missing-snapshot",
            () => {
                try {
                    test_missing_snapshot_is_reported ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );

        return GLib.Test.run ();
    }
}
