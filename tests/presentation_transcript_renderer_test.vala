using AskTheModel;

private void
test_transcript_renderer () {
    var buffer = new Gtk.TextBuffer (null);

    PresentationTranscriptRenderer.append_plain_message (
        buffer,
        TranscriptSpeaker.USER,
        "Hello",
        false
    );

    bool fallback =
        PresentationTranscriptRenderer.append_assistant_message (
            buffer,
            "## Heading\n\n" +
            "* **First**\n" +
            "* Second with `code`",
            false
        );

    assert (!fallback);

    string visible = buffer.text;
    assert (
        visible ==
        "You: Hello\n\n" +
        "Assistant: Heading\n\n" +
        "• First\n" +
        "• Second with code"
    );
    assert (!visible.contains ("**"));
    assert (!visible.contains ("`"));

    var user_tag =
        buffer.tag_table.lookup (
            PresentationTranscriptRenderer.TAG_SPEAKER_USER
        );
    var assistant_tag =
        buffer.tag_table.lookup (
            PresentationTranscriptRenderer.TAG_SPEAKER_ASSISTANT
        );
    var heading_tag =
        buffer.tag_table.lookup (
            PresentationTranscriptRenderer.TAG_HEADING
        );
    var inline_code_tag =
        buffer.tag_table.lookup (
            PresentationTranscriptRenderer.TAG_INLINE_CODE
        );

    assert (user_tag != null);
    assert (assistant_tag != null);
    assert (heading_tag != null);
    assert (inline_code_tag != null);

    Gtk.TextIter iter;

    buffer.get_iter_at_offset (out iter, 1);
    assert (iter.has_tag (user_tag));

    int assistant_offset =
        visible.index_of ("Assistant:");
    assert (assistant_offset >= 0);
    buffer.get_iter_at_offset (
        out iter,
        assistant_offset + 1
    );
    assert (iter.has_tag (assistant_tag));

    int heading_offset =
        visible.index_of ("Heading");
    assert (heading_offset >= 0);
    buffer.get_iter_at_offset (
        out iter,
        heading_offset + 1
    );
    assert (iter.has_tag (heading_tag));

    int code_offset =
        visible.index_of ("code");
    assert (code_offset >= 0);
    buffer.get_iter_at_offset (
        out iter,
        code_offset + 1
    );
    assert (iter.has_tag (inline_code_tag));

    assert (
        user_tag.weight ==
        (int) Pango.Weight.BOLD
    );
    assert (
        assistant_tag.weight ==
        (int) Pango.Weight.BOLD
    );
    assert (
        heading_tag.weight ==
        (int) Pango.Weight.NORMAL
    );
    assert (
        heading_tag.style ==
        Pango.Style.NORMAL
    );
}

private void
test_plain_messages_are_literal () {
    var buffer = new Gtk.TextBuffer (null);

    PresentationTranscriptRenderer.append_plain_message (
        buffer,
        TranscriptSpeaker.USER,
        "**literal user markup**",
        true
    );
    PresentationTranscriptRenderer.append_plain_message (
        buffer,
        TranscriptSpeaker.SYSTEM,
        "<b>literal system text</b>",
        true
    );

    assert (
        buffer.text ==
        "You: **literal user markup**\n\n" +
        "System: <b>literal system text</b>"
    );
}

int
main (string[] args) {
    Gtk.init ();
    Test.init (ref args);

    Test.add_func (
        "/presentation/transcript-renderer",
        test_transcript_renderer
    );
    Test.add_func (
        "/presentation/plain-message-literal",
        test_plain_messages_are_literal
    );

    return Test.run ();
}
