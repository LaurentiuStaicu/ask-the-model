using GLib;

private static void
test_domain_bridge_zero_and_repository_scope ()
{
    string root;

    try {
        root = DirUtils.make_tmp (
            "atm-conversation-domain-XXXXXX"
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    var store =
        new AskTheModel.ConversationPersistenceStore (
            root
        );

    var zero_grounding =
        new AskTheModel.ConversationGrounding ();
    var zero_session =
        new AskTheModel.ConversationSession ();

    try {
        assert (zero_grounding.freeze ());
        zero_session.begin (
            zero_grounding,
            "model-zero",
            "digest-zero"
        );

        string zero_id =
            store.create_for_session (
                zero_session
            );

        assert (zero_id.length > 0);

        int64 zero_turn =
            store.commit_turn (
                zero_id,
                "hello",
                "world",
                "world",
                false
            );

        assert (zero_turn == 0);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    const string repository_sha =
        "0123456789abcdef0123456789abcdef01234567";
    string snapshot_root = "";
    string cache_root = "";
    string index_path = "";
    string detected_version = "";
    var grounding =
        new AskTheModel.ConversationGrounding ();
    var session =
        new AskTheModel.ConversationSession ();

    try {
        assert (
            AskTheModelTest.ConversationFixtureNative.create (
                "rmd",
                "RMD",
                "Romanian Monetary Dynamics",
                "0.1.0",
                repository_sha,
                out snapshot_root,
                out cache_root,
                out index_path,
                out detected_version
            )
        );
        assert (detected_version == "0.1.0");
        assert (
            grounding.add_ready_repository (
                "rmd",
                detected_version,
                repository_sha,
                snapshot_root,
                index_path
            )
        );
        grounding.pin_repository_generation (7);
        assert (grounding.freeze ());
        session.begin (
            grounding,
            "model-grounded",
            "digest-grounded"
        );

        string conversation_id =
            store.create_for_session (
                session,
                "Grounded"
            );

        var resolution =
            new AskTheModel.CitationResolution ();

        resolution.add_citation (
            new AskTheModel.CitationReference (
                "S1",
                "rmd",
                "0.1.0",
                repository_sha,
                "source-1",
                "README.md",
                "lines 1-2",
                "Title",
                "Excerpt"
            )
        );

        int64 turn_no =
            store.commit_turn (
                conversation_id,
                "question",
                "answer [S1]",
                "answer",
                true,
                resolution
            );

        assert (turn_no == 0);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    string path = store.path ();
    assert (
        FileUtils.test (
            path,
            FileTest.EXISTS
        )
    );

    AskTheModelTest.ConversationFixtureNative.remove (
        snapshot_root,
        cache_root
    );
}

public static int
main (string[] args)
{
    Test.init (ref args);

    Test.add_func (
        "/conversation-persistence/domain-bridge",
        test_domain_bridge_zero_and_repository_scope
    );

    return Test.run ();
}
