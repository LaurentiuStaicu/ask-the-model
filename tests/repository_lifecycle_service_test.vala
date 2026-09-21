namespace AskTheModel.Tests {
    private static int result_code = 0;

    private static string new_temp_root () {
        try {
            return GLib.DirUtils.make_tmp (
                "atm-repository-lifecycle-test-XXXXXX"
            );
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static async void run_checks (GLib.MainLoop loop) {
        string root = new_temp_root ();

        try {
            var service = new RepositoryLifecycleService (root);
            RepositoryDescriptor[] catalog = RepositoryCatalog.all ();
            RepositoryDescriptor[] none = {};
            RepositoryDescriptor[] one = { catalog[0] };

            assert (catalog.length == 3);

            RepositoryRuntimeInfo freshness =
                service.info_for (catalog[0].id);
            freshness.remote_sha =
                "1111111111111111111111111111111111111111";
            freshness.remote_version = "9.9.9";
            freshness.clear_remote_identity ();
            assert (freshness.remote_sha == null);
            assert (freshness.remote_version == null);

            RepositoryRuntimeInfo first =
                service.info_for (catalog[0].id);
            RepositoryRuntimeInfo second =
                service.info_for (catalog[1].id);
            RepositoryRuntimeInfo third =
                service.info_for (catalog[2].id);
            first.remote_sha =
                "1111111111111111111111111111111111111111";
            first.remote_version = "1.0.0";
            second.remote_sha =
                "2222222222222222222222222222222222222222";
            second.remote_version = "2.0.0";
            third.remote_sha =
                "3333333333333333333333333333333333333333";
            third.remote_version = "3.0.0";

            RepositoryDescriptor[] batch = {
                catalog[0],
                catalog[2]
            };
            service.clear_remote_identities (batch);

            assert (first.remote_sha == null);
            assert (first.remote_version == null);
            assert (
                second.remote_sha ==
                "2222222222222222222222222222222222222222"
            );
            assert (second.remote_version == "2.0.0");
            assert (third.remote_sha == null);
            assert (third.remote_version == null);

            second.clear_remote_identity ();

            foreach (RepositoryDescriptor descriptor in catalog) {
                RepositoryRuntimeInfo info =
                    service.info_for (descriptor.id);

                assert (
                    info.descriptor.id == descriptor.id
                );
                assert (!info.local.is_ready ());
                assert (info.download_required ());
                assert (!info.update_available ());
                assert (info.remote_sha == null);
                assert (info.remote_version == null);
            }

            assert (!service.selection_needs_action (none));
            assert (
                service.action_tooltip (none) ==
                "Selected repositories are current"
            );

            assert (service.selection_needs_action (one));
            assert (
                service.action_tooltip (one) ==
                "Download selected repositories"
            );

            ConversationGrounding zero =
                yield service.prepare_conversation_grounding (none);

            assert (zero.is_frozen ());
            assert (zero.repository_count () == 0);

            bool not_ready_rejected = false;

            try {
                yield service.prepare_conversation_grounding (one);
            } catch (RepositoryError error) {
                not_ready_rejected =
                    error.code == RepositoryError.NOT_READY;
            }

            assert (not_ready_rejected);
        } catch (GLib.Error error) {
            stderr.printf ("%s\n", error.message);
            result_code = 1;
        }

        GLib.DirUtils.remove (root);
        loop.quit ();
    }

    public static int main (string[] args) {
        var loop = new GLib.MainLoop ();
        run_checks.begin (loop);
        loop.run ();
        return result_code;
    }
}
