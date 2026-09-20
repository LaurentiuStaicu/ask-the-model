using GLib;

private static void
test_zero_scope_freeze ()
{
    var grounding = new AskTheModel.ConversationGrounding ();

    assert (!grounding.is_frozen);
    assert (grounding.repository_count == 0);

    try {
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (grounding.is_frozen);
    assert (grounding.repository_count == 0);
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
    assert (grounding.repository_count == 0);
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

    return Test.run ();
}
