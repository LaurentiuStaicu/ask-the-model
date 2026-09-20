using GLib;

private static void
test_requires_frozen_grounding ()
{
    var grounding = new AskTheModel.ConversationGrounding ();
    var session = new AskTheModel.ConversationSession ();
    bool rejected = false;

    try {
        session.begin (grounding);
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
        session.begin (grounding);
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
        session.begin (first);
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool rejected = false;

    try {
        session.begin (second);
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
        session.begin (second);
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
        session.begin (grounding);

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

    return Test.run ();
}
