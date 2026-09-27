using GLib;

private enum PurgeHookMode {
    NONE,
    ACQUIRE_SHARED_AFTER_CANDIDATE,
    CREATE_DURABLE_ROOT_AFTER_CANDIDATE,
    ASSERT_SHARED_BLOCKED_AFTER_EXCLUSIONS,
    REMOVE_CANDIDATE_BEFORE_I5
}

private static PurgeHookMode hook_mode = PurgeHookMode.NONE;
private static string? hook_state_root = null;
private static int64 hook_generation_id = 0;
private static string? hook_repository_version = null;
private static int hook_shared_fd = -1;
private static bool hook_shared_blocked = false;
private static bool hook_action_completed = false;

private static string
new_temp_root () {
    Error? error = null;
    string? root = null;

    try {
        root = DirUtils.make_tmp (
            "atm-c1-i9-test-XXXXXX"
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

private static string
snapshot_path_for (
    string root,
    string sha
) {
    return Path.build_filename (
        root,
        "Repositories",
        "ewd",
        "snapshots",
        sha
    );
}

private static string
trash_path_for (
    string root,
    AskTheModel.RepositoryGcTrashCandidate candidate
) {
    return Path.build_filename (
        root,
        "Repositories",
        ".trash",
        candidate.repository_id,
        candidate.trash_name
    );
}

private static void
create_snapshot_directory (
    string root,
    string sha
) {
    assert (
        DirUtils.create_with_parents (
            snapshot_path_for (
                root,
                sha
            ),
            0700
        ) == 0
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

private static AskTheModel.ConversationPersistenceStore
conversation_store_for (
    string root
) throws Error {
    return new AskTheModel.ConversationPersistenceStore (
        root,
        Path.build_filename (
            root,
            "exports"
        )
    );
}

private static int64
prepare_historical_candidate (
    string root,
    string old_sha,
    string new_sha,
    bool duplicate_old_generation = false
) throws Error {
    publish_generation_one (
        root,
        old_sha
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

    if (duplicate_old_generation) {
        control.set_current (
            "ewd",
            old_sha,
            "0.1.1"
        );
        assert (control.repository_generation_id == 2);
    }

    control.set_current (
        "ewd",
        new_sha,
        "0.2.0"
    );

    create_snapshot_directory (
        root,
        old_sha
    );
    create_snapshot_directory (
        root,
        new_sha
    );

    string trash_path;
    assert (
        AskTheModel.RepositoryNative.
            gc_isolate_snapshot_to_trash (
                root,
                "ewd",
                old_sha,
                out trash_path,
                null
            )
    );
    assert (
        FileUtils.test (
            trash_path,
            FileTest.IS_DIR
        )
    );
    assert (
        !FileUtils.test (
            snapshot_path_for (
                root,
                old_sha
            ),
            FileTest.EXISTS
        )
    );

    return control.repository_generation_id;
}

private static void
reset_hook () {
    AskTheModel.RepositoryGcPurgeOrchestrator.
        set_test_hook (
            null
        );

    if (hook_shared_fd >= 0) {
        AskTheModel.RepositoryNative.
            release_generation_lease (
                hook_shared_fd
            );
    }

    hook_mode = PurgeHookMode.NONE;
    hook_state_root = null;
    hook_generation_id = 0;
    hook_repository_version = "0.1.0";
    hook_shared_fd = -1;
    hook_shared_blocked = false;
    hook_action_completed = false;
}

private static void
purge_test_hook (
    string checkpoint,
    AskTheModel.RepositoryGcTrashCandidate candidate
) throws Error {
    if (hook_action_completed ||
        hook_state_root == null) {
        return;
    }

    string state_root = hook_state_root;

    if (hook_mode ==
            PurgeHookMode.ACQUIRE_SHARED_AFTER_CANDIDATE &&
        checkpoint == "after-candidate-selection") {
        bool contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_generation_lease_shared (
                    state_root,
                    hook_generation_id,
                    out hook_shared_fd,
                    out contended
                )
        );
        assert (!contended);
        assert (hook_shared_fd >= 0);
        hook_action_completed = true;
        return;
    }

    if (hook_mode ==
            PurgeHookMode.CREATE_DURABLE_ROOT_AFTER_CANDIDATE &&
        checkpoint == "after-candidate-selection") {
        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                state_root
            );

        string id = conversations.create_conversation (
            "Late purge durable root",
            3000,
            "model-test",
            null,
            hook_generation_id,
            {
                new AskTheModel.ConversationPersistenceRepository (
                    candidate.repository_id,
                    hook_repository_version ?? "0.1.0",
                    candidate.snapshot_sha
                )
            }
        );
        assert (id.length > 0);
        hook_action_completed = true;
        return;
    }

    if (hook_mode ==
            PurgeHookMode.ASSERT_SHARED_BLOCKED_AFTER_EXCLUSIONS &&
        checkpoint == "after-b2-exclusions") {
        int reader_fd = -1;
        bool contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_generation_lease_shared (
                    state_root,
                    hook_generation_id,
                    out reader_fd,
                    out contended
                )
        );
        assert (contended);
        assert (reader_fd < 0);
        hook_shared_blocked = true;
        hook_action_completed = true;
        return;
    }

    if (hook_mode ==
            PurgeHookMode.REMOVE_CANDIDATE_BEFORE_I5 &&
        checkpoint == "before-i5-purge") {
        string trash_path =
            trash_path_for (
                state_root,
                candidate
            );

        assert (
            DirUtils.remove (
                trash_path
            ) == 0
        );
        hook_action_completed = true;
    }
}

private static void
install_hook (
    PurgeHookMode mode,
    string state_root,
    int64 generation_id,
    string repository_version = "0.1.0"
) {
    reset_hook ();
    hook_mode = mode;
    hook_state_root = state_root;
    hook_generation_id = generation_id;
    hook_repository_version = repository_version;

    AskTheModel.RepositoryGcPurgeOrchestrator.
        set_test_hook (
            purge_test_hook
        );
}

private static void
test_purge_off_is_noop () {
    string root = new_temp_root ();

    try {
        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    false,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.NOT_ENABLED
        );
        assert (
            !FileUtils.test (
                Path.build_filename (
                    root,
                    "Repositories",
                    ".trash"
                ),
                FileTest.EXISTS
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_purge_b0_contention_is_noop () {
    string root = new_temp_root ();
    int b0_fd = -1;

    try {
        bool contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_mutation_lease (
                    root,
                    out b0_fd,
                    out contended
                )
        );
        assert (!contended);
        assert (b0_fd >= 0);

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.B0_CONTENDED
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        if (b0_fd >= 0) {
            AskTheModel.RepositoryNative.
                release_mutation_lease (
                    b0_fd
                );
        }
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_purge_no_candidate_is_noop () {
    const string ACTIVE_SHA =
        "1111111111111111111111111111111111111111";

    string root = new_temp_root ();

    try {
        publish_generation_one (
            root,
            ACTIVE_SHA
        );
        create_snapshot_directory (
            root,
            ACTIVE_SHA
        );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.NO_CANDIDATE
        );
        assert (
            FileUtils.test (
                snapshot_path_for (
                    root,
                    ACTIVE_SHA
                ),
                FileTest.IS_DIR
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_unrooted_historical_trash_is_purged () {
    const string OLD_SHA =
        "2222222222222222222222222222222222222222";
    const string NEW_SHA =
        "3333333333333333333333333333333333333333";

    string root = new_temp_root ();

    try {
        prepare_historical_candidate (
            root,
            OLD_SHA,
            NEW_SHA
        );

        AskTheModel.RepositoryGcTrashCandidate? before =
            AskTheModel.RepositoryGcTrashDiscovery.
                select_one (
                    root
                );
        assert (before != null);
        string selected_path =
            trash_path_for (
                root,
                before
            );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.PURGED
        );
        assert (result.repository_id == "ewd");
        assert (result.snapshot_sha == OLD_SHA);
        assert (
            !FileUtils.test (
                selected_path,
                FileTest.EXISTS
            )
        );
        assert (
            FileUtils.test (
                snapshot_path_for (
                    root,
                    NEW_SHA
                ),
                FileTest.IS_DIR
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_orphan_trash_without_generation_reference_is_purged () {
    const string ACTIVE_SHA =
        "4444444444444444444444444444444444444444";
    const string ORPHAN_SHA =
        "5555555555555555555555555555555555555555";

    string root = new_temp_root ();

    try {
        publish_generation_one (
            root,
            ACTIVE_SHA
        );
        create_snapshot_directory (
            root,
            ACTIVE_SHA
        );
        create_snapshot_directory (
            root,
            ORPHAN_SHA
        );

        string trash_path;
        assert (
            AskTheModel.RepositoryNative.
                gc_isolate_snapshot_to_trash (
                    root,
                    "ewd",
                    ORPHAN_SHA,
                    out trash_path,
                    null
                )
        );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.PURGED
        );
        assert (result.snapshot_sha == ORPHAN_SHA);
        assert (
            !FileUtils.test (
                trash_path,
                FileTest.EXISTS
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_late_shared_reader_blocks_purge_exclusive () {
    const string OLD_SHA =
        "6666666666666666666666666666666666666666";
    const string NEW_SHA =
        "7777777777777777777777777777777777777777";

    string root = new_temp_root ();

    try {
        prepare_historical_candidate (
            root,
            OLD_SHA,
            NEW_SHA
        );

        AskTheModel.RepositoryGcTrashCandidate? candidate =
            AskTheModel.RepositoryGcTrashDiscovery.
                select_one (
                    root
                );
        assert (candidate != null);
        string trash_path =
            trash_path_for (
                root,
                candidate
            );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        install_hook (
            PurgeHookMode.ACQUIRE_SHARED_AFTER_CANDIDATE,
            root,
            1
        );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (hook_action_completed);
        assert (hook_shared_fd >= 0);
        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.B2_CONTENDED
        );
        assert (
            FileUtils.test (
                trash_path,
                FileTest.IS_DIR
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_late_durable_root_is_reread_and_preserved () {
    const string OLD_SHA =
        "8888888888888888888888888888888888888888";
    const string NEW_SHA =
        "9999999999999999999999999999999999999999";

    string root = new_temp_root ();

    try {
        prepare_historical_candidate (
            root,
            OLD_SHA,
            NEW_SHA
        );

        AskTheModel.RepositoryGcTrashCandidate? candidate =
            AskTheModel.RepositoryGcTrashDiscovery.
                select_one (
                    root
                );
        assert (candidate != null);
        string trash_path =
            trash_path_for (
                root,
                candidate
            );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        install_hook (
            PurgeHookMode.CREATE_DURABLE_ROOT_AFTER_CANDIDATE,
            root,
            1,
            "0.1.0"
        );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (hook_action_completed);
        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.DURABLE_ROOT_APPEARED
        );
        assert (
            FileUtils.test (
                trash_path,
                FileTest.IS_DIR
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_purge_exclusive_blocks_new_shared_reader () {
    const string OLD_SHA =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const string NEW_SHA =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    string root = new_temp_root ();

    try {
        prepare_historical_candidate (
            root,
            OLD_SHA,
            NEW_SHA
        );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        install_hook (
            PurgeHookMode.ASSERT_SHARED_BLOCKED_AFTER_EXCLUSIONS,
            root,
            1
        );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (hook_shared_blocked);
        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.PURGED
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_partial_b2_exclusions_are_released () {
    const string OLD_SHA =
        "cccccccccccccccccccccccccccccccccccccccc";
    const string NEW_SHA =
        "dddddddddddddddddddddddddddddddddddddddd";

    string root = new_temp_root ();

    try {
        int64 active_generation =
            prepare_historical_candidate (
                root,
                OLD_SHA,
                NEW_SHA,
                true
            );
        assert (active_generation == 3);

        AskTheModel.RepositoryGcTrashCandidate? candidate =
            AskTheModel.RepositoryGcTrashDiscovery.
                select_one (
                    root
                );
        assert (candidate != null);
        string trash_path =
            trash_path_for (
                root,
                candidate
            );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        install_hook (
            PurgeHookMode.ACQUIRE_SHARED_AFTER_CANDIDATE,
            root,
            2
        );

        AskTheModel.RepositoryGcPurgeResult result =
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcPurgeOutcome.B2_CONTENDED
        );

        reset_hook ();

        int probe_fd = -1;
        bool probe_contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_generation_lease_exclusive (
                    root,
                    1,
                    out probe_fd,
                    out probe_contended
                )
        );
        assert (!probe_contended);
        assert (probe_fd >= 0);

        AskTheModel.RepositoryNative.
            release_generation_lease (
                probe_fd
            );

        assert (
            FileUtils.test (
                trash_path,
                FileTest.IS_DIR
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_exact_i5_revalidation_failure_releases_leases () {
    const string OLD_SHA =
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    const string NEW_SHA =
        "ffffffffffffffffffffffffffffffffffffffff";

    string root = new_temp_root ();
    bool failed = false;

    try {
        prepare_historical_candidate (
            root,
            OLD_SHA,
            NEW_SHA
        );

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        install_hook (
            PurgeHookMode.REMOVE_CANDIDATE_BEFORE_I5,
            root,
            1
        );

        try {
            AskTheModel.RepositoryGcPurgeOrchestrator.
                purge_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );
        } catch (Error expected) {
            failed = true;
        }

        assert (failed);
        assert (hook_action_completed);

        reset_hook ();

        int b0_probe_fd = -1;
        bool b0_contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_mutation_lease (
                    root,
                    out b0_probe_fd,
                    out b0_contended
                )
        );
        assert (!b0_contended);
        assert (b0_probe_fd >= 0);
        AskTheModel.RepositoryNative.
            release_mutation_lease (
                b0_probe_fd
            );

        int b2_probe_fd = -1;
        bool b2_contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_generation_lease_exclusive (
                    root,
                    1,
                    out b2_probe_fd,
                    out b2_contended
                )
        );
        assert (!b2_contended);
        assert (b2_probe_fd >= 0);
        AskTheModel.RepositoryNative.
            release_generation_lease (
                b2_probe_fd
            );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

public static int
main (string[] args) {
    Test.init (ref args);

    Test.add_func (
        "/repository-gc-i9/off-noop",
        test_purge_off_is_noop
    );
    Test.add_func (
        "/repository-gc-i9/b0-contention-noop",
        test_purge_b0_contention_is_noop
    );
    Test.add_func (
        "/repository-gc-i9/no-candidate-noop",
        test_purge_no_candidate_is_noop
    );
    Test.add_func (
        "/repository-gc-i9/orphan-zero-generation-reference-purged",
        test_orphan_trash_without_generation_reference_is_purged
    );
    Test.add_func (
        "/repository-gc-i9/unrooted-historical-purged",
        test_unrooted_historical_trash_is_purged
    );
    Test.add_func (
        "/repository-gc-i9/late-reader-blocks-exclusive",
        test_late_shared_reader_blocks_purge_exclusive
    );
    Test.add_func (
        "/repository-gc-i9/late-durable-root-reread-preserves",
        test_late_durable_root_is_reread_and_preserved
    );
    Test.add_func (
        "/repository-gc-i9/exclusive-blocks-new-reader",
        test_purge_exclusive_blocks_new_shared_reader
    );
    Test.add_func (
        "/repository-gc-i9/partial-exclusions-released",
        test_partial_b2_exclusions_are_released
    );
    Test.add_func (
        "/repository-gc-i9/exact-i5-revalidation-failure-releases",
        test_exact_i5_revalidation_failure_releases_leases
    );

    return Test.run ();
}
