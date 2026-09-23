namespace AskTheModel.Tests {
    private static string new_temp_root () {
        try {
            return GLib.DirUtils.make_tmp (
                "atm-control-repository-state-test-XXXXXX"
            );
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static string control_path (string root) {
        return GLib.Path.build_filename (
            root,
            "control-state.sqlite3"
        );
    }

    private static string legacy_path (string root) {
        return GLib.Path.build_filename (
            root,
            "repository-state.json"
        );
    }

    private static void remove_tree_best_effort (
        string root
    ) {
        string[] names = {
            "control-state.sqlite3-shm",
            "control-state.sqlite3-wal",
            "control-state.sqlite3",
            "repository-state.json"
        };

        foreach (string name in names) {
            GLib.FileUtils.remove (
                GLib.Path.build_filename (
                    root,
                    name
                )
            );
        }

        GLib.DirUtils.remove (root);
    }

    private static void bootstrap_empty (
        string root
    ) throws GLib.Error {
        int disposition;
        ControlStateNative.publish_cutover (
            control_path (root),
            legacy_path (root),
            out disposition
        );
        assert (disposition == 0);
    }

    public static int main (string[] args) {
        string sha =
            "0123456789abcdef0123456789abcdef01234567";
        string seal =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

        try {
            string absent_root = new_temp_root ();
            var absent =
                new ControlRepositoryStateStore (
                    absent_root
                );
            assert (
                absent.load_status ==
                RepositoryStateLoadStatus.ABSENT
            );

            bool absent_mutation_rejected = false;
            try {
                absent.set_current (
                    "rmd",
                    sha,
                    "0.1.0"
                );
            } catch (RepositoryError error) {
                absent_mutation_rejected =
                    error.code ==
                    RepositoryError.STORAGE;
            }
            assert (absent_mutation_rejected);
            assert (
                !GLib.FileUtils.test (
                    control_path (absent_root),
                    GLib.FileTest.EXISTS
                )
            );
            remove_tree_best_effort (
                absent_root
            );

            string root = new_temp_root ();
            bootstrap_empty (root);

            var store =
                new ControlRepositoryStateStore (
                    root
                );
            assert (
                store.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (
                store.control_schema_version == 1
            );
            assert (store.repository_generation_id == 0);
            assert (
                !store.record_for ("rmd").is_ready ()
            );

            store.set_current (
                "rmd",
                sha,
                "0.1.0"
            );
            assert (
                store.record_for ("rmd").current_sha ==
                sha
            );
            assert (
                store.record_for ("rmd").version ==
                "0.1.0"
            );
            assert (store.repository_generation_id == 1);

            var reloaded =
                new ControlRepositoryStateStore (
                    root
                );
            assert (
                reloaded.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (
                reloaded.record_for ("rmd").current_sha ==
                sha
            );
            assert (
                reloaded.record_for ("rmd").version ==
                "0.1.0"
            );
            assert (
                reloaded.record_for (
                    "rmd"
                ).snapshot_seal_sha256 == null
            );
            assert (reloaded.repository_generation_id == 1);

            reloaded.set_snapshot_seal (
                "rmd",
                sha,
                seal
            );
            assert (
                reloaded.record_for (
                    "rmd"
                ).snapshot_seal_sha256 == seal
            );
            assert (reloaded.repository_generation_id == 2);

            var sealed =
                new ControlRepositoryStateStore (
                    root
                );
            assert (
                sealed.record_for (
                    "rmd"
                ).snapshot_seal_sha256 == seal
            );

            bool wrong_sha_rejected = false;
            try {
                sealed.set_snapshot_seal (
                    "rmd",
                    "1111111111111111111111111111111111111111",
                    seal
                );
            } catch (RepositoryError error) {
                wrong_sha_rejected =
                    error.code ==
                    RepositoryError.INVALID_RESPONSE;
            }
            assert (wrong_sha_rejected);
            assert (
                sealed.record_for (
                    "rmd"
                ).snapshot_seal_sha256 == seal
            );

            bool invalid_seal_rejected = false;
            try {
                sealed.set_snapshot_seal (
                    "rmd",
                    sha,
                    "invalid"
                );
            } catch (RepositoryError error) {
                invalid_seal_rejected =
                    error.code ==
                    RepositoryError.INVALID_RESPONSE;
            }
            assert (invalid_seal_rejected);

            var stale =
                new ControlRepositoryStateStore (
                    root
                );
            var writer =
                new ControlRepositoryStateStore (
                    root
                );
            int64 stale_generation =
                stale.repository_generation_id;
            assert (stale_generation == 2);

            writer.set_current (
                "ewd",
                "3333333333333333333333333333333333333333",
                "0.2.0"
            );
            assert (writer.repository_generation_id == 3);
            assert (
                stale.repository_generation_id ==
                stale_generation
            );
            assert (
                !stale.record_for ("ewd").is_ready ()
            );

            bool stale_write_rejected = false;
            try {
                stale.set_current (
                    "rmd",
                    "2222222222222222222222222222222222222222",
                    "0.2.0"
                );
            } catch (RepositoryError error) {
                stale_write_rejected =
                    error.code ==
                    RepositoryError.STORAGE;
            }
            assert (stale_write_rejected);
            assert (
                stale.repository_generation_id ==
                stale_generation
            );
            assert (
                stale.record_for ("rmd").current_sha ==
                sha
            );

            var after_concurrent_write =
                new ControlRepositoryStateStore (
                    root
                );
            assert (
                after_concurrent_write.repository_generation_id ==
                3
            );
            assert (
                after_concurrent_write.record_for (
                    "ewd"
                ).current_sha ==
                "3333333333333333333333333333333333333333"
            );

            string old_version =
                sealed.record_for ("rmd").version ?? "";
            GLib.FileUtils.set_contents (
                control_path (root),
                "not a sqlite database"
            );

            bool storage_failure_rolled_back = false;
            try {
                sealed.set_current (
                    "rmd",
                    "2222222222222222222222222222222222222222",
                    "0.2.0"
                );
            } catch (RepositoryError error) {
                storage_failure_rolled_back =
                    error.code ==
                    RepositoryError.STORAGE;
            }
            assert (storage_failure_rolled_back);
            assert (
                sealed.record_for ("rmd").current_sha ==
                sha
            );
            assert (
                sealed.record_for ("rmd").version ==
                old_version
            );
            remove_tree_best_effort (root);

            string invalid_root = new_temp_root ();
            GLib.FileUtils.set_contents (
                control_path (invalid_root),
                "not a sqlite database"
            );
            var invalid =
                new ControlRepositoryStateStore (
                    invalid_root
                );
            assert (
                invalid.load_status ==
                RepositoryStateLoadStatus.INVALID
            );
            assert (
                !invalid.record_for ("rmd").is_ready ()
            );
            remove_tree_best_effort (
                invalid_root
            );

            string legacy_root = new_temp_root ();
            GLib.FileUtils.set_contents (
                legacy_path (legacy_root),
                """
{
  "schema_version": 2,
  "repositories": [
    {
      "id": "rmd",
      "sha": "0123456789abcdef0123456789abcdef01234567",
      "version": "0.1.0",
      "snapshot_seal_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }
  ]
}
"""
            );

            int disposition;
            ControlStateNative.publish_cutover (
                control_path (legacy_root),
                legacy_path (legacy_root),
                out disposition
            );
            assert (disposition == 1);

            var imported =
                new ControlRepositoryStateStore (
                    legacy_root
                );
            assert (
                imported.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (
                imported.record_for ("rmd").current_sha ==
                sha
            );
            assert (
                imported.record_for (
                    "rmd"
                ).snapshot_seal_sha256 == seal
            );

            string preserved_legacy;
            GLib.FileUtils.get_contents (
                legacy_path (legacy_root),
                out preserved_legacy
            );
            assert (
                preserved_legacy.contains (
                    "\"schema_version\": 2"
                )
            );

            remove_tree_best_effort (
                legacy_root
            );
        } catch (GLib.Error error) {
            stderr.printf ("%s\n", error.message);
            return 1;
        }

        return 0;
    }
}
