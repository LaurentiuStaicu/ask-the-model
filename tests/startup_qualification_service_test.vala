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
            "runtime=runtime/io.elementary.Platform/x86_64/8\n" +
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

    private static string repository_snapshot_path (
        string data_root,
        string repository_id,
        string sha
    ) {
        return GLib.Path.build_filename (
            data_root,
            "Repositories",
            repository_id,
            "snapshots",
            sha
        );
    }

    private static string create_valid_snapshot (
        string data_root,
        string repository_id,
        string acronym,
        string display_name,
        string sha,
        string version
    ) throws GLib.Error {
        string root = repository_snapshot_path (
            data_root,
            repository_id,
            sha
        );
        string atm = GLib.Path.build_filename (
            root,
            ".atm"
        );
        string model = GLib.Path.build_filename (
            root,
            "model"
        );

        assert (
            GLib.DirUtils.create_with_parents (
                atm,
                0700
            ) == 0
        );
        assert (
            GLib.DirUtils.create_with_parents (
                model,
                0700
            ) == 0
        );

        string citation = GLib.Path.build_filename (
            root,
            "CITATION.cff"
        );
        string status = GLib.Path.build_filename (
            root,
            "STATUS.md"
        );
        string readme = GLib.Path.build_filename (
            root,
            "README.md"
        );
        string core = GLib.Path.build_filename (
            root,
            "model",
            "core.json"
        );
        string manifest_path = GLib.Path.build_filename (
            root,
            ".atm",
            "repository.json"
        );

        string citation_text =
            "cff-version: 1.2.0\n" +
            "message: cite this\n" +
            "type: software\n" +
            "title: Test\n" +
            "version: %s\n".printf (version);

        string manifest =
            "{\n" +
            "  \"schema_version\": 1,\n" +
            "  \"repository_id\": \"%s\",\n".printf (
                repository_id
            ) +
            "  \"acronym\": \"%s\",\n".printf (
                acronym
            ) +
            "  \"display_name\": \"%s\",\n".printf (
                display_name
            ) +
            "  \"version_source\": {" +
            "\"type\": \"cff\", " +
            "\"path\": \"CITATION.cff\"},\n" +
            "  \"status_source\": \"STATUS.md\",\n" +
            "  \"required_paths\": [" +
            "\"CITATION.cff\", " +
            "\"STATUS.md\", " +
            "\"model/core.json\"],\n" +
            "  \"retrieval\": {\n" +
            "    \"canonical\": [" +
            "\"STATUS.md\", " +
            "\"README.md\", " +
            "\"CITATION.cff\"],\n" +
            "    \"structural\": [" +
            "\"model/core.json\"],\n" +
            "    \"evidence\": [],\n" +
            "    \"tabular\": [],\n" +
            "    \"implementation\": [],\n" +
            "    \"exclude\": [" +
            "\".github\", " +
            "\"__pycache__\"]\n" +
            "  }\n" +
            "}\n";

        GLib.FileUtils.set_contents (
            citation,
            citation_text
        );
        GLib.FileUtils.set_contents (
            status,
            "# Status\nValidated test status.\n"
        );
        GLib.FileUtils.set_contents (
            readme,
            "# Test repository\nGrounded evidence.\n"
        );
        GLib.FileUtils.set_contents (
            core,
            "{\"entity\": \"test\"}\n"
        );
        GLib.FileUtils.set_contents (
            manifest_path,
            manifest
        );

        return root;
    }

    private static void write_repository_state_v1 (
        string state_root,
        string repository_id,
        string sha,
        string version
    ) throws GLib.Error {
        string state_path = GLib.Path.build_filename (
            state_root,
            "repository-state.json"
        );
        string state =
            "{\n" +
            "  \"schema_version\": 1,\n" +
            "  \"repositories\": [\n" +
            "    {\n" +
            "      \"id\": \"%s\",\n".printf (
                repository_id
            ) +
            "      \"sha\": \"%s\",\n".printf (
                sha
            ) +
            "      \"version\": \"%s\"\n".printf (
                version
            ) +
            "    }\n" +
            "  ]\n" +
            "}\n";

        GLib.FileUtils.set_contents (
            state_path,
            state
        );
    }

    private static void write_repository_state_v2 (
        string state_root,
        string repository_id,
        string sha,
        string version,
        string seal
    ) throws GLib.Error {
        string state_path = GLib.Path.build_filename (
            state_root,
            "repository-state.json"
        );
        string state =
            "{\n" +
            "  \"schema_version\": 2,\n" +
            "  \"repositories\": [\n" +
            "    {\n" +
            "      \"id\": \"%s\",\n".printf (
                repository_id
            ) +
            "      \"sha\": \"%s\",\n".printf (
                sha
            ) +
            "      \"version\": \"%s\",\n".printf (
                version
            ) +
            "      \"snapshot_seal_sha256\": \"%s\"\n".printf (
                seal
            ) +
            "    }\n" +
            "  ]\n" +
            "}\n";

        GLib.FileUtils.set_contents (
            state_path,
            state
        );
    }

    private static bool directory_is_empty (
        string path
    ) throws GLib.Error {
        var directory = GLib.Dir.open (path);
        return directory.read_name () == null;
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
            RepositoryStateLoadStatus.VALID
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
            ) == "VALID"
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

        assert (!report.installation_qualified);
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

    private static void test_legacy_snapshot_enrolls_seal ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-seal-legacy-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-seal-legacy-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-seal-legacy-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-seal-legacy-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);
        string sha =
            "3333333333333333333333333333333333333333";

        create_valid_snapshot (
            data_root,
            "rmd",
            "RMD",
            "Romanian Monetary Dynamics",
            sha,
            "0.1.0"
        );
        write_repository_state_v1 (
            state_root,
            "rmd",
            sha,
            "0.1.0"
        );

        var first_service =
            new StartupQualificationService (
                flatpak_info,
                data_root,
                cache_root,
                state_root
            );
        StartupQualificationReport first =
            first_service.run ();

        StartupRepositoryOutcome rmd =
            repository_for (first, "rmd");
        assert (
            rmd.status ==
            StartupRepositoryStatus.READY_REPAIRED_INDEX
        );

        var migrated =
            new ControlRepositoryStateStore (
                state_root
            );
        assert (
            migrated.load_status ==
            RepositoryStateLoadStatus.VALID
        );
        assert (
            migrated.control_schema_version == 1
        );
        assert (
            migrated.record_for (
                "rmd"
            ).snapshot_seal_sha256 != null
        );
        assert (
            migrated.record_for (
                "rmd"
            ).snapshot_seal_sha256.length == 64
        );

        var legacy_source =
            new RepositoryStateStore (
                state_root
            );
        assert (
            legacy_source.loaded_schema_version == 1
        );
        assert (
            legacy_source.record_for (
                "rmd"
            ).snapshot_seal_sha256 == null
        );

        var second_service =
            new StartupQualificationService (
                flatpak_info,
                data_root,
                cache_root,
                state_root
            );
        StartupQualificationReport second =
            second_service.run ();

        assert (
            repository_for (
                second,
                "rmd"
            ).status == StartupRepositoryStatus.READY
        );
    }

    private static void test_seal_mismatch_precedes_index_rebuild ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-seal-bad-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-seal-bad-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-seal-bad-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-seal-bad-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);
        string sha =
            "4444444444444444444444444444444444444444";
        string wrong_seal =
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

        create_valid_snapshot (
            data_root,
            "rmd",
            "RMD",
            "Romanian Monetary Dynamics",
            sha,
            "0.1.0"
        );
        write_repository_state_v2 (
            state_root,
            "rmd",
            sha,
            "0.1.0",
            wrong_seal
        );

        assert (directory_is_empty (cache_root));

        var service = new StartupQualificationService (
            flatpak_info,
            data_root,
            cache_root,
            state_root
        );
        StartupQualificationReport report =
            service.run ();

        StartupRepositoryOutcome rmd =
            repository_for (report, "rmd");
        assert (
            rmd.status ==
            StartupRepositoryStatus.SNAPSHOT_INVALID
        );
        assert (
            rmd.reason_code ==
            "snapshot_seal_mismatch"
        );

        assert (directory_is_empty (cache_root));

        var preserved =
            new ControlRepositoryStateStore (
                state_root
            );
        assert (
            preserved.record_for (
                "rmd"
            ).snapshot_seal_sha256 == wrong_seal
        );
    }

    private static void
    test_existing_control_db_wins_over_legacy_json ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-authority-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-authority-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-authority-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-authority-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);
        string sha =
            "7777777777777777777777777777777777777777";

        write_repository_state_v1 (
            state_root,
            "rmd",
            sha,
            "0.1.0"
        );

        var first_service =
            new StartupQualificationService (
                flatpak_info,
                data_root,
                cache_root,
                state_root
            );
        StartupQualificationReport first =
            first_service.run ();

        assert (first.installation_qualified);
        assert (
            first.repository_state_status ==
            RepositoryStateLoadStatus.VALID
        );
        assert (
            repository_for (
                first,
                "rmd"
            ).snapshot_sha == sha
        );

        string legacy_path =
            GLib.Path.build_filename (
                state_root,
                "repository-state.json"
            );
        string malformed = "{ legacy is now invalid";
        GLib.FileUtils.set_contents (
            legacy_path,
            malformed
        );

        var second_service =
            new StartupQualificationService (
                flatpak_info,
                data_root,
                cache_root,
                state_root
            );
        StartupQualificationReport second =
            second_service.run ();

        assert (second.installation_qualified);
        assert (
            second.repository_state_status ==
            RepositoryStateLoadStatus.VALID
        );
        assert (
            repository_for (
                second,
                "rmd"
            ).snapshot_sha == sha
        );

        string preserved;
        GLib.FileUtils.get_contents (
            legacy_path,
            out preserved
        );
        assert (preserved == malformed);
    }

    private static void
    test_invalid_control_db_blocks_legacy_fallback ()
        throws GLib.Error {
        string fixture_root = new_temp_root (
            "atm-gs0-db-invalid-fixture-XXXXXX"
        );
        string data_parent = new_temp_root (
            "atm-gs0-db-invalid-data-XXXXXX"
        );
        string data_root = GLib.Path.build_filename (
            data_parent,
            "Ask the Model"
        );
        string cache_root = new_temp_root (
            "atm-gs0-db-invalid-cache-XXXXXX"
        );
        string state_root = new_temp_root (
            "atm-gs0-db-invalid-state-XXXXXX"
        );
        string flatpak_info =
            write_flatpak_fixture (fixture_root);
        string sha =
            "8888888888888888888888888888888888888888";

        write_repository_state_v1 (
            state_root,
            "rmd",
            sha,
            "0.1.0"
        );

        string control_path =
            GLib.Path.build_filename (
                state_root,
                "control-state.sqlite3"
            );
        string corrupt = "not a sqlite database";
        GLib.FileUtils.set_contents (
            control_path,
            corrupt
        );

        var service =
            new StartupQualificationService (
                flatpak_info,
                data_root,
                cache_root,
                state_root
            );
        StartupQualificationReport report =
            service.run ();

        assert (!report.installation_qualified);
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
        }

        string preserved_control;
        GLib.FileUtils.get_contents (
            control_path,
            out preserved_control
        );
        assert (preserved_control == corrupt);

        var legacy =
            new RepositoryStateStore (
                state_root
            );
        assert (
            legacy.load_status ==
            RepositoryStateLoadStatus.VALID
        );
        assert (
            legacy.record_for (
                "rmd"
            ).current_sha == sha
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
        GLib.Test.add_func (
            "/gs0/seal/legacy-enrollment",
            () => {
                try {
                    test_legacy_snapshot_enrolls_seal ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );
        GLib.Test.add_func (
            "/gs0/seal/mismatch-before-index",
            () => {
                try {
                    test_seal_mismatch_precedes_index_rebuild ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );
        GLib.Test.add_func (
            "/gs0/authority/db-wins-over-legacy",
            () => {
                try {
                    test_existing_control_db_wins_over_legacy_json ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );
        GLib.Test.add_func (
            "/gs0/authority/invalid-db-no-fallback",
            () => {
                try {
                    test_invalid_control_db_blocks_legacy_fallback ();
                } catch (GLib.Error error) {
                    GLib.error ("%s", error.message);
                }
            }
        );

        return GLib.Test.run ();
    }
}
