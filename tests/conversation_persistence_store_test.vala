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

    return Test.run ();
}
