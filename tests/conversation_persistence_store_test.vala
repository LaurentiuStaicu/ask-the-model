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
                "Excerpt"
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

    return Test.run ();
}
