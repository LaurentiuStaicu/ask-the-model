using GLib;

namespace AskTheModel.C1TestSupport {
    [CCode (
        cname = "atm_c1_test_publish_empty_complete_generation",
        cheader_filename = "repository_gc_durable_roots_test_support.h"
    )]
    public static extern bool publish_empty_complete_generation (
        string control_path
    ) throws GLib.Error;

    [CCode (
        cname = "atm_c1_test_acquire_shared_generation_lease",
        cheader_filename = "repository_gc_durable_roots_test_support.h"
    )]
    public static extern int acquire_shared_generation_lease (
        string state_root,
        int64 generation_id
    ) throws GLib.Error;

    [CCode (
        cname = "atm_c1_test_release_generation_lease",
        cheader_filename = "repository_gc_durable_roots_test_support.h"
    )]
    public static extern void release_generation_lease (
        int lease_fd
    );
}

private static string
new_temp_root () {
    Error? error = null;
    string? root = null;

    try {
        root = DirUtils.make_tmp (
            "atm-c1-roots-test-XXXXXX"
        );
    } catch (Error caught) {
        error = caught;
    }

    assert (error == null);
    assert (root != null);
    return root ?? "";
}

private static void
remove_tree_best_effort (string path) {
    if (!FileUtils.test (
            path,
            FileTest.EXISTS
        ) &&
        !FileUtils.test (
            path,
            FileTest.IS_SYMLINK
        )) {
        return;
    }

    if (!FileUtils.test (
            path,
            FileTest.IS_DIR
        ) ||
        FileUtils.test (
            path,
            FileTest.IS_SYMLINK
        )) {
        FileUtils.remove (path);
        return;
    }

    try {
        Dir directory = Dir.open (path);
        string? name;

        while ((name = directory.read_name ()) != null) {
            remove_tree_best_effort (
                Path.build_filename (
                    path,
                    name
                )
            );
        }
    } catch (Error error) {
    }

    DirUtils.remove (path);
}

private static string
control_path_for (string root) {
    return Path.build_filename (
        root,
        "control-state.sqlite3"
    );
}

private static void
publish_generation_one (
    string root,
    string sha
) throws Error {
    string legacy_path = Path.build_filename (
        root,
        "repository-state.json"
    );

    FileUtils.set_contents (
        legacy_path,
        "{\n" +
        "  \"schema_version\": 1,\n" +
        "  \"repositories\": [\n" +
        "    {\n" +
        "      \"id\": \"ewd\",\n" +
        "      \"sha\": \"%s\",\n".printf (sha) +
        "      \"version\": \"0.1.0\"\n" +
        "    }\n" +
        "  ]\n" +
        "}\n"
    );

    int disposition;
    AskTheModel.ControlStateNative.publish_cutover (
        control_path_for (root),
        legacy_path,
        out disposition
    );

    int64 active_generation;
    AskTheModel.ControlStateNative.active_generation_id (
        control_path_for (root),
        out active_generation
    );
    assert (active_generation == 1);
}

