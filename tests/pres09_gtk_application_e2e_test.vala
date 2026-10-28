using GLib;
using Soup;

namespace AskTheModel {

private const string EWD_INITIAL_SNAPSHOT_SHA =
    "d9e249339663015f6d1c05752338a955bf64ad0b";
private const string EWD_CURRENT_SNAPSHOT_SHA =
    "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
private const string BNR_SNAPSHOT_SHA =
    "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170";
private const string TEST_MODEL = "test-model";
private const string TEST_DIGEST = "test-digest";

private static void write_json_response (
    Soup.ServerMessage message,
    string body
) {
    message.set_status (Soup.Status.OK, null);
    message.set_response (
        "application/json",
        Soup.MemoryUse.COPY,
        body.data
    );
}

private static void tags_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"models\":[{\"name\":\"test-model\",\"digest\":\"test-digest\"}]}"
    );
}

private static void show_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"capabilities\":[\"completion\"]}"
    );
}

private static void chat_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"message\":{\"content\":\"The pinned repository evidence supports the requested trend. [S1]\"},\"done\":true}\n"
    );
}

private static void generate_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"response\":\"Chart E2E\",\"done\":true}"
    );
}

private static void copy_tree (
    File source,
    File destination
) throws GLib.Error {
    FileInfo info = source.query_info (
        "standard::type",
        FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
        null
    );

    if (info.get_file_type () == FileType.DIRECTORY) {
        destination.make_directory_with_parents (null);

        FileEnumerator enumerator = source.enumerate_children (
            "standard::name,standard::type",
            FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
            null
        );

        FileInfo? child_info;
        while ((child_info = enumerator.next_file (null)) != null) {
            string name = child_info.get_name ();
            /* A checked-out test repository contains .git metadata which is
             * not part of the pinned scientific snapshot or its manifest. */
            if (name == ".git") {
                continue;
            }
            copy_tree (
                source.get_child (name),
                destination.get_child (name)
            );
        }

        enumerator.close (null);
        return;
    }

    if (info.get_file_type () != FileType.REGULAR) {
        throw new GLib.IOError.NOT_SUPPORTED (
            "PRES-09 fixture contains a non-regular file."
        );
    }

    source.copy (
        destination,
        FileCopyFlags.ALL_METADATA,
        null,
        null
    );
}

private static void remove_tree_best_effort (File root) {
    try {
        FileInfo info = root.query_info (
            "standard::type",
            FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
            null
        );
        if (info.get_file_type () == FileType.DIRECTORY) {
            FileEnumerator enumerator = root.enumerate_children (
                "standard::name,standard::type",
                FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
                null
            );
            FileInfo? child_info;
            while ((child_info = enumerator.next_file (null)) != null) {
                remove_tree_best_effort (
                    root.get_child (child_info.get_name ())
                );
            }
            enumerator.close (null);
        }
        root.delete (null);
    } catch (GLib.Error error) {
        /* Best-effort cleanup only; never mask the E2E result. */
    }
}

private static int run_child_phase (
    string executable_path,
    string phase
) {
    string[] child_args = {
        executable_path,
        "--phase=" + phase
    };
    string standard_output = "";
    string standard_error = "";
    int wait_status = 0;

    try {
        bool spawned = Process.spawn_sync (
            null,
            child_args,
            null,
            SpawnFlags.SEARCH_PATH,
            null,
            out standard_output,
            out standard_error,
            out wait_status
        );

        if (!spawned || wait_status != 0) {
            stderr.printf (
                "PRES09-GTK child '%s' failed (wait status %d).\nstdout:\n%s\nstderr:\n%s\n",
                phase,
                wait_status,
                standard_output,
                standard_error
            );
            return 1;
        }

        stdout.printf (
            "PRES09-GTK child '%s' output:\n%s",
            phase,
            standard_output
        );
        if (standard_error.length > 0) {
            stderr.printf (
                "PRES09-GTK child '%s' stderr:\n%s",
                phase,
                standard_error
            );
        }
        return 0;
    } catch (GLib.Error error) {
        stderr.printf (
            "Could not launch PRES-09 child '%s': %s\n",
            phase,
            error.message
        );
        return 1;
    }
}

