using AskTheModel;

private void
assert_rgba (
    Gdk.RGBA actual,
    string expected_spec
) {
    Gdk.RGBA expected = Gdk.RGBA ();
    assert (expected.parse (expected_spec));
    assert (actual.equal (expected));
}

private void
assert_tag_at (
    Gtk.TextBuffer buffer,
    string tag_name,
    int offset
) {
    unowned Gtk.TextTag? tag =
        buffer.tag_table.lookup (tag_name);
    assert (tag != null);

    Gtk.TextIter iter;
    buffer.get_iter_at_offset (
        out iter,
        offset
    );
    assert (iter.has_tag (tag));
}

private void
test_rendering_projection () {
    var buffer = new Gtk.TextBuffer (null);
    var renderer =
        new PresentationRenderer (buffer);

    renderer.append_user (
        "Întrebare **literală**"
    );
    renderer.append_assistant (
        "## Rezultat\n\n" +
        "Text **important** cu `code`.\n\n" +
        "- unu\n" +
        "- doi\n\n" +
        "> Citat\n\n" +
        "```txt\n" +
        "x = \"**literal**\"\n" +
        "```"
    );

    const string expected =
        "You: Întrebare **literală**\n\n" +
        "Assistant: Rezultat\n\n" +
        "Text important cu code.\n\n" +
        "• unu\n" +
        "• doi\n\n" +
        "Citat\n\n" +
        "x = \"**literal**\"\n";

    assert (buffer.text == expected);

    assert_tag_at (
        buffer,
        "atm-speaker-you",
        0
    );

    int assistant_offset =
        "You: Întrebare **literală**\n\n"
            .char_count ();

    assert_tag_at (
        buffer,
        "atm-speaker-assistant",
        assistant_offset
    );

    int heading_offset =
        (
            "You: Întrebare **literală**\n\n" +
            "Assistant: "
        ).char_count ();

    assert_tag_at (
        buffer,
        "atm-heading-2",
        heading_offset
    );

    int inline_code_byte =
        expected.index_of ("code.");
    assert (inline_code_byte >= 0);
    int inline_code_offset =
        expected.substring (
            0,
            inline_code_byte
        ).char_count ();

    assert_tag_at (
        buffer,
        "atm-inline-code",
        inline_code_offset
    );

    int quote_byte =
        expected.index_of ("Citat");
    assert (quote_byte >= 0);
    int quote_offset =
        expected.substring (
            0,
            quote_byte
        ).char_count ();

    assert_tag_at (
        buffer,
        "atm-quote",
        quote_offset
    );

    int block_code_byte =
        expected.index_of ("x =");
    assert (block_code_byte >= 0);
    int block_code_offset =
        expected.substring (
            0,
            block_code_byte
        ).char_count ();

    assert_tag_at (
        buffer,
        "atm-code-block",
        block_code_offset
    );
}

private void
test_palette_and_theme_update () {
    var buffer = new Gtk.TextBuffer (null);
    var renderer =
        new PresentationRenderer (buffer);

    unowned Gtk.TextTag? you =
        buffer.tag_table.lookup (
            "atm-speaker-you"
        );
    unowned Gtk.TextTag? assistant =
        buffer.tag_table.lookup (
            "atm-speaker-assistant"
        );
    unowned Gtk.TextTag? heading =
        buffer.tag_table.lookup (
            "atm-heading-2"
        );
    unowned Gtk.TextTag? quote =
        buffer.tag_table.lookup (
            "atm-quote"
        );
    unowned Gtk.TextTag? code =
        buffer.tag_table.lookup (
            "atm-code-block"
        );

    assert (you != null);
    assert (assistant != null);
    assert (heading != null);
    assert (quote != null);
    assert (code != null);

    assert (you.weight == 700);
    assert (assistant.weight == 700);
    assert_rgba (
        assistant.foreground_rgba,
        "#1B1B18"
    );
    assert_rgba (
        you.foreground_rgba,
        "#5F605C"
    );

    assert (heading.weight != 700);
    assert (heading.scale > 1.0);
    assert (quote.left_margin == 12);
    assert_rgba (
        code.paragraph_background_rgba,
        "#EFF0ED"
    );

    renderer.set_dark (true);

    assert_rgba (
        assistant.foreground_rgba,
        "#FAFBF9"
    );
    assert_rgba (
        you.foreground_rgba,
        "#C9CAC7"
    );
    assert_rgba (
        quote.foreground_rgba,
        "#C9CAC7"
    );
    assert_rgba (
        code.paragraph_background_rgba,
        "#2D2D2A"
    );
}

int
main (string[] args) {
    Gtk.init ();
    Test.init (ref args);

    Test.add_func (
        "/presentation-renderer/projection",
        test_rendering_projection
    );
    Test.add_func (
        "/presentation-renderer/palette",
        test_palette_and_theme_update
    );

    return Test.run ();
}
