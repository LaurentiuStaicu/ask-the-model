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
        cname = "atm_c1_test_make_symlink",
        cheader_filename = "repository_gc_durable_roots_test_support.h"
    )]
    public static extern bool make_symlink (
        string target_path,
        string link_path
    ) throws GLib.Error;
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
                    root,
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
                    root,
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
                    root,
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
test_live_generation_lease_protects_historical_root () {
    const string OLD_EWD_SHA =
        "dddddddddddddddddddddddddddddddddddddddd";
    const string NEW_EWD_SHA =
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

    string root = new_temp_root ();

    try {
        publish_generation_one (
            root,
            OLD_EWD_SHA
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
            NEW_EWD_SHA,
            "0.2.0"
        );
        assert (control.repository_generation_id == 2);

        int reader_fd = -1;
        bool reader_contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_generation_lease_shared (
                    root,
                    1,
                    out reader_fd,
                    out reader_contended
                )
        );
        assert (!reader_contended);
        assert (reader_fd >= 0);

        AskTheModel.RepositoryGcDurableRoots live_roots =
            AskTheModel.RepositoryGcDurableRootCollector.
                collect (
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (live_roots.protects_generation (1));
        assert (live_roots.protects_generation (2));
        assert (
            live_roots.protects_snapshot (
                "ewd",
                OLD_EWD_SHA
            )
        );
        assert (
            live_roots.protects_snapshot (
                "ewd",
                NEW_EWD_SHA
            )
        );

        AskTheModel.RepositoryNative.
            release_generation_lease (
                reader_fd
            );
        reader_fd = -1;

        AskTheModel.RepositoryGcDurableRoots after_release =
            AskTheModel.RepositoryGcDurableRootCollector.
                collect (
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (!after_release.protects_generation (1));
        assert (after_release.protects_generation (2));
        assert (
            !after_release.protects_snapshot (
                "ewd",
                OLD_EWD_SHA
            )
        );
        assert (
            after_release.protects_snapshot (
                "ewd",
                NEW_EWD_SHA
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        remove_tree_best_effort (root);
    }
}


private static string
snapshot_root_for (
    string root,
    string repository_id
) {
    return Path.build_filename (
        root,
        "Repositories",
        repository_id,
        "snapshots"
    );
}


private static void
test_candidate_discovery_filters_protected_and_quarantine () {
    const string PROTECTED_SHA =
        "1111111111111111111111111111111111111111";
    const string CANDIDATE_SHA =
        "2222222222222222222222222222222222222222";
    const string UNKNOWN_REPO_SHA =
        "3333333333333333333333333333333333333333";

    string root = new_temp_root ();

    try {
        string ewd_snapshots =
            snapshot_root_for (
                root,
                "ewd"
            );
        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    ewd_snapshots,
                    PROTECTED_SHA
                ),
                0700
            ) == 0
        );
        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    ewd_snapshots,
                    CANDIDATE_SHA
                ),
                0700
            ) == 0
        );
        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    ewd_snapshots,
                    ".invalid-%s-1-0".printf (
                        CANDIDATE_SHA
                    )
                ),
                0700
            ) == 0
        );

        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    root,
                    "Repositories",
                    "unknown-repository",
                    "snapshots",
                    UNKNOWN_REPO_SHA
                ),
                0700
            ) == 0
        );

        var protected_roots =
            new AskTheModel.RepositoryGcDurableRoots ();
        protected_roots.add_snapshot (
            "ewd",
            PROTECTED_SHA
        );

        AskTheModel.RepositoryGcCandidateSet candidates =
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    protected_roots
                );

        assert (candidates.count () == 1);
        assert (
            candidates.contains (
                "ewd",
                CANDIDATE_SHA
            )
        );
        assert (
            !candidates.contains (
                "ewd",
                PROTECTED_SHA
            )
        );
        assert (
            !candidates.contains (
                "unknown-repository",
                UNKNOWN_REPO_SHA
            )
        );
        assert (
            candidates.quarantine_entry_count () == 1
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        remove_tree_best_effort (root);
    }
}


