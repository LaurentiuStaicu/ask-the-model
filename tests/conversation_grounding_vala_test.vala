using GLib;

private static void
test_zero_scope_freeze ()
{
    var grounding = new AskTheModel.ConversationGrounding ();

    assert (!grounding.is_frozen ());
    assert (grounding.repository_count () == 0);

    try {
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (grounding.is_frozen ());
    assert (grounding.repository_count () == 0);

    bool needs_clarification;
    string? system_instructions;
    string? evidence_text;
    string? post_evidence_reminder;

    try {
        bool has_grounding = grounding.prepare_turn (
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
}

private static void
test_frozen_scope_rejects_late_repository ()
{
    var grounding = new AskTheModel.ConversationGrounding ();

    try {
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool rejected = false;

    try {
        grounding.add_ready_repository (
            "ewd",
            "0.1.0",
            "1111111111111111111111111111111111111111",
            "/tmp/not-used-after-freeze",
            "/tmp/not-used-after-freeze.sqlite"
        );
    } catch (Error error) {
        rejected = true;
        assert (
            error.message ==
            "Repository scope is frozen for this conversation."
        );
    }

    assert (rejected);
    assert (grounding.repository_count () == 0);
}

private static void
test_zero_scope_has_no_citation_map ()
{
    var grounding = new AskTheModel.ConversationGrounding ();

    try {
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    bool rejected = false;

    try {
        grounding.resolve_turn_citations (
            "No grounded turn [S1]."
        );
    } catch (Error error) {
        rejected = true;
        assert (
            error.message ==
            "No grounded conversation turn is available for citation resolution."
        );
    }

    assert (rejected);
}

public static int
main (string[] args)
{
    Test.init (ref args);

    Test.add_func (
        "/conversation-grounding-vala/zero-scope-freeze",
        test_zero_scope_freeze
    );
    Test.add_func (
        "/conversation-grounding-vala/frozen-rejects-late-repository",
        test_frozen_scope_rejects_late_repository
    );
    Test.add_func (
        "/conversation-grounding-vala/zero-scope-no-citation-map",
        test_zero_scope_has_no_citation_map
    );

    return Test.run ();
}