private static void
test_active_and_conversation_roots_are_unioned () {
    const string EWD_SHA =
        "1111111111111111111111111111111111111111";
    const string CBD_SHA =
        "2222222222222222222222222222222222222222";

    string root = new_temp_root ();

    try {
        publish_generation_one (
            root,
            EWD_SHA
        );

        var conversations =
            new AskTheModel.ConversationPersistenceStore (
                root,
                Path.build_filename (
                    root,
                    "exports"
                )
            );

        var ewd_pin =
            new AskTheModel.ConversationPersistenceRepository (
                "ewd",
                "0.1.0",
                EWD_SHA
            );

        string archived_id =
            conversations.create_conversation (
                "Archived generation one",
                100,
                "model-a",
                null,
                1,
                { ewd_pin }
            );
        conversations.set_archived (
            archived_id,
            true,
            101
        );

        string working_id =
            conversations.create_conversation (
                "Working generation one",
                102,
                "model-a",
                null,
                1,
                { ewd_pin }
            );
        assert (working_id.length > 0);

        string zero_id =
            conversations.create_conversation (
                "No repository generation",
                103,
                "model-a",
                null,
                0,
                {}
            );
        assert (zero_id.length > 0);

        var control =
            new AskTheModel.ControlRepositoryStateStore (
                root
            );
        assert (
            control.load_status ==
            AskTheModel.RepositoryStateLoadStatus.VALID
        );
        assert (control.repository_generation_id == 1);

        control.set_current (
            "cbd",
            CBD_SHA,
            "0.1.0"
        );
        assert (control.repository_generation_id == 2);

        AskTheModel.RepositoryGcDurableRoots roots =
            AskTheModel.RepositoryGcDurableRootCollector.
                collect (
                    control_path_for (root),
                    conversations
                );

        assert (roots.generation_count () == 2);
        assert (roots.protects_generation (1));
        assert (roots.protects_generation (2));
        assert (!roots.protects_generation (0));

        assert (roots.snapshot_count () == 2);
        assert (
            roots.protects_snapshot (
                "ewd",
                EWD_SHA
            )
        );
        assert (
            roots.protects_snapshot (
                "cbd",
                CBD_SHA
            )
        );
        assert (
            !roots.protects_snapshot (
                "rmd",
                "3333333333333333333333333333333333333333"
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        remove_tree_best_effort (root);
    }
}

private static void
test_conversation_pin_mismatch_fails_closed () {
    const string EWD_SHA =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const string WRONG_SHA =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    string root = new_temp_root ();

    try {
        publish_generation_one (
            root,
            EWD_SHA
        );

        var conversations =
            new AskTheModel.ConversationPersistenceStore (
                root,
                Path.build_filename (
                    root,
                    "exports"
                )
            );

        string conversation_id =
            conversations.create_conversation (
                "Mismatched pin",
                200,
                "model-a",
                null,
                1,
                {
                    new AskTheModel.ConversationPersistenceRepository (
                        "ewd",
                        "0.1.0",
                        WRONG_SHA
                    )
                }
            );
        conversations.set_archived (
            conversation_id,
            true,
            201
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcDurableRootCollector.
                collect (
                    control_path_for (root),
                    conversations
                );
        } catch (Error error) {
            rejected = true;
        }

        assert (rejected);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        remove_tree_best_effort (root);
    }
}


private static void
test_empty_complete_generation_fails_closed () {
    const string EWD_SHA =
        "cccccccccccccccccccccccccccccccccccccccc";

    string root = new_temp_root ();

    try {
        publish_generation_one (
            root,
            EWD_SHA
        );

        var conversations =
            new AskTheModel.ConversationPersistenceStore (
                root,
                Path.build_filename (
                    root,
                    "exports"
                )
            );

        AskTheModel.C1TestSupport.
            publish_empty_complete_generation (
                control_path_for (root)
            );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcDurableRootCollector.
                collect (
                    control_path_for (root),
                    conversations
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "positive protected repository generation contains no catalog repository state"
                ) >= 0;
        }

        assert (rejected);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        remove_tree_best_effort (root);
    }
}


private static void
test_live_historical_generation_is_protected_only_while_contended () {
    const string OLD_SHA =
        "dddddddddddddddddddddddddddddddddddddddd";
    const string NEW_SHA =
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

    string root = new_temp_root ();
    int lease_fd = -1;

    try {
        publish_generation_one (
            root,
            OLD_SHA
        );

        var conversations =
            new AskTheModel.ConversationPersistenceStore (
                root,
                Path.build_filename (
                    root,
                    "exports"
                )
            );

        var control =
            new AskTheModel.ControlRepositoryStateStore (
                root
            );
        assert (
            control.load_status ==
            AskTheModel.RepositoryStateLoadStatus.VALID
        );
        assert (control.repository_generation_id == 1);

        control.set_current (
            "ewd",
            NEW_SHA,
            "0.2.0"
        );
        assert (control.repository_generation_id == 2);

        lease_fd =
            AskTheModel.C1TestSupport.
                acquire_shared_generation_lease (
                    root,
                    1
                );
        assert (lease_fd >= 0);

        AskTheModel.RepositoryGcDurableRoots roots =
            AskTheModel.RepositoryGcDurableRootCollector.
                collect_with_live_roots (
                    control_path_for (root),
                    root,
                    conversations
                );

        assert (roots.generation_count () == 2);
        assert (roots.protects_generation (2));
        assert (roots.protects_generation (1));
        assert (
            roots.protects_snapshot (
                "ewd",
                OLD_SHA
            )
        );
        assert (
            roots.protects_snapshot (
                "ewd",
                NEW_SHA
            )
        );

        AskTheModel.C1TestSupport.
            release_generation_lease (
                lease_fd
            );
        lease_fd = -1;

        AskTheModel.RepositoryGcDurableRoots after_release =
            AskTheModel.RepositoryGcDurableRootCollector.
                collect_with_live_roots (
                    control_path_for (root),
                    root,
                    conversations
                );

        assert (after_release.generation_count () == 1);
        assert (after_release.protects_generation (2));
        assert (!after_release.protects_generation (1));
        assert (
            !after_release.protects_snapshot (
                "ewd",
                OLD_SHA
            )
        );
        assert (
            after_release.protects_snapshot (
                "ewd",
                NEW_SHA
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        if (lease_fd >= 0) {
            AskTheModel.C1TestSupport.
                release_generation_lease (
                    lease_fd
                );
        }

        remove_tree_best_effort (root);
    }
}


public static int
main (string[] args) {
    Test.init (ref args);

    Test.add_func (
        "/repository-gc-roots/active-plus-conversation",
        test_active_and_conversation_roots_are_unioned
    );
    Test.add_func (
        "/repository-gc-roots/conversation-mismatch-fail-closed",
        test_conversation_pin_mismatch_fails_closed
    );
    Test.add_func (
        "/repository-gc-roots/empty-complete-generation-fail-closed",
        test_empty_complete_generation_fails_closed
    );
    Test.add_func (
        "/repository-gc-roots/live-historical-generation",
        test_live_historical_generation_is_protected_only_while_contended
    );
    return Test.run ();
}
