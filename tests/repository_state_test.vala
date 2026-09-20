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

    public static int main (string[] args) {
        string root = new_temp_root ();
        string sha =
            "0123456789abcdef0123456789abcdef01234567";

        try {
            var store = new RepositoryStateStore (root);
            assert (!store.record_for ("rmd").is_ready ());

            store.set_current (
                "rmd",
                sha,
                "0.1.0"
            );

            assert (store.record_for ("rmd").is_ready ());
            assert (
                store.record_for ("rmd").current_sha == sha
            );
            assert (
                store.record_for ("rmd").version == "0.1.0"
            );

            var reloaded = new RepositoryStateStore (root);
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
        } catch (GLib.Error error) {
            stderr.printf ("%s\n", error.message);
            return 1;
        }

        string state_file = GLib.Path.build_filename (
            root,
            "repository-state.json"
        );
        GLib.FileUtils.remove (state_file);
        GLib.DirUtils.remove (root);
        return 0;
    }
}
