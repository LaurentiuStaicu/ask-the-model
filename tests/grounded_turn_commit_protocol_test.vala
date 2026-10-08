using GLib;

namespace GroundedTurnFixtureNative {
    [CCode (
        cname = "atm_grounded_turn_fixture_create",
        cheader_filename = "grounded_turn_test_support.h"
    )]
    public static extern bool create (
        out string cache_root,
        out string snapshot_root,
        out string index_path,
        out string version,
        out string snapshot_sha
    ) throws GLib.Error;

    [CCode (
        cname = "atm_grounded_turn_fixture_destroy",
        cheader_filename = "grounded_turn_test_support.h"
    )]
    public static extern void destroy (
        string cache_root,
        string snapshot_root,
        string index_path,
        string version,
        string snapshot_sha
    );
}

private static AskTheModel.ConversationSession
new_session (
    out string cache_root,
    out string snapshot_root,
    out string index_path,
    out string version,
    out string snapshot_sha
) throws GLib.Error {
    GroundedTurnFixtureNative.create (
        out cache_root,
        out snapshot_root,
        out index_path,
        out version,
        out snapshot_sha
    );

    var grounding =
        new AskTheModel.ConversationGrounding ();

    assert (
        grounding.add_ready_repository (
            "ewd",
            version,
            snapshot_sha,
            snapshot_root,
            index_path
        )
    );

    grounding.pin_repository_generation (7);
    assert (grounding.freeze ());

    var session =
        new AskTheModel.ConversationSession ();
    session.begin (
        grounding,
        "model-a",
        "digest-a"
    );

    return session;
}

