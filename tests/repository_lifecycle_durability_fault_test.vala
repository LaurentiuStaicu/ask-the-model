namespace AskTheModel.M12FaultSupport {
    [CCode (
        cname = "atm_m12_set_fault_checkpoint",
        cheader_filename = "repository_lifecycle_durability_fault_support.h"
    )]
    public static extern void set_fault_checkpoint (
        string checkpoint_id
    );

    [CCode (
        cname = "atm_m12_clear_fault",
        cheader_filename = "repository_lifecycle_durability_fault_support.h"
    )]
    public static extern void clear_fault ();

    [CCode (
        cname = "atm_m12_fault_triggered",
        cheader_filename = "repository_lifecycle_durability_fault_support.h"
    )]
    public static extern bool fault_triggered ();

    [CCode (
        cname = "atm_m12_write_ewd_archive",
        cheader_filename = "repository_lifecycle_durability_fault_support.h"
    )]
    public static extern bool write_ewd_archive (
        string archive_path
    );
}

namespace AskTheModel.Tests {
    private static int result_code = 0;

    private static string new_m12_root () {
        try {
            return GLib.DirUtils.make_tmp (
                "atm-m12-runtime-fault-XXXXXX"
            );
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static void remove_tree_best_effort (
        string path
    ) {
        if (!GLib.FileUtils.test (
                path,
                GLib.FileTest.EXISTS
            )) {
            return;
        }

        if (!GLib.FileUtils.test (
                path,
                GLib.FileTest.IS_DIR
            )) {
            GLib.FileUtils.remove (path);
            return;
        }

        try {
            var directory = GLib.Dir.open (path);
            string? name;

            while ((name = directory.read_name ()) != null) {
                remove_tree_best_effort (
                    GLib.Path.build_filename (
                        path,
                        name
                    )
                );
            }
        } catch (GLib.FileError error) {
            return;
        }

        GLib.DirUtils.remove (path);
    }

    private static void publish_empty_control_state (
        string state_root
    ) throws GLib.Error {
        int disposition;

        ControlStateNative.publish_cutover (
            GLib.Path.build_filename (
                state_root,
                "control-state.sqlite3"
            ),
            GLib.Path.build_filename (
                state_root,
                "absent-legacy-state.json"
            ),
            out disposition
        );
    }

    private static async void run_fault_scenario (
        string checkpoint
    ) throws GLib.Error {
        string root = new_m12_root ();
        string state_root =
            GLib.Path.build_filename (
                root,
                "state"
            );
        string data_root =
            GLib.Path.build_filename (
                root,
                "data"
            );
        string cache_root =
            GLib.Path.build_filename (
                root,
                "cache"
            );
        string archive_path =
            GLib.Path.build_filename (
                root,
                "ewd.tar.gz"
            );

        assert (
            GLib.DirUtils.create_with_parents (
                state_root,
                0700
            ) == 0
        );
        assert (
            GLib.DirUtils.create_with_parents (
                data_root,
                0700
            ) == 0
        );
        assert (
            GLib.DirUtils.create_with_parents (
                cache_root,
                0700
            ) == 0
        );

        assert (
            M12FaultSupport.write_ewd_archive (
                archive_path
            )
        );

        publish_empty_control_state (
            state_root
        );

        var before =
            new ControlRepositoryStateStore (
                state_root
            );
        assert (
            before.load_status ==
                RepositoryStateLoadStatus.VALID
        );
        int64 generation_before =
            before.repository_generation_id;
        assert (
            before.record_for ("ewd").current_sha ==
                null
        );

        var service =
            new RepositoryLifecycleService (
                state_root,
                data_root,
                cache_root
            );
        service.apply_installation_qualification (
            true
        );
        assert (
            service.repository_operations_allowed ()
        );

        var policy = new OptimizationPolicy ();
        policy.set_enabled_for_session (true);
        service.set_optimization_policy (policy);
        assert (
            service.optimization_mode_snapshot ()
        );

        RepositoryDescriptor descriptor =
            RepositoryCatalog.all ()[0];
        RepositoryRuntimeInfo info =
            service.info_for (
                descriptor.id
            );
        string sha =
            "0123456789abcdef0123456789abcdef01234567";
        info.remote_sha = sha;
        info.remote_version = "0.1.0";

        service.set_archive_download_override_for_test (
            archive_path
        );

        M12FaultSupport.set_fault_checkpoint (
            checkpoint
        );

        bool failed = false;

        try {
            RepositoryDescriptor[] selection = {
                descriptor
            };
            yield service.download_or_update (
                selection
            );
        } catch (RepositoryError error) {
            failed =
                error.code == RepositoryError.STORAGE ||
                error.code == RepositoryError.NOT_READY;
        } finally {
            service.set_archive_download_override_for_test (
                null
            );
        }

        assert (failed);
        assert (
            M12FaultSupport.fault_triggered ()
        );
        M12FaultSupport.clear_fault ();

        var after =
            new ControlRepositoryStateStore (
                state_root
            );
        assert (
            after.load_status ==
                RepositoryStateLoadStatus.VALID
        );
        assert (
            after.repository_generation_id ==
                generation_before
        );

        RepositoryLocalRecord record =
            after.record_for (
                descriptor.id
            );
        assert (!record.is_ready ());
        assert (record.current_sha == null);
        assert (record.version == null);
        assert (
            record.snapshot_seal_sha256 == null
        );

        stdout.printf (
            "M12 runtime fault qualified checkpoint=%s generation=%" +
            int64.FORMAT +
            "\n",
            checkpoint,
            after.repository_generation_id
        );

        remove_tree_best_effort (root);
    }

    private static async void run_checks (
        GLib.MainLoop loop
    ) {
        try {
            string[] checkpoints = {
                "runtime_ingest_before_tree_fsync",
                "namespace_repository_before_child_fsync",
                "runtime_ingest_before_destination_parent_fsync",
                "runtime_ingest_before_source_parent_fsync"
            };

            foreach (string checkpoint in checkpoints) {
                yield run_fault_scenario (
                    checkpoint
                );
            }
        } catch (GLib.Error error) {
            stderr.printf (
                "%s\n",
                error.message
            );
            result_code = 1;
        }

        loop.quit ();
    }

    public static int main (
        string[] args
    ) {
        var loop = new GLib.MainLoop ();
        run_checks.begin (loop);
        loop.run ();
        return result_code;
    }
}
