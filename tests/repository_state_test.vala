namespace AskTheModel.Tests {
    private static string new_temp_root () {
        try {
            return GLib.DirUtils.make_tmp (
                "atm-repository-state-test-XXXXXX"
            );
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static string state_path (string root) {
        return GLib.Path.build_filename (
            root,
            "repository-state.json"
        );
    }

    private static void remove_state_root (string root) {
        GLib.FileUtils.remove (state_path (root));
        GLib.DirUtils.remove (root);
    }

    public static int main (string[] args) {
        string root = new_temp_root ();
        string sha =
            "0123456789abcdef0123456789abcdef01234567";

        try {
            var store = new RepositoryStateStore (root);
            assert (
                store.load_status ==
                RepositoryStateLoadStatus.ABSENT
            );
            assert (!store.record_for ("rmd").is_ready ());

            store.set_current (
                "rmd",
                sha,
                "0.1.0"
            );

            assert (
                store.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (store.record_for ("rmd").is_ready ());
            assert (
                store.record_for ("rmd").current_sha == sha
            );
            assert (
                store.record_for ("rmd").version == "0.1.0"
            );

            var reloaded = new RepositoryStateStore (root);
            assert (
                reloaded.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (reloaded.record_for ("rmd").is_ready ());
            assert (
                reloaded.record_for ("rmd").current_sha == sha
            );
            assert (
                reloaded.record_for ("rmd").version == "0.1.0"
            );

            bool rejected = false;
            try {
                reloaded.set_current (
                    "rmd",
                    "invalid",
                    "0.1.0"
                );
            } catch (RepositoryError error) {
                rejected =
                    error.code ==
                    RepositoryError.INVALID_RESPONSE;
            }

            assert (rejected);

            string rollback_root = new_temp_root ();
            string rollback_state_file =
                state_path (rollback_root);
            string old_sha =
                "1111111111111111111111111111111111111111";
            string new_sha =
                "2222222222222222222222222222222222222222";
            string old_seal =
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            string new_seal =
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

            var rollback_store =
                new RepositoryStateStore (rollback_root);
            rollback_store.set_current (
                "rmd",
                old_sha,
                "0.1.0",
                old_seal
            );

            GLib.FileUtils.remove (rollback_state_file);
            GLib.DirUtils.remove (rollback_root);
            GLib.FileUtils.set_contents (
                rollback_root,
                "blocking-file"
            );

            bool storage_failed = false;
            try {
                rollback_store.set_current (
                    "rmd",
                    new_sha,
                    "0.2.0",
                    new_seal
                );
            } catch (RepositoryError error) {
                storage_failed =
                    error.code == RepositoryError.STORAGE;
            }

            assert (storage_failed);
            assert (
                rollback_store.record_for ("rmd").current_sha ==
                    old_sha
            );
            assert (
                rollback_store.record_for ("rmd").version ==
                    "0.1.0"
            );
            assert (
                rollback_store.record_for ("rmd").seal_sha256 ==
                    old_seal
            );

            GLib.FileUtils.remove (rollback_root);

            string invalid_root = new_temp_root ();
            string invalid_state = state_path (invalid_root);
            string malformed = "{ this is not json";
            GLib.FileUtils.set_contents (
                invalid_state,
                malformed
            );

            var invalid_store =
                new RepositoryStateStore (invalid_root);
            assert (
                invalid_store.load_status ==
                RepositoryStateLoadStatus.INVALID
            );
            assert (
                !invalid_store.record_for ("ewd").is_ready ()
            );
            assert (
                !invalid_store.record_for ("cbd").is_ready ()
            );
            assert (
                !invalid_store.record_for ("rmd").is_ready ()
            );

            bool invalid_overwrite_rejected = false;
            try {
                invalid_store.set_current (
                    "rmd",
                    sha,
                    "0.1.0"
                );
            } catch (RepositoryError error) {
                invalid_overwrite_rejected =
                    error.code == RepositoryError.STORAGE;
            }

            assert (invalid_overwrite_rejected);

            string preserved;
            GLib.FileUtils.get_contents (
                invalid_state,
                out preserved
            );
            assert (preserved == malformed);
            remove_state_root (invalid_root);

            string partial_root = new_temp_root ();
            string partial_state = state_path (partial_root);
            GLib.FileUtils.set_contents (
                partial_state,
                """
{
  "schema_version": 1,
  "repositories": [
    {
      "id": "rmd",
      "sha": "0123456789abcdef0123456789abcdef01234567",
      "version": "0.1.0"
    },
    {
      "id": "unknown",
      "sha": "1111111111111111111111111111111111111111",
      "version": "0.1.0"
    }
  ]
}
"""
            );

            var partial_store =
                new RepositoryStateStore (partial_root);
            assert (
                partial_store.load_status ==
                RepositoryStateLoadStatus.INVALID
            );
            assert (
                !partial_store.record_for ("rmd").is_ready ()
            );
            remove_state_root (partial_root);

            string duplicate_root = new_temp_root ();
            GLib.FileUtils.set_contents (
                state_path (duplicate_root),
                """
{
  "schema_version": 1,
  "repositories": [
    {
      "id": "rmd",
      "sha": "0123456789abcdef0123456789abcdef01234567",
      "version": "0.1.0"
    },
    {
      "id": "rmd",
      "sha": "1111111111111111111111111111111111111111",
      "version": "0.1.0"
    }
  ]
}
"""
            );

            var duplicate_store =
                new RepositoryStateStore (duplicate_root);
            assert (
                duplicate_store.load_status ==
                RepositoryStateLoadStatus.INVALID
            );
            assert (
                !duplicate_store.record_for ("rmd").is_ready ()
            );
            remove_state_root (duplicate_root);

            string sealed_root = new_temp_root ();
            string seal_sha =
                "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
            var sealed_store =
                new RepositoryStateStore (sealed_root);

            sealed_store.set_current (
                "ewd",
                sha,
                "0.1.0",
                seal_sha
            );

            var sealed_reloaded =
                new RepositoryStateStore (sealed_root);
            assert (
                sealed_reloaded.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (
                sealed_reloaded.record_for ("ewd").seal_sha256 ==
                    seal_sha
            );
            remove_state_root (sealed_root);

            string invalid_seal_root = new_temp_root ();
            var invalid_seal_store =
                new RepositoryStateStore (invalid_seal_root);
            bool invalid_seal_rejected = false;

            try {
                invalid_seal_store.set_current (
                    "ewd",
                    sha,
                    "0.1.0",
                    "bad-seal"
                );
            } catch (RepositoryError error) {
                invalid_seal_rejected =
                    error.code ==
                    RepositoryError.INVALID_RESPONSE;
            }

            assert (invalid_seal_rejected);
            remove_state_root (invalid_seal_root);

            string malformed_seal_root = new_temp_root ();
            GLib.FileUtils.set_contents (
                state_path (malformed_seal_root),
                """
{
  "schema_version": 1,
  "repositories": [
    {
      "id": "ewd",
      "sha": "0123456789abcdef0123456789abcdef01234567",
      "version": "0.1.0",
      "seal_sha256": "not-a-valid-seal"
    }
  ]
}
"""
            );

            var malformed_seal_store =
                new RepositoryStateStore (malformed_seal_root);
            assert (
                malformed_seal_store.load_status ==
                RepositoryStateLoadStatus.INVALID
            );
            assert (
                !malformed_seal_store.record_for ("ewd").is_ready ()
            );
            remove_state_root (malformed_seal_root);

            string empty_valid_root = new_temp_root ();
            GLib.FileUtils.set_contents (
                state_path (empty_valid_root),
                """
{
  "schema_version": 1,
  "repositories": []
}
"""
            );

            var empty_valid =
                new RepositoryStateStore (empty_valid_root);
            assert (
                empty_valid.load_status ==
                RepositoryStateLoadStatus.VALID
            );
            assert (!empty_valid.record_for ("rmd").is_ready ());
            remove_state_root (empty_valid_root);

        } catch (GLib.Error error) {
            stderr.printf ("%s\n", error.message);
            return 1;
        }

        remove_state_root (root);
        return 0;
    }
}