private static void
test_success_commits_durable_boundary_before_session () {
    string cache_root;
    string snapshot_root;
    string index_path;
    string version;
    string snapshot_sha;

    try {
        var session = new_session (
            out cache_root,
            out snapshot_root,
            out index_path,
            out version,
            out snapshot_sha
        );

        var store =
            new AskTheModel.ConversationPersistenceStore (
                cache_root
            );

        string conversation_id =
            store.create_conversation (
                "Grounded",
                1,
                "model-a",
                "digest-a",
                7,
                {
                    new AskTheModel.ConversationPersistenceRepository (
                        "ewd",
                        version,
                        snapshot_sha
                    )
                }
            );

        var conversation =
            new AskTheModel.OllamaConversation ();

        bool needs_clarification;
        string? system_instructions;
        string? evidence_text;
        string? post_evidence_reminder;

        assert (
            session.prepare_turn (
                "What is the current fixture status?",
                false,
                out needs_clarification,
                out system_instructions,
                out evidence_text,
                out post_evidence_reminder
            )
        );
        assert (!needs_clarification);

        var resolution =
            session.resolve_turn_citations (
                "The fixture status is current [S1]."
            );

        assert (resolution.citation_count () == 1);
        assert (resolution.unknown_label_count () == 0);
        assert (conversation.message_count () == 0);

        var citation =
            resolution.citation_at (0);
        assert (citation != null);

        var persistence_citation =
            new AskTheModel.ConversationPersistenceCitation (
                citation.label,
                citation.repository_id,
                citation.repository_version,
                citation.snapshot_sha,
                citation.logical_source_id,
                citation.source_path,
                citation.locator,
                citation.title,
                citation.excerpt,
                citation.immutable_permalink
            );

        int64 turn_no =
            AskTheModel.ConversationTurnCommitter.commit (
                store,
                conversation_id,
                conversation,
                "What is the current fixture status?",
                "The fixture status is current [S1].",
                "The fixture status is current.",
                true,
                2,
                { persistence_citation }
            );

        assert (turn_no == 0);
        assert (conversation.message_count () == 2);

        var before_session_commit =
            store.load_snapshot (conversation_id);
        assert (before_session_commit.messages.length == 2);
        assert (before_session_commit.messages[1].grounded);

        assert (session.commit_turn ());
        assert (conversation.message_count () == 2);

        var after_session_commit =
            store.load_snapshot (conversation_id);
        assert (after_session_commit.messages.length == 2);
        assert (after_session_commit.messages[1].citations.length == 1);

        GroundedTurnFixtureNative.destroy (
            cache_root,
            snapshot_root,
            index_path,
            version,
            snapshot_sha
        );
    } catch (GLib.Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

private static void
test_persistence_failure_is_aborted_and_retryable () {
    string cache_root;
    string snapshot_root;
    string index_path;
    string version;
    string snapshot_sha;

    try {
        var session = new_session (
            out cache_root,
            out snapshot_root,
            out index_path,
            out version,
            out snapshot_sha
        );

        var store =
            new AskTheModel.ConversationPersistenceStore (
                cache_root
            );

        string conversation_id =
            store.create_conversation (
                "Grounded",
                1,
                "model-a",
                "digest-a",
                7,
                {
                    new AskTheModel.ConversationPersistenceRepository (
                        "ewd",
                        version,
                        snapshot_sha
                    )
                }
            );

        var conversation =
            new AskTheModel.OllamaConversation ();

        bool needs_clarification;
        string? system_instructions;
        string? evidence_text;
        string? post_evidence_reminder;

        assert (
            session.prepare_turn (
                "What is the current fixture status?",
                false,
                out needs_clarification,
                out system_instructions,
                out evidence_text,
                out post_evidence_reminder
            )
        );

        var resolution =
            session.resolve_turn_citations (
                "The fixture status is current [S1]."
            );
        assert (resolution.citation_count () == 1);

        var bad_citation =
            new AskTheModel.ConversationPersistenceCitation (
                "S1",
                "ewd",
                version,
                "2222222222222222222222222222222222222222",
                "source-1",
                "STATUS.md",
                "lines 1-2"
            );

        bool rejected = false;
        try {
            AskTheModel.ConversationTurnCommitter.commit (
                store,
                conversation_id,
                conversation,
                "What is the current fixture status?",
                "The fixture status is current [S1].",
                "The fixture status is current.",
                true,
                3,
                { bad_citation }
            );
        } catch (GLib.Error error) {
            rejected = true;
        }

        assert (rejected);
        assert (conversation.message_count () == 0);
        assert (
            store.load_snapshot (conversation_id).messages.length == 0
        );

        session.abort_turn ();

        bool needs_clarification_retry;
        string? system_instructions_retry;
        string? evidence_text_retry;
        string? post_evidence_reminder_retry;

        assert (
            session.prepare_turn (
                "What is the current fixture status?",
                false,
                out needs_clarification_retry,
                out system_instructions_retry,
                out evidence_text_retry,
                out post_evidence_reminder_retry
            )
        );

        var retry_resolution =
            session.resolve_turn_citations (
                "The fixture status is current [S1]."
            );
        assert (retry_resolution.citation_count () == 1);

        var retry_citation =
            retry_resolution.citation_at (0);
        assert (retry_citation != null);

        var valid_persistence_citation =
            new AskTheModel.ConversationPersistenceCitation (
                retry_citation.label,
                retry_citation.repository_id,
                retry_citation.repository_version,
                retry_citation.snapshot_sha,
                retry_citation.logical_source_id,
                retry_citation.source_path,
                retry_citation.locator,
                retry_citation.title,
                retry_citation.excerpt,
                retry_citation.immutable_permalink
            );

        assert (
            AskTheModel.ConversationTurnCommitter.commit (
                store,
                conversation_id,
                conversation,
                "What is the current fixture status?",
                "The fixture status is current [S1].",
                "The fixture status is current.",
                true,
                4,
                { valid_persistence_citation }
            ) == 0
        );
        assert (conversation.message_count () == 2);
        assert (session.commit_turn ());

        GroundedTurnFixtureNative.destroy (
            cache_root,
            snapshot_root,
            index_path,
            version,
            snapshot_sha
        );
    } catch (GLib.Error error) {
        critical ("%s", error.message);
        assert_not_reached ();
    }
}

int
main (string[] args) {
    Test.init (ref args);

    Test.add_func (
        "/grounded-turn-commit-protocol/success",
        test_success_commits_durable_boundary_before_session
    );
    Test.add_func (
        "/grounded-turn-commit-protocol/persistence-failure-abort-retry",
        test_persistence_failure_is_aborted_and_retryable
    );

    return Test.run ();
}
