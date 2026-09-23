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

private static void
test_repository_identity_accessors ()
{
    const string sha =
        "0123456789abcdef0123456789abcdef01234567";
    string snapshot_root = "";
    string cache_root = "";
    string index_path = "";
    string detected_version = "";
    var grounding =
        new AskTheModel.ConversationGrounding ();

    try {
        assert (
            AskTheModelTest.ConversationFixtureNative.create (
                "rmd",
                "RMD",
                "Romanian Monetary Dynamics",
                "0.1.0",
                sha,
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
                sha,
                snapshot_root,
                index_path
            )
        );
        grounding.pin_repository_generation (9);
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (grounding.repository_count () == 1);
    assert (grounding.repository_id_at (0) == "rmd");
    assert (grounding.repository_version_at (0) == "0.1.0");
    assert (
        grounding.repository_sha_at (0) ==
        sha
    );
    assert (grounding.repository_id_at (1) == null);
    assert (grounding.repository_version_at (1) == null);
    assert (grounding.repository_sha_at (1) == null);

    AskTheModelTest.ConversationFixtureNative.remove (
        snapshot_root,
        cache_root
    );
}

private static void
test_generation_pin_is_immutable_after_freeze ()
{
    var grounding =
        new AskTheModel.ConversationGrounding ();

    try {
        grounding.pin_repository_generation (7);
        assert (grounding.freeze ());
    } catch (Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }

    assert (
        grounding.repository_generation_id () == 7
    );

    bool rejected = false;

    try {
        grounding.pin_repository_generation (8);
    } catch (Error error) {
        rejected = true;
        assert (
            error.message ==
            "Repository generation cannot change after conversation grounding is frozen."
        );
    }

    assert (rejected);
    assert (
        grounding.repository_generation_id () == 7
    );
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
    Test.add_func (
        "/conversation-grounding-vala/repository-identity-accessors",
        test_repository_identity_accessors
    );
    Test.add_func (
        "/conversation-grounding-vala/generation-pin-immutable",
        test_generation_pin_is_immutable_after_freeze
    );

    return Test.run ();
}
