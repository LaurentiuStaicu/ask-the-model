using GLib;

private static string
new_temp_root ()
{
    Error? error = null;
    string? root = null;

    try {
        root = DirUtils.make_tmp (
            "atm-conversation-persistence-XXXXXX"
        );
    } catch (Error caught) {
        error = caught;
    }

    assert (error == null);
    assert (root != null);
    return root ?? "";
}

private static void
test_domain_write_bridge ()
{
    string root = new_temp_root ();

    try {
        var store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        string plain_id =
            store.create_conversation (
                "Plain",
                10,
                "model-a",
                null,
                0,
                {}
            );

        assert (plain_id.length > 0);

        int64 plain_turn =
            store.commit_turn (
                plain_id,
                "hello",
                "world",
                "world",
                false,
                11,
                {}
            );

        assert (plain_turn == 0);

        var repository =
            new AskTheModel.ConversationPersistenceRepository (
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567"
            );

        string grounded_id =
            store.create_conversation (
                "Grounded",
                20,
                "model-b",
                "digest-b",
                7,
                { repository }
            );

        var citation =
            new AskTheModel.ConversationPersistenceCitation (
                "S1",
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567",
                "source-1",
                "README.md",
                "lines 1-2",
                "Title",
                "Excerpt",
                "https://example.invalid/rmd/README.md#L1-L2"
            );

        int64 grounded_turn =
            store.commit_turn (
                grounded_id,
                "question",
                "answer [S1]",
                "answer",
                true,
                21,
                { citation }
            );

        assert (grounded_turn == 0);

        int64 followup_turn =
            store.commit_turn (
                grounded_id,
                "follow-up",
                "plain",
                "plain",
                false,
                22,
                {}
            );

        assert (followup_turn == 1);

        store.update_title (
            grounded_id,
            "Updated title",
            23
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_domain_bridge_rejects_wrong_provenance ()
{
    string root = new_temp_root ();

    try {
        var store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );
        var conversation =
            new AskTheModel.OllamaConversation ();

        var repository =
            new AskTheModel.ConversationPersistenceRepository (
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567"
            );

        string conversation_id =
            store.create_conversation (
                "Grounded",
                20,
                "model-b",
                null,
                7,
                { repository }
            );

        var citation =
            new AskTheModel.ConversationPersistenceCitation (
                "S1",
                "rmd",
                "0.1.0",
                "1111111111111111111111111111111111111111",
                "source-1",
                "README.md",
                "lines 1-2"
            );

        bool rejected = false;

        try {
            AskTheModel.ConversationTurnCommitter.commit (
                store,
                conversation_id,
                conversation,
                "question",
                "answer [S1]",
                "answer",
                true,
                21,
                { citation }
            );
        } catch (Error error) {
            rejected = true;
        }

        assert (rejected);
        assert (conversation.message_count () == 0);

        int64 retry_turn =
            AskTheModel.ConversationTurnCommitter.commit (
                store,
                conversation_id,
                conversation,
                "question",
                "plain answer",
                "plain answer",
                false,
                22,
                {}
            );

        assert (retry_turn == 0);
        assert (conversation.message_count () == 2);

        var unsaved =
            new AskTheModel.OllamaConversation ();

        int64 unsaved_turn =
            AskTheModel.ConversationTurnCommitter.commit (
                null,
                null,
                unsaved,
                "local",
                "answer",
                "answer",
                false,
                23,
                {}
            );

        assert (unsaved_turn == -1);
        assert (unsaved.message_count () == 2);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_snapshot_restore_provider_history ()
{
    string root = new_temp_root ();

    try {
        var store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        var repository =
            new AskTheModel.ConversationPersistenceRepository (
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567"
            );

        string conversation_id =
            store.create_conversation (
                "Grounded",
                20,
                "model-b",
                "digest-b",
                7,
                { repository }
            );

        var citation =
            new AskTheModel.ConversationPersistenceCitation (
                "S1",
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567",
                "source-1",
                "README.md",
                "lines 1-2",
                "Title",
                "Excerpt",
                "https://example.invalid/rmd/README.md#L1-L2"
            );

        assert (
            store.commit_turn (
                conversation_id,
                "question",
                "answer [S1]",
                "answer",
                true,
                21,
                { citation }
            ) == 0
        );
        assert (
            store.commit_turn (
                conversation_id,
                "follow-up",
                "plain follow-up",
                "plain follow-up",
                false,
                22,
                {}
            ) == 1
        );

        store.update_title (
            conversation_id,
            "Updated",
            23
        );

        AskTheModel.ConversationPersistenceSummary[] summaries =
            store.list_conversations ();

        assert (summaries.length == 1);
        assert (
            summaries[0].conversation_id ==
            conversation_id
        );
        assert (summaries[0].title == "Updated");
        assert (summaries[0].updated_at_us == 23);
        assert (!summaries[0].archived);

        AskTheModel.ConversationPersistenceSnapshot snapshot =
            store.load_snapshot (
                conversation_id
            );

        assert (
            snapshot.conversation_id ==
            conversation_id
        );
        assert (snapshot.title == "Updated");
        assert (snapshot.model_name == "model-b");
        assert (snapshot.model_digest == "digest-b");
        assert (
            snapshot.repository_generation_id == 7
        );
        assert (snapshot.repositories.length == 1);
        assert (
            snapshot.repositories[0].repository_id ==
            "rmd"
        );
        assert (snapshot.messages.length == 4);
        assert (snapshot.messages[0].role == "user");
        assert (
            snapshot.messages[0].provider_content ==
            "question"
        );
        assert (
            snapshot.messages[1].role ==
            "assistant"
        );
        assert (
            snapshot.messages[1].provider_content ==
            "answer [S1]"
        );
        assert (
            snapshot.messages[1].display_content ==
            "answer"
        );
        assert (snapshot.messages[1].grounded);
        assert (
            snapshot.messages[1].citations.length ==
            1
        );
        assert (
            snapshot.messages[1].citations[0].label ==
            "S1"
        );
        assert (
            snapshot.messages[1].citations[0].immutable_permalink ==
            "https://example.invalid/rmd/README.md#L1-L2"
        );

        string[] available_names = {
            "other-model",
            "model-b"
        };
        string[] available_digests = {
            "other-digest",
            "digest-b"
        };
        AskTheModel.ConversationPersistenceRepository[]
            qualified_repositories = {
                new AskTheModel.ConversationPersistenceRepository (
                    "rmd",
                    "0.1.0",
                    "0123456789abcdef0123456789abcdef01234567"
                )
            };

        assert (
            snapshot.model_identity_available (
                available_names,
                available_digests
            )
        );

        snapshot.require_continuation_identity (
            available_names,
            available_digests,
            7,
            qualified_repositories
        );

        bool wrong_digest_rejected = false;

        try {
            snapshot.require_continuation_identity (
                { "model-b" },
                { "wrong-digest" },
                7,
                qualified_repositories
            );
        } catch (Error error) {
            wrong_digest_rejected = true;
        }

        assert (wrong_digest_rejected);

        bool wrong_generation_rejected = false;

        try {
            snapshot.require_continuation_identity (
                available_names,
                available_digests,
                8,
                qualified_repositories
            );
        } catch (Error error) {
            wrong_generation_rejected = true;
        }

        assert (wrong_generation_rejected);

        bool wrong_repository_rejected = false;

        try {
            snapshot.require_continuation_identity (
                available_names,
                available_digests,
                7,
                {
                    new AskTheModel.ConversationPersistenceRepository (
                        "rmd",
                        "0.1.0",
                        "1111111111111111111111111111111111111111"
                    )
                }
            );
        } catch (Error error) {
            wrong_repository_rejected = true;
        }

        assert (wrong_repository_rejected);

        var restored =
            new AskTheModel.OllamaConversation ();

        snapshot.restore_provider_history (
            restored
        );

        assert (restored.message_count () == 4);
        assert (restored.role_at (0) == "user");
        assert (
            restored.content_at (0) ==
            "question"
        );
        assert (
            restored.role_at (1) ==
            "assistant"
        );
        assert (
            restored.content_at (1) ==
            "answer [S1]"
        );
        assert (restored.role_at (2) == "user");
        assert (
            restored.content_at (2) ==
            "follow-up"
        );
        assert (
            restored.content_at (3) ==
            "plain follow-up"
        );
        assert (restored.content_at (4) == null);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_archive_delete_domain_lifecycle ()
{
    string root = new_temp_root ();

    try {
        var store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        string conversation_id =
            store.create_conversation (
                "Lifecycle",
                100,
                "model-a",
                "digest-a",
                0,
                {}
            );

        assert (
            store.commit_turn (
                conversation_id,
                "hello",
                "world",
                "world",
                false,
                101,
                {}
            ) == 0
        );

        string automatic_export_path =
            GLib.Path.build_filename (
                store.export_root,
                "%s.json".printf (
                    conversation_id
                )
            );

        assert (
            !GLib.FileUtils.test (
                automatic_export_path,
                GLib.FileTest.EXISTS
            )
        );

        store.set_archived (
            conversation_id,
            true,
            102
        );

        string archived_export;
        assert (
            GLib.FileUtils.get_contents (
                automatic_export_path,
                out archived_export
            )
        );
        assert (
            archived_export.index_of (
                "\"archived\" : true"
            ) >= 0
        );

        var archived =
            store.load_snapshot (
                conversation_id
            );
        assert (archived.archived);
        assert (archived.updated_at_us == 102);

        store.delete_conversation (
            conversation_id
        );

        assert (
            !GLib.FileUtils.test (
                automatic_export_path,
                GLib.FileTest.EXISTS
            )
        );

        bool missing = false;

        try {
            store.load_snapshot (
                conversation_id
            );
        } catch (Error error) {
            missing = true;
        }

        assert (missing);
        assert (
            store.list_conversations ().length == 0
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}


private static void
test_deterministic_read_only_export ()
{
    string root = new_temp_root ();

    try {
        var store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        var repository =
            new AskTheModel.ConversationPersistenceRepository (
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567"
            );

        string conversation_id =
            store.create_conversation (
                "Quoted \"title\"\nline",
                200,
                "model-export",
                null,
                9,
                { repository }
            );

        var citation =
            new AskTheModel.ConversationPersistenceCitation (
                "S1",
                "rmd",
                "0.1.0",
                "0123456789abcdef0123456789abcdef01234567",
                "source-1",
                "README.md",
                "lines 1-2",
                null,
                "Excerpt \"quoted\"\nΔ",
                "https://example.invalid/rmd/README.md#L1-L2"
            );

        assert (
            store.commit_turn (
                conversation_id,
                "question",
                "answer [S1]",
                "answer",
                true,
                201,
                { citation }
            ) == 0
        );
        assert (
            store.commit_turn (
                conversation_id,
                "follow-up",
                "plain provider",
                "plain display",
                false,
                202,
                {}
            ) == 1
        );

        var before = store.list_conversations ()[0];
        string first =
            store.export_conversation_json (
                conversation_id
            );
        string second =
            store.export_conversation_json (
                conversation_id
            );
        var after = store.list_conversations ()[0];

        string automatic_export_path =
            GLib.Path.build_filename (
                store.export_root,
                "%s.json".printf (
                    conversation_id
                )
            );
        string automatic_export;

        assert (
            !GLib.FileUtils.test (
                automatic_export_path,
                GLib.FileTest.EXISTS
            )
        );

        store.set_archived (
            conversation_id,
            true,
            203
        );

        string archived_json =
            store.export_conversation_json (
                conversation_id
            );

        assert (
            GLib.FileUtils.get_contents (
                automatic_export_path,
                out automatic_export
            )
        );

        assert (automatic_export == archived_json);
        assert (first == second);
        assert (first.has_suffix ("\n"));
        assert (
            first.index_of (
                AskTheModel.ConversationExport.FORMAT
            ) >= 0
        );
        assert (
            first.index_of (
                "\"schema_version\""
            ) >= 0
        );
        assert (
            first.index_of (
                "\"repository_generation_id\""
            ) >= 0
        );
        assert (
            first.index_of (
                "0123456789abcdef0123456789abcdef01234567"
            ) >= 0
        );
        assert (
            first.index_of (
                "https://example.invalid/rmd/README.md#L1-L2"
            ) >= 0
        );
        assert (
            first.index_of ("answer [S1]") >= 0
        );
        assert (
            first.index_of ("plain display") >= 0
        );
        assert (
            first.index_of ("\\\"quoted\\\"") >= 0
        );

        var parser = new Json.Parser ();
        parser.load_from_data (first, -1);

        Json.Node root_node = parser.get_root ();
        Json.Object envelope = root_node.get_object ();

        assert (
            envelope.get_string_member ("format") ==
            AskTheModel.ConversationExport.FORMAT
        );
        assert (
            envelope.get_int_member ("schema_version") ==
            AskTheModel.ConversationExport.SCHEMA_VERSION
        );

        Json.Object exported =
            envelope.get_object_member ("conversation");
        assert (
            exported.get_string_member ("conversation_id") ==
            conversation_id
        );
        assert (
            exported.get_int_member (
                "repository_generation_id"
            ) == 9
        );

        Json.Object model =
            exported.get_object_member ("model");
        assert (
            model.get_string_member ("name") ==
            "model-export"
        );
        assert (model.get_null_member ("digest"));

        Json.Array repositories =
            exported.get_array_member ("repositories");
        assert (repositories.get_length () == 1);
        assert (
            repositories.get_object_element (0)
                .get_string_member ("repository_id") ==
            "rmd"
        );

        Json.Array messages =
            exported.get_array_member ("messages");
        assert (messages.get_length () == 4);
        assert (
            messages.get_object_element (1)
                .get_boolean_member ("grounded")
        );
        assert (
            messages.get_object_element (1)
                .get_array_member ("citations")
                .get_length () == 1
        );
        assert (
            messages.get_object_element (1)
                .get_array_member ("citations")
                .get_object_element (0)
                .get_null_member ("title")
        );

        assert (
            before.updated_at_us ==
            after.updated_at_us
        );
        assert (
            before.archived ==
            after.archived
        );
        assert (
            before.open_on_startup ==
            after.open_on_startup
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}


private static void
test_existing_exports_resynchronize_on_reopen ()
{
    string root = new_temp_root ();

    try {
        var first_store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        string conversation_id =
            first_store.create_conversation (
                "Existing",
                300,
                "model-existing",
                "digest-existing",
                0,
                {}
            );

        assert (
            first_store.commit_turn (
                conversation_id,
                "hello",
                "world",
                "world",
                false,
                301,
                {}
            ) == 0
        );

        first_store.set_archived (
            conversation_id,
            true,
            302
        );

        string export_path =
            GLib.Path.build_filename (
                first_store.export_root,
                "%s.json".printf (
                    conversation_id
                )
            );

        assert (
            GLib.FileUtils.test (
                export_path,
                GLib.FileTest.EXISTS
            )
        );
        assert (GLib.FileUtils.remove (export_path) == 0);
        assert (
            !GLib.FileUtils.test (
                export_path,
                GLib.FileTest.EXISTS
            )
        );

        var reopened_store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        string synchronized_export;
        assert (
            GLib.FileUtils.get_contents (
                export_path,
                out synchronized_export
            )
        );
        assert (
            synchronized_export ==
            reopened_store.export_conversation_json (
                conversation_id
            )
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_unarchived_conversations_are_discarded_on_reopen ()
{
    string root = new_temp_root ();

    try {
        string conversation_id;

        {
            var first_store =
                new AskTheModel.ConversationPersistenceStore (
                    root
                );

            conversation_id =
                first_store.create_conversation (
                    "Temporary",
                    400,
                    "model-temporary",
                    null,
                    0,
                    {}
                );

            assert (
                first_store.commit_turn (
                    conversation_id,
                    "temporary",
                    "answer",
                    "answer",
                    false,
                    401,
                    {}
                ) == 0
            );
        }

        var reopened_store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        assert (
            reopened_store.list_conversations ().length == 0
        );

        bool missing = false;

        try {
            reopened_store.load_snapshot (
                conversation_id
            );
        } catch (Error error) {
            missing = true;
        }

        assert (missing);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_permanent_delete_fails_closed_when_export_cannot_be_removed ()
{
    string root = new_temp_root ();

    try {
        var store =
            new AskTheModel.ConversationPersistenceStore (
                root
            );

        string conversation_id =
            store.create_conversation (
                "Protected archive",
                500,
                "model-delete",
                null,
                0,
                {}
            );

        assert (
            store.commit_turn (
                conversation_id,
                "keep",
                "me",
                "me",
                false,
                501,
                {}
            ) == 0
        );

        store.set_archived (
            conversation_id,
            true,
            502
        );

        string export_path =
            GLib.Path.build_filename (
                store.export_root,
                "%s.json".printf (
                    conversation_id
                )
            );

        assert (GLib.FileUtils.remove (export_path) == 0);
        assert (GLib.DirUtils.create (export_path, 0700) == 0);

        string blocker =
            GLib.Path.build_filename (
                export_path,
                "blocker"
            );
        GLib.FileUtils.set_contents (
            blocker,
            "block"
        );

        bool failed = false;

        try {
            store.delete_conversation (
                conversation_id
            );
        } catch (Error error) {
            failed = true;
        }

        assert (failed);
        assert (
            store.load_snapshot (
                conversation_id
            ).archived
        );

        assert (GLib.FileUtils.remove (blocker) == 0);
        assert (GLib.DirUtils.remove (export_path) == 0);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

public static int
main (string[] args)
{
    Test.init (ref args);

    Test.add_func (
        "/conversation-persistence/domain-write-bridge",
        test_domain_write_bridge
    );
    Test.add_func (
        "/conversation-persistence/rejects-wrong-provenance",
        test_domain_bridge_rejects_wrong_provenance
    );
    Test.add_func (
        "/conversation-persistence/snapshot-restore-provider-history",
        test_snapshot_restore_provider_history
    );
    Test.add_func (
        "/conversation-persistence/archive-delete-lifecycle",
        test_archive_delete_domain_lifecycle
    );
    Test.add_func (
        "/conversation-persistence/deterministic-read-only-export",
        test_deterministic_read_only_export
    );
    Test.add_func (
        "/conversation-persistence/existing-export-resync",
        test_existing_exports_resynchronize_on_reopen
    );
    Test.add_func (
        "/conversation-persistence/unarchived-discard-on-reopen",
        test_unarchived_conversations_are_discarded_on_reopen
    );
    Test.add_func (
        "/conversation-persistence/permanent-delete-export-fail-closed",
        test_permanent_delete_fails_closed_when_export_cannot_be_removed
    );

    return Test.run ();
}