private static void
test_candidate_discovery_rejects_malformed_entry () {
    string root = new_temp_root ();

    try {
        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    snapshot_root_for (
                        root,
                        "ewd"
                    ),
                    "not-a-snapshot-sha"
                ),
                0700
            ) == 0
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    new AskTheModel.RepositoryGcDurableRoots ()
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "unexpected entry"
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
test_candidate_discovery_rejects_malformed_quarantine_entry () {
    const string SHA =
        "7777777777777777777777777777777777777777";

    string root = new_temp_root ();

    try {
        string snapshots =
            snapshot_root_for (
                root,
                "ewd"
            );

        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    snapshots,
                    ".invalid-%s-not-a-timestamp-0".printf (
                        SHA
                    )
                ),
                0700
            ) == 0
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    new AskTheModel.RepositoryGcDurableRoots ()
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "malformed quarantine entry"
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
test_candidate_discovery_rejects_symlink_entry () {
    const string SHA =
        "4444444444444444444444444444444444444444";

    string root = new_temp_root ();

    try {
        string snapshots =
            snapshot_root_for (
                root,
                "ewd"
            );
        string target =
            Path.build_filename (
                root,
                "real-target"
            );
        assert (
            DirUtils.create_with_parents (
                snapshots,
                0700
            ) == 0
        );
        assert (
            DirUtils.create_with_parents (
                target,
                0700
            ) == 0
        );

        AskTheModel.C1TestSupport.make_symlink (
            target,
            Path.build_filename (
                snapshots,
                SHA
            )
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    new AskTheModel.RepositoryGcDurableRoots ()
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "not a real directory"
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
test_candidate_discovery_rejects_regular_file_entry () {
    const string SHA =
        "5555555555555555555555555555555555555555";

    string root = new_temp_root ();

    try {
        string snapshots =
            snapshot_root_for (
                root,
                "ewd"
            );
        assert (
            DirUtils.create_with_parents (
                snapshots,
                0700
            ) == 0
        );

        FileUtils.set_contents (
            Path.build_filename (
                snapshots,
                SHA
            ),
            "not-a-directory"
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    new AskTheModel.RepositoryGcDurableRoots ()
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "not a real directory"
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
test_candidate_discovery_rejects_symlink_snapshot_root () {
    string root = new_temp_root ();

    try {
        string repository_root =
            Path.build_filename (
                root,
                "Repositories",
                "ewd"
            );
        string target =
            Path.build_filename (
                root,
                "snapshot-root-target"
            );

        assert (
            DirUtils.create_with_parents (
                repository_root,
                0700
            ) == 0
        );
        assert (
            DirUtils.create_with_parents (
                target,
                0700
            ) == 0
        );

        AskTheModel.C1TestSupport.make_symlink (
            target,
            Path.build_filename (
                repository_root,
                "snapshots"
            )
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    new AskTheModel.RepositoryGcDurableRoots ()
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "without following links"
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
test_candidate_discovery_rejects_intermediate_repository_symlink () {
    const string SHA =
        "6666666666666666666666666666666666666666";

    string root = new_temp_root ();

    try {
        string repositories_root =
            Path.build_filename (
                root,
                "Repositories"
            );
        string target_repository =
            Path.build_filename (
                root,
                "repository-target"
            );

        assert (
            DirUtils.create_with_parents (
                Path.build_filename (
                    target_repository,
                    "snapshots",
                    SHA
                ),
                0700
            ) == 0
        );
        assert (
            DirUtils.create_with_parents (
                repositories_root,
                0700
            ) == 0
        );

        AskTheModel.C1TestSupport.make_symlink (
            target_repository,
            Path.build_filename (
                repositories_root,
                "ewd"
            )
        );

        bool rejected = false;

        try {
            AskTheModel.RepositoryGcCandidateDiscovery.
                discover (
                    root,
                    new AskTheModel.RepositoryGcDurableRoots ()
                );
        } catch (Error error) {
            rejected =
                error.message.index_of (
                    "without following links"
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
        "/repository-gc-roots/live-generation-lease-protected",
        test_live_generation_lease_protects_historical_root
    );
    Test.add_func (
        "/repository-gc-candidates/protected-and-quarantine-filtered",
        test_candidate_discovery_filters_protected_and_quarantine
    );
    Test.add_func (
        "/repository-gc-candidates/malformed-entry-fail-closed",
        test_candidate_discovery_rejects_malformed_entry
    );
    Test.add_func (
        "/repository-gc-candidates/malformed-quarantine-fail-closed",
        test_candidate_discovery_rejects_malformed_quarantine_entry
    );
    Test.add_func (
        "/repository-gc-candidates/symlink-entry-fail-closed",
        test_candidate_discovery_rejects_symlink_entry
    );
    Test.add_func (
        "/repository-gc-candidates/regular-file-entry-fail-closed",
        test_candidate_discovery_rejects_regular_file_entry
    );
    Test.add_func (
        "/repository-gc-candidates/symlink-root-fail-closed",
        test_candidate_discovery_rejects_symlink_snapshot_root
    );
    Test.add_func (
        "/repository-gc-candidates/intermediate-symlink-fail-closed",
        test_candidate_discovery_rejects_intermediate_repository_symlink
    );
    return Test.run ();
}