private static int run_snapshot_scenario (
    string raw_executable_path,
    string ewd_snapshot_sha,
    string? full_ewd_root_override
) {
    string executable_path = raw_executable_path;
    if (!GLib.Path.is_absolute (executable_path)) {
        executable_path = GLib.Path.build_filename (
            Environment.get_current_dir (),
            executable_path
        );
    }

    string? fixture_env =
        Environment.get_variable ("ATM_CHART01_FIXTURE");
    if (fixture_env == null ||
        fixture_env.strip ().length == 0) {
        stderr.printf (
            "ATM_CHART01_FIXTURE is required for the PRES-09 GTK application test.\n"
        );
        return 1;
    }

    string root;
    try {
        root = DirUtils.make_tmp (
            "atm-pres09-gtk-application-XXXXXX"
        );
    } catch (GLib.Error error) {
        stderr.printf ("Could not create test root: %s\n", error.message);
        return 1;
    }

    string home = GLib.Path.build_filename (root, "home");
    string state = GLib.Path.build_filename (root, "state");
    string cache = GLib.Path.build_filename (root, "cache");
    string config = GLib.Path.build_filename (root, "config");
    string data_root = GLib.Path.build_filename (
        home,
        "Ask the Model"
    );
    string snapshot_root = GLib.Path.build_filename (
        data_root,
        "Repositories",
        "ewd",
        "snapshots",
        ewd_snapshot_sha
    );
    string bnr_snapshot_root = GLib.Path.build_filename (
        data_root,
        "Repositories",
        "rmd",
        "snapshots",
        BNR_SNAPSHOT_SHA
    );
    string state_db = GLib.Path.build_filename (
        state,
        "control-state.sqlite3"
    );
    string flatpak_info = GLib.Path.build_filename (
        root,
        "flatpak-info"
    );

    try {
        File.new_for_path (home).make_directory_with_parents (null);
        File.new_for_path (state).make_directory_with_parents (null);
        File.new_for_path (cache).make_directory_with_parents (null);
        File.new_for_path (config).make_directory_with_parents (null);

        string? full_ewd_root = full_ewd_root_override;
        string source_snapshot;
        if (full_ewd_root != null &&
            full_ewd_root.strip ().length > 0) {
            source_snapshot = GLib.Path.is_absolute (full_ewd_root)
                ? full_ewd_root
                : GLib.Path.build_filename (
                    Environment.get_current_dir (),
                    full_ewd_root
                );
        } else {
            source_snapshot = GLib.Path.build_filename (
                fixture_env,
                ewd_snapshot_sha == EWD_INITIAL_SNAPSHOT_SHA
                    ? "ewd"
                    : "ewd-current"
            );
        }
        copy_tree (
            File.new_for_path (source_snapshot),
            File.new_for_path (snapshot_root)
        );

        string? full_rmd_root =
            Environment.get_variable ("ATM_PRES09_FULL_RMD_ROOT");
        if (full_rmd_root == null || full_rmd_root.strip ().length == 0) {
            stderr.printf ("ATM_PRES09_FULL_RMD_ROOT is required for the BNR C01 application E2E.\\n");
            remove_tree_best_effort (File.new_for_path (root));
            return 1;
        }
        string source_rmd_snapshot = GLib.Path.is_absolute (full_rmd_root)
            ? full_rmd_root
            : GLib.Path.build_filename (
                Environment.get_current_dir (),
                full_rmd_root
            );
        copy_tree (
            File.new_for_path (source_rmd_snapshot),
            File.new_for_path (bnr_snapshot_root)
        );

        string app_commit = string.nfill (64, 'a');
        string runtime_commit = string.nfill (64, 'b');
        string app_extension_commit = string.nfill (64, 'c');
        string runtime_extension_commit = string.nfill (64, 'd');
        string flatpak_text =
            "[Application]\n" +
            "name=io.github.laurentiustaicu.ask_the_model\n" +
            "runtime=runtime/io.elementary.Platform/x86_64/8\n\n" +
            "[Instance]\n" +
            "app-commit=%s\n".printf (app_commit) +
            "app-extensions=org.example.Locale=%s;\n".printf (app_extension_commit) +
            "branch=master\n" +
            "arch=x86_64\n" +
            "flatpak-version=1.14.0\n" +
            "runtime-commit=%s\n".printf (runtime_commit) +
            "runtime-extensions=org.example.Runtime=%s;\n".printf (runtime_extension_commit);
        FileUtils.set_contents (flatpak_info, flatpak_text);

        string seal;
        uint64 file_count;
        uint64 total_bytes;
        if (!RepositoryNative.compute_snapshot_seal (
                snapshot_root,
                out seal,
                out file_count,
                out total_bytes
            )) {
            stderr.printf ("PRES-09 could not compute fixture EWD snapshot seal.\\n");
            remove_tree_best_effort (File.new_for_path (root));
            return 1;
        }
        string bnr_seal;
        uint64 bnr_file_count;
        uint64 bnr_total_bytes;
        if (!RepositoryNative.compute_snapshot_seal (
                bnr_snapshot_root,
                out bnr_seal,
                out bnr_file_count,
                out bnr_total_bytes
            )) {
            stderr.printf ("PRES-09 could not compute fixture RMD snapshot seal.\\n");
            remove_tree_best_effort (File.new_for_path (root));
            return 1;
        }

        void* control_store = null;
        if (!ControlStateNative.open_for_test (
                state_db,
                out control_store
            ) || control_store == null) {
            stderr.printf ("PRES-09 could not initialize the isolated Control DB schema.\\n");
            remove_tree_best_effort (File.new_for_path (root));
            return 1;
        }
        ControlStateNative.close_for_test (control_store);

        int64 generation_id = 0;
        if (!ControlStateNative.set_current_values_guarded (
                state_db,
                0,
                "ewd",
                ewd_snapshot_sha,
                "0.1.0",
                seal,
                out generation_id
            ) || generation_id <= 0) {
            stderr.printf ("PRES-09 could not seed the pinned EWD Control DB generation.\\n");
            remove_tree_best_effort (File.new_for_path (root));
            return 1;
        }
        int64 rmd_generation_id = 0;
        if (!ControlStateNative.set_current_values_guarded (
                state_db,
                generation_id,
                "rmd",
                BNR_SNAPSHOT_SHA,
                "0.3.0",
                bnr_seal,
                out rmd_generation_id
            ) || rmd_generation_id <= generation_id) {
            stderr.printf ("PRES-09 could not seed the pinned RMD Control DB generation.\\n");
            remove_tree_best_effort (File.new_for_path (root));
            return 1;
        }
    } catch (GLib.Error error) {
        stderr.printf (
            "PRES-09 fixture setup failed: %s\n",
            error.message
        );
        remove_tree_best_effort (File.new_for_path (root));
        return 1;
    }

    Environment.set_variable (
        "ATM_PRES09_EWD_SNAPSHOT_SHA",
        ewd_snapshot_sha,
        true
    );
    Environment.set_variable ("HOME", home, true);
    Environment.set_variable ("XDG_STATE_HOME", state, true);
    Environment.set_variable ("XDG_CACHE_HOME", cache, true);
    Environment.set_variable ("XDG_CONFIG_HOME", config, true);
    Environment.set_variable (
        "ATM_PRES09_TEST_FLATPAK_INFO",
        flatpak_info,
        true
    );
    Environment.set_variable ("GTK_A11Y", "none", true);
    Environment.set_variable ("GTK_IM_MODULE", "gtk-im-context-simple", true);
    Environment.set_variable ("GDK_BACKEND", "x11", true);
    Environment.set_variable ("GSK_RENDERER", "cairo", true);
    Environment.set_variable ("LIBGL_ALWAYS_SOFTWARE", "1", true);
    Environment.set_variable ("NO_AT_BRIDGE", "1", true);
    Environment.set_variable ("G_DEBUG", "fatal-warnings", true);

    string[] phases = {
        "source-details",
        "intent-rejection",
        "live",
        "history",
        "history-invalid"
    };

    foreach (string phase in phases) {
        if (run_child_phase (executable_path, phase) != 0) {
            stderr.printf (
                "PRES09 GTK Application E2E stopped at phase '%s'; fixture retained at %s\n",
                phase,
                root
            );
            return 1;
        }
    }

    remove_tree_best_effort (File.new_for_path (root));
    remove_tree_best_effort (File.new_for_path (root));
    stdout.printf (
        "PASS: PRES-09 completed full live/Archive/process-relaunch History matrix for EWD pin %s.\\n",
        ewd_snapshot_sha
    );
    return 0;
}

