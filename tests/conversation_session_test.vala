using GLib;

private static void
test_requires_frozen_grounding ()
{
    var grounding = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();
    bool rejected = false;

    try {
        session.begin (grounding, "test-model");
    } catch (Error error) {
        rejected = true;
        assert (
            error.message ==
            "Conversation grounding must be frozen before the session starts."
        );
    }

    assert (rejected);
    assert (!session.is_active ());
    assert (session.repository_count () == 0);
}

private static void
test_zero_scope_start_prepare_reset ()
{
    var grounding = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();

    try {
        assert (grounding.freeze ());
        session.begin (grounding, "test-model");
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (session.is_active ());
    assert (session.repository_count () == 0);

    bool needs_clarification;
    string? system_instructions;
    string? evidence_text;
    string? post_evidence_reminder;

    try {
        bool has_grounding = session.prepare_turn (
            "ordinary local chat",
            out needs_clarification,
            out system_instructions,
            out evidence_text,
            out post_evidence_reminder
        );

        assert (!has_grounding);
        assert (!needs_clarification);
        assert (system_instructions == null);
        assert (evidence_text == null);
        assert (post_evidence_reminder == null);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    session.reset ();
    assert (!session.is_active ());
    assert (session.repository_count () == 0);
}

private static void
test_rejects_second_begin_until_reset ()
{
    var first = new AskTheModel.ConversationGrounding ();
    var second = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();

    try {
        assert (first.freeze ());
        assert (second.freeze ());
        session.begin (first, "first-model");
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool rejected = false;

    try {
        session.begin (second, "second-model");
    } catch (Error error) {
        rejected = true;
        assert (
            error.message ==
            "Conversation session is already active."
        );
    }

    assert (rejected);
    assert (session.is_active ());

    session.reset ();

    try {
        session.begin (second, "second-model");
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (session.is_active ());
}

private static void
test_zero_scope_has_no_committable_grounded_turn ()
{
    var grounding = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();

    try {
        assert (grounding.freeze ());
        session.begin (grounding, "test-model");

        bool needs_clarification;
        string? system_instructions;
        string? evidence_text;
        string? post_evidence_reminder;

        bool has_grounding = session.prepare_turn (
            "ordinary local chat",
            out needs_clarification,
            out system_instructions,
            out evidence_text,
            out post_evidence_reminder
        );

        assert (!has_grounding);
        assert (!needs_clarification);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool commit_rejected = false;

    try {
        session.commit_turn ();
    } catch (Error error) {
        commit_rejected = true;
        assert (
            error.message ==
            "No prepared grounded turn is available to commit."
        );
    }

    assert (commit_rejected);

    session.abort_turn ();
    assert (session.is_active ());
}

private static void
test_model_identity_is_pinned_until_reset ()
{
    var first = new AskTheModel.ConversationGrounding ();
    var second = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();

    try {
        assert (first.freeze ());
        assert (second.freeze ());
        session.begin (
            first,
            "model-a",
            "digest-a"
        );

        assert (session.model_name () == "model-a");
        assert (session.model_digest () == "digest-a");
        session.require_model ("model-a", "digest-a");
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool mismatch_rejected = false;

    try {
        session.require_model ("model-b");
    } catch (Error error) {
        mismatch_rejected = true;
        assert (
            error.message ==
            "The active AI model differs from the model pinned to this conversation."
        );
    }

    assert (mismatch_rejected);

    bool digest_mismatch_rejected = false;

    try {
        session.require_model ("model-a", "digest-b");
    } catch (Error error) {
        digest_mismatch_rejected = true;
        assert (
            error.message ==
            "The active AI model digest differs from the model pinned to this conversation."
        );
    }

    assert (digest_mismatch_rejected);

    session.reset ();
    assert (session.model_name () == null);
    assert (session.model_digest () == null);

    try {
        session.begin (second, "model-b");
        assert (session.model_name () == "model-b");
        assert (session.model_digest () == null);
        session.require_model (" model-b ");
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_empty_model_is_rejected ()
{
    var grounding = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();

    try {
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool rejected = false;

    try {
        session.begin (grounding, "   ");
    } catch (Error error) {
        rejected = true;
        assert (
            error.message ==
            "Conversation AI model must be known before the session starts."
        );
    }

    assert (rejected);
    assert (!session.is_active ());
    assert (session.model_name () == null);
}

private static void
test_repository_identity_is_exposed_by_session ()
{
    const string sha =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
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
                "cbd",
                "CBD",
                "Cognitive Belief Dynamics",
                "0.2.0",
                sha,
                out snapshot_root,
                out cache_root,
                out index_path,
                out detected_version
            )
        );
        assert (detected_version == "0.2.0");
        assert (
            grounding.add_ready_repository (
                "cbd",
                detected_version,
                sha,
                snapshot_root,
                index_path
            )
        );
        grounding.pin_repository_generation (17);
        assert (grounding.freeze ());
        session.begin (
            grounding,
            "test-model",
            "test-digest"
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (session.repository_count () == 1);
    assert (session.repository_generation_id () == 17);
    assert (session.repository_id_at (0) == "cbd");
    assert (session.repository_version_at (0) == "0.2.0");
    assert (
        session.repository_sha_at (0) ==
        sha
    );
    assert (session.repository_id_at (1) == null);

    session.reset ();
    assert (session.repository_id_at (0) == null);

    AskTheModelTest.ConversationFixtureNative.remove (
        snapshot_root,
        cache_root
    );
}

private static void
test_repository_generation_is_pinned_until_reset ()
{
    var grounding = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();

    try {
        grounding.pin_repository_generation (42);
        assert (grounding.freeze ());
        session.begin (
            grounding,
            "test-model"
        );
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (
        grounding.repository_generation_id () == 42
    );
    assert (
        session.repository_generation_id () == 42
    );

    session.reset ();
    assert (
        session.repository_generation_id () == 0
    );
}

public static int
main (string[] args)
{
    Test.init (ref args);

    Test.add_func (
        "/conversation-session/requires-frozen-grounding",
        test_requires_frozen_grounding
    );
    Test.add_func (
        "/conversation-session/zero-scope-start-prepare-reset",
        test_zero_scope_start_prepare_reset
    );
    Test.add_func (
        "/conversation-session/rejects-second-begin-until-reset",
        test_rejects_second_begin_until_reset
    );
    Test.add_func (
        "/conversation-session/zero-scope-no-grounded-commit",
        test_zero_scope_has_no_committable_grounded_turn
    );
    Test.add_func (
        "/conversation-session/model-pinned-until-reset",
        test_model_identity_is_pinned_until_reset
    );
    Test.add_func (
        "/conversation-session/empty-model-rejected",
        test_empty_model_is_rejected
    );
    Test.add_func (
        "/conversation-session/repository-identity-exposed",
        test_repository_identity_is_exposed_by_session
    );
    Test.add_func (
        "/conversation-session/repository-generation-pinned",
        test_repository_generation_is_pinned_until_reset
    );

    return Test.run ();
}
