using GLib;

private enum GcHookMode {
    NONE,
    ACQUIRE_SHARED_AFTER_CANDIDATE,
    CREATE_DURABLE_ROOT_AFTER_CANDIDATE,
    ASSERT_SHARED_BLOCKED_AFTER_EXCLUSIONS
}

private static GcHookMode hook_mode = GcHookMode.NONE;
private static string? hook_state_root = null;
private static int64 hook_generation_id = 0;
private static string hook_repository_version = "0.1.0";
private static int hook_shared_fd = -1;
private static bool hook_shared_blocked = false;
private static bool hook_action_completed = false;

private static string
new_temp_root () {
    Error? error = null;
    string? root = null;

    try {
        root = DirUtils.make_tmp (
            "atm-c1-i6-test-XXXXXX"
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

    return control.repository_generation_id;
}

private static void
reset_hook () {
    AskTheModel.RepositoryGcIsolationOrchestrator.
        set_test_hook (
            null
        );

    if (hook_shared_fd >= 0) {
        AskTheModel.RepositoryNative.
            release_generation_lease (
                hook_shared_fd
            );
    }

    hook_mode = GcHookMode.NONE;
    hook_state_root = null;
    hook_generation_id = 0;
    hook_repository_version = "0.1.0";
    hook_shared_fd = -1;
    hook_shared_blocked = false;
    hook_action_completed = false;
}

private static void
orchestrator_test_hook (
    string checkpoint,
    AskTheModel.RepositoryGcSnapshotCandidate candidate
) throws Error {
    if (hook_action_completed ||
        hook_state_root == null) {
        return;
    }

    string state_root = hook_state_root;

    if (hook_mode ==
            GcHookMode.ACQUIRE_SHARED_AFTER_CANDIDATE &&
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
            GcHookMode.CREATE_DURABLE_ROOT_AFTER_CANDIDATE &&
        checkpoint == "after-candidate-selection") {
        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                state_root
            );

        string id = conversations.create_conversation (
            "Late durable root",
            1000,
            "model-test",
            null,
            hook_generation_id,
            {
                new AskTheModel.ConversationPersistenceRepository (
                    candidate.repository_id,
                    hook_repository_version,
                    candidate.snapshot_sha
                )
            }
        );
        assert (id.length > 0);
        hook_action_completed = true;
        return;
    }

    if (hook_mode ==
            GcHookMode.ASSERT_SHARED_BLOCKED_AFTER_EXCLUSIONS &&
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
    }
}

private static void
install_hook (
    GcHookMode mode,
    string state_root,
    int64 generation_id,
    string repository_version = "0.1.0"
) {
    reset_hook ();
    hook_mode = mode;
    hook_state_root = state_root;
    hook_generation_id = generation_id;
    hook_repository_version = repository_version;
    AskTheModel.RepositoryGcIsolationOrchestrator.
        set_test_hook (
            orchestrator_test_hook
        );
}

private static void
test_orchestrator_off_is_noop () {
    string root = new_temp_root ();

    try {
        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    false,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.NOT_ENABLED
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
test_orchestrator_b0_contention_is_noop () {
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

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.B0_CONTENDED
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
test_orchestrator_isolates_unrooted_historical_snapshot () {
    const string OLD_SHA =
        "1111111111111111111111111111111111111111";
    const string NEW_SHA =
        "2222222222222222222222222222222222222222";

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

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.ISOLATED
        );
        assert (result.repository_id == "ewd");
        assert (result.snapshot_sha == OLD_SHA);
        assert (result.trash_path != null);
        assert (
            !FileUtils.test (
                snapshot_path_for (
                    root,
                    OLD_SHA
                ),
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
        assert (
            FileUtils.test (
                result.trash_path,
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
test_late_shared_reader_blocks_gc_exclusive () {
    const string OLD_SHA =
        "3333333333333333333333333333333333333333";
    const string NEW_SHA =
        "4444444444444444444444444444444444444444";

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
            GcHookMode.ACQUIRE_SHARED_AFTER_CANDIDATE,
            root,
            1
        );

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
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
            AskTheModel.RepositoryGcIsolationOutcome.B2_CONTENDED
        );
        assert (
            FileUtils.test (
                snapshot_path_for (
                    root,
                    OLD_SHA
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
test_late_durable_root_is_reread () {
    const string OLD_SHA =
        "5555555555555555555555555555555555555555";
    const string NEW_SHA =
        "6666666666666666666666666666666666666666";

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
            GcHookMode.CREATE_DURABLE_ROOT_AFTER_CANDIDATE,
            root,
            1,
            "0.1.0"
        );

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (hook_action_completed);
        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.DURABLE_ROOT_APPEARED
        );
        assert (
            FileUtils.test (
                snapshot_path_for (
                    root,
                    OLD_SHA
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
test_gc_exclusive_blocks_new_shared_reader () {
    const string OLD_SHA =
        "7777777777777777777777777777777777777777";
    const string NEW_SHA =
        "8888888888888888888888888888888888888888";

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
            GcHookMode.ASSERT_SHARED_BLOCKED_AFTER_EXCLUSIONS,
            root,
            1
        );

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (hook_shared_blocked);
        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.ISOLATED
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
        "9999999999999999999999999999999999999999";
    const string NEW_SHA =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

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

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        install_hook (
            GcHookMode.ACQUIRE_SHARED_AFTER_CANDIDATE,
            root,
            2
        );

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.B2_CONTENDED
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
                snapshot_path_for (
                    root,
                    OLD_SHA
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
test_durable_only_reread_ignores_owned_b2_exclusive () {
    const string OLD_SHA =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const string NEW_SHA =
        "cccccccccccccccccccccccccccccccccccccccc";

    string root = new_temp_root ();
    int exclusion_fd = -1;

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

        string conversation_id =
            conversations.create_conversation (
                "Protected while GC owns B2 EX",
                2000,
                "model-test",
                null,
                1,
                {
                    new AskTheModel.ConversationPersistenceRepository (
                        "ewd",
                        "0.1.0",
                        OLD_SHA
                    )
                }
            );
        assert (conversation_id.length > 0);

        bool contended = false;

        assert (
            AskTheModel.RepositoryNative.
                try_acquire_generation_lease_exclusive (
                    root,
                    1,
                    out exclusion_fd,
                    out contended
                )
        );
        assert (!contended);
        assert (exclusion_fd >= 0);

        AskTheModel.RepositoryGcDurableRoots roots =
            AskTheModel.RepositoryGcDurableRootCollector.
                collect_durable_only (
                    control_path_for (root),
                    conversations
                );

        assert (roots.protects_generation (1));
        assert (
            roots.protects_snapshot (
                "ewd",
                OLD_SHA
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    } finally {
        if (exclusion_fd >= 0) {
            AskTheModel.RepositoryNative.
                release_generation_lease (
                    exclusion_fd
                );
        }
        reset_hook ();
        remove_tree_best_effort (root);
    }
}

private static void
test_orchestrator_no_candidate_is_noop () {
    const string ACTIVE_SHA =
        "dddddddddddddddddddddddddddddddddddddddd";

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

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.NO_CANDIDATE
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
test_orphan_without_generation_reference_is_isolated () {
    const string ORPHAN_SHA =
        "1111111111111111111111111111111111111111";
    const string ACTIVE_SHA =
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

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

        AskTheModel.ConversationPersistenceStore conversations =
            conversation_store_for (
                root
            );

        AskTheModel.RepositoryGcIsolationResult result =
            AskTheModel.RepositoryGcIsolationOrchestrator.
                isolate_one (
                    true,
                    root,
                    root,
                    control_path_for (root),
                    conversations
                );

        assert (
            result.outcome ==
            AskTheModel.RepositoryGcIsolationOutcome.ISOLATED
        );
        assert (result.repository_id == "ewd");
        assert (result.snapshot_sha == ORPHAN_SHA);
        assert (result.trash_path != null);
        assert (
            !FileUtils.test (
                snapshot_path_for (
                    root,
                    ORPHAN_SHA
                ),
                FileTest.EXISTS
            )
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
        assert (
            FileUtils.test (
                result.trash_path,
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

public static int
main (string[] args) {
    Test.init (ref args);

    Test.add_func (
        "/repository-gc-i6/off-noop",
        test_orchestrator_off_is_noop
    );
    Test.add_func (
        "/repository-gc-i6/b0-contention-noop",
        test_orchestrator_b0_contention_is_noop
    );
    Test.add_func (
        "/repository-gc-i6/no-candidate-noop",
        test_orchestrator_no_candidate_is_noop
    );
    Test.add_func (
        "/repository-gc-i6/orphan-zero-generation-reference",
        test_orphan_without_generation_reference_is_isolated
    );
    Test.add_func (
        "/repository-gc-i6/unrooted-historical-isolated",
        test_orchestrator_isolates_unrooted_historical_snapshot
    );
    Test.add_func (
        "/repository-gc-i6/late-reader-blocks-exclusive",
        test_late_shared_reader_blocks_gc_exclusive
    );
    Test.add_func (
        "/repository-gc-i6/late-durable-root-reread",
        test_late_durable_root_is_reread
    );
    Test.add_func (
        "/repository-gc-i6/exclusive-blocks-new-reader",
        test_gc_exclusive_blocks_new_shared_reader
    );
    Test.add_func (
        "/repository-gc-i6/partial-exclusions-released",
        test_partial_b2_exclusions_are_released
    );
    Test.add_func (
        "/repository-gc-i6/durable-only-under-owned-exclusive",
        test_durable_only_reread_ignores_owned_b2_exclusive
    );

    return Test.run ();
}