private static int run_orchestrator (
    string raw_executable_path
) {
    string? initial_root =
        Environment.get_variable ("ATM_PRES09_FULL_EWD_INITIAL_ROOT");
    string? current_root =
        Environment.get_variable ("ATM_PRES09_FULL_EWD_ROOT");

    if (run_snapshot_scenario (
            raw_executable_path,
            EWD_INITIAL_SNAPSHOT_SHA,
            initial_root
        ) != 0) {
        return 1;
    }
    if (run_snapshot_scenario (
            raw_executable_path,
            EWD_CURRENT_SNAPSHOT_SHA,
            current_root
        ) != 0) {
        return 1;
    }

    stdout.printf (
        "PASS: PRES-09 full-process persistence E2E passed for both admitted EWD pins plus the exact RMD BNR C01 pin.\\n"
    );
    return 0;
}

private static int run_application_phase (
    string phase,
    string executable_path
) {
    Soup.Server server = new Soup.Server (null);
    server.add_handler ("/api/tags", tags_handler);
    server.add_handler ("/api/show", show_handler);
    server.add_handler ("/api/chat", chat_handler);
    server.add_handler ("/api/generate", generate_handler);

    try {
        server.listen_local (
            0,
            Soup.ServerListenOptions.IPV4_ONLY
        );
    } catch (GLib.Error error) {
        stderr.printf ("PRES-09 fake provider did not listen: %s\n", error.message);
        return 1;
    }

    GLib.SList<GLib.Uri>? uris = server.get_uris ();
    if (uris == null || uris.data == null) {
        stderr.printf ("PRES-09 fake provider did not expose a local URI.\n");
        server.disconnect ();
        return 1;
    }

    string base_url = "http://%s:%u".printf (
        uris.data.get_host (),
        uris.data.get_port ()
    );
    Environment.set_variable (
        "ATM_M12_TEST_BASE_URL",
        base_url,
        true
    );

    if (phase == "history-invalid") {
        string? home = Environment.get_variable ("HOME");
        if (home == null) {
            stderr.printf ("HOME missing in invalid-source phase.\n");
            server.disconnect ();
            return 1;
        }

        string? ewd_sha = Environment.get_variable (
            "ATM_PRES09_EWD_SNAPSHOT_SHA"
        );
        if (ewd_sha == null ||
            (ewd_sha != EWD_INITIAL_SNAPSHOT_SHA &&
             ewd_sha != EWD_CURRENT_SNAPSHOT_SHA)) {
            stderr.printf ("PRES-09 EWD snapshot SHA is missing or not admitted.\\n");
            server.disconnect ();
            return 1;
        }
        string manifest = GLib.Path.build_filename (
            home,
            "Ask the Model",
            "Repositories",
            "ewd",
            "snapshots",
            ewd_sha,
            "science",
            "data",
            "input_manifest.json"
        );
        string bnr_audit = GLib.Path.build_filename (
            home,
            "Ask the Model",
            "Repositories",
            "rmd",
            "snapshots",
            BNR_SNAPSHOT_SHA,
            "model",
            "calibration_validation",
            "bnr_bls_cross_round_mapping_audit.json"
        );
        try {
            File.new_for_path (manifest).delete (null);
            File.new_for_path (bnr_audit).delete (null);
        } catch (GLib.Error error) {
            stderr.printf (
                "PRES-09 could not tamper with the EWD/RMD test sources: %s\n",
                error.message
            );
            server.disconnect ();
            return 1;
        }
    }

    var app = new Application ();
    int test_status = 1;
    app.pres09_test_finished.connect ((success, detail) => {
        test_status = success ? 0 : 1;
        if (!success) {
            stderr.printf (
                "PRES09-GTK phase '%s' failed: %s\n",
                phase,
                detail
            );
        }
        app.quit ();
    });

    app.pres09_test_begin (phase);
    string[] app_args = { executable_path };
    app.run (app_args);
    server.disconnect ();
    return test_status;
}

public static int main (string[] args) {
    if (args.length > 1 &&
        args[1].has_prefix ("--phase=")) {
        string phase = args[1].substring (
            "--phase=".length
        );
        return run_application_phase (
            phase,
            args[0]
        );
    }

    return run_orchestrator (args[0]);
}

}
