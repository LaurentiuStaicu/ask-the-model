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
    var view = new Gtk.TextView ();
    var buffer = view.buffer;
    var renderer =
        new PresentationRenderer (view);

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
test_chart_widget_newline_rendering () {
    var view = new Gtk.TextView ();
    var renderer = new PresentationRenderer (view);
    renderer.append_assistant ("Răspuns");

    var chart = new Gtk.Label ("Grafic");
    renderer.append_chart_widget (chart);

    assert (chart.has_css_class ("atm-chart-attachment"));
    assert (view.buffer.text == "Assistant: Răspuns\n\n");
    assert (!view.buffer.text.contains ("\\n"));

    var status_view = new Gtk.TextView ();
    var status_renderer = new PresentationRenderer (status_view);
    status_renderer.append_chart_status ("Grafic indisponibil");

    assert (status_view.buffer.text == "\n");
    assert (!status_view.buffer.text.contains ("\\n"));

    Gtk.TextIter iter;
    status_view.buffer.get_start_iter (out iter);
    unowned Gtk.TextChildAnchor? anchor = iter.get_child_anchor ();
    assert (anchor != null);
    bool found_status = false;
    foreach (unowned Gtk.Widget widget in anchor.get_widgets ()) {
        if (widget.has_css_class ("atm-chart-status")) {
            found_status = true;
        }
    }
    assert (found_status);
}

private void
test_palette_and_theme_update () {
    var view = new Gtk.TextView ();
    var buffer = view.buffer;
    var renderer =
        new PresentationRenderer (view);

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

private void
test_pending_assistant_lifecycle () {
    var view = new Gtk.TextView ();
    var buffer = view.buffer;
    var renderer =
        new PresentationRenderer (view);

    renderer.append_user ("Întrebare");
    string baseline = buffer.text;

    renderer.begin_assistant_generation ();
    assert (renderer.assistant_generation_pending ());
    assert (
        buffer.text ==
        baseline + "\n\nAssistant: "
    );

    Gtk.TextIter pending_start;
    Gtk.TextIter pending_end;
    buffer.get_bounds (
        out pending_start,
        out pending_end
    );
    buffer.select_range (
        pending_start,
        pending_end
    );

    bool pending_had_anchor = false;
    string? pending_semantic =
        renderer.selected_semantic_text (
            out pending_had_anchor
        );
    assert (pending_had_anchor);
    assert (pending_semantic != null);
    assert (!pending_semantic.contains ("\uFFFC"));

    renderer.complete_assistant_generation (
        "Răspuns final"
    );
    assert (!renderer.assistant_generation_pending ());
    assert (
        buffer.text ==
        baseline +
        "\n\nAssistant: Răspuns final"
    );

    string completed = buffer.text;

    renderer.append_turn_separator ();
    assert (buffer.text == completed + "\n");

    renderer.begin_assistant_generation ();
    assert (renderer.assistant_generation_pending ());
    renderer.cancel_assistant_generation ();
    assert (!renderer.assistant_generation_pending ());
    assert (buffer.text == completed + "\n");
}

private void
test_semantic_anchor_copy_projection () {
    var view = new Gtk.TextView ();
    var buffer = view.buffer;
    var renderer =
        new PresentationRenderer (view);

    renderer.append_assistant (
        "Răspuns"
    );

    string before_sources =
        buffer.text;
    Gtk.Button[] no_sources = {};
    renderer.append_sources (
        no_sources
    );
    assert (
        buffer.text ==
        before_sources
    );

    var source_1 = new Gtk.Button () {
        label = "[1]",
        focusable = true
    };
    source_1.update_property (
        Gtk.AccessibleProperty.LABEL,
        "Source 1"
    );

    var source_2 = new Gtk.Button () {
        label = "[2]",
        focusable = true
    };
    source_2.update_property (
        Gtk.AccessibleProperty.LABEL,
        "Source 2"
    );

    Gtk.Button[] sources = {
        source_1,
        source_2
    };
    renderer.append_sources (
        sources
    );
    renderer.append_turn_separator ();

    assert (source_1.focusable);
    assert (source_2.focusable);

    Gtk.TextIter start;
    Gtk.TextIter end;
    buffer.get_bounds (
        out start,
        out end
    );
    buffer.select_range (
        start,
        end
    );

    bool had_anchor = false;
    string? semantic =
        renderer.selected_semantic_text (
            out had_anchor
        );

    assert (had_anchor);
    assert (semantic != null);
    assert (
        semantic ==
        "Assistant: Răspuns\nSources: [1] [2]"
    );
    assert (!semantic.contains ("\uFFFC"));
    assert (!semantic.contains ("╌"));
}

/* Include actual child widgets and applied tags, not just TextBuffer.text:
 * TextBuffer.text intentionally omits source/separator anchors. */
private string
projection_signature (Gtk.TextBuffer buffer) {
    var signature = new GLib.StringBuilder ();
    Gtk.TextIter iter;
    buffer.get_start_iter (out iter);
    while (!iter.is_end ()) {
        unowned Gtk.TextChildAnchor? anchor = iter.get_child_anchor ();
        if (anchor != null) {
            foreach (unowned Gtk.Widget widget in anchor.get_widgets ()) {
                if (widget is Gtk.Button) {
                    signature.append (((Gtk.Button) widget).label ?? "");
                    assert (widget.focusable);
                    assert (widget.has_css_class ("atm-source-ref"));
                } else {
                    assert (widget.has_css_class ("atm-turn-separator"));
                    signature.append ("<separator>");
                }
            }
        } else {
            signature.append_unichar (iter.get_char ());
            foreach (unowned Gtk.TextTag tag in iter.get_tags ()) {
                signature.append ("<" + (tag.name ?? "") + ">");
            }
        }
        iter.forward_char ();
    }
    return signature.str;
}

private string
copy_projection (PresentationRenderer renderer, Gtk.TextBuffer buffer) {
    Gtk.TextIter start;
    Gtk.TextIter end;
    buffer.get_bounds (out start, out end);
    buffer.select_range (start, end);
    bool had_anchor;
    string? text = renderer.selected_semantic_text (out had_anchor);
    assert (had_anchor);
    assert (text != null);
    assert (!text.contains ("\uFFFC"));
    return text;
}

private void
test_completed_turn_parity () {
    string[] answers = {
        "Răspuns **important** cu șțâîă și 日本語.",
        "## Titlu\n\n- unu\n- doi\n\n> Citat\n\n```txt\nx = 1\n```",
        "**incomplet <span>text &amp; final",
        "```vala\nvar x = \"neterminat\";",
        "Please restate the question with the repository, variable, source, or topic you mean."
    };
    for (int theme = 0; theme < 2; theme++) {
        for (int source_count = 0; source_count <= 2; source_count += 2) {
            var live_view = new Gtk.TextView ();
            var history_view = new Gtk.TextView ();
            var live = new PresentationRenderer (live_view);
            var history = new PresentationRenderer (history_view);
            live.set_dark (theme == 1);
            history.set_dark (theme == 1);
            foreach (string answer in answers) {
                live.append_user ("Întrebare **literală**");
                history.append_user ("Întrebare **literală**");
                live.begin_assistant_generation ();
                assert (!history.assistant_generation_pending ());
                Gtk.Button[] live_sources = {};
                Gtk.Button[] history_sources = {};
                for (int i = 1; i <= source_count; i++) {
                    live_sources += new Gtk.Button.with_label ("[%d]".printf (i));
                    history_sources += new Gtk.Button.with_label ("[%d]".printf (i));
                }
                live.append_completed_turn (answer, live_sources);
                history.append_completed_turn (answer, history_sources);
                assert (!live.assistant_generation_pending ());
                assert (!history.assistant_generation_pending ());
                assert (projection_signature (live_view.buffer) ==
                        projection_signature (history_view.buffer));
                string copied = copy_projection (live, live_view.buffer);
                assert (copied == copy_projection (history, history_view.buffer));
                assert (copied.contains ("Întrebare **literală**"));
                if (source_count > 0) {
                    assert (copied.contains ("Sources: [1] [2]"));
                } else {
                    assert (!copied.contains ("Sources:"));
                }
            }
            string completed = projection_signature (live_view.buffer);
            assert (completed.split ("<separator>").length == answers.length + 1);
            live.begin_assistant_generation ();
            live.append_completed_turn ("", {});
            history.append_completed_turn ("", {});
            assert (!live.assistant_generation_pending ());
            assert (projection_signature (live_view.buffer) == completed);
            assert (projection_signature (history_view.buffer) == completed);
        }
    }
}

private void
settle_visual_frame () {
    var loop = new MainLoop ();
    Timeout.add (400, () => {
        loop.quit ();
        return Source.REMOVE;
    });
    loop.run ();
}

/* Opt-in CI smoke fixture. Uses production CSS and the actual renderer;
 * ImageMagick captures only the isolated Xvfb display. */
private int
visual_smoke (string output_dir) {
    var css = new Gtk.CssProvider ();
    css.load_from_path ("data/style.css");
    Gtk.StyleContext.add_provider_for_display (
        Gdk.Display.get_default (), css,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    DirUtils.create_with_parents (output_dir, 0755);
    for (int theme = 0; theme < 2; theme++) {
        var window = new Gtk.Window () {
            default_width = 1320,
            default_height = 820
        };
        window.add_css_class ("atm-window");
        if (theme == 1) {
            window.add_css_class ("atm-dark");
        }
        var columns = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 16) {
            homogeneous = true,
            margin_top = 12, margin_bottom = 12,
            margin_start = 12, margin_end = 12
        };
        window.child = columns;
        Gtk.ScrolledWindow[] scrolls = {};
        string? previous_signature = null;
        for (int mode = 0; mode < 2; mode++) {
            var column = new Gtk.Box (Gtk.Orientation.VERTICAL, 8);
            column.append (new Gtk.Label (mode == 0 ? "Live" : "History"));
            var view = new Gtk.TextView () {
                editable = false, cursor_visible = false,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                left_margin = 10, right_margin = 10
            };
            var scroll = new Gtk.ScrolledWindow () {
                vexpand = true, hexpand = true, child = view
            };
            scrolls += scroll;
            column.append (scroll);
            columns.append (column);
            var renderer = new PresentationRenderer (view);
            renderer.set_dark (theme == 1);
            for (int turn = 0; turn < 4; turn++) {
                renderer.append_user ("Întrebare **literală** — %d".printf (turn + 1));
                if (mode == 0) {
                    renderer.begin_assistant_generation ();
                }
                Gtk.Button[] sources = {
                    new Gtk.Button.with_label ("[1]"),
                    new Gtk.Button.with_label ("[2]")
                };
                renderer.append_completed_turn (
                    "## Rezultat și limite\n\n" +
                    "Text **important**, șțâîă, cu `cod` și conținut suficient de lung pentru a verifica împachetarea pe mai multe rânduri în aceeași fereastră.\n\n" +
                    "- Prima observație\n- A doua observație\n\n" +
                    "> Interpretarea păstrează limitele dovezilor.\n\n" +
                    "```txt\nx = 1\ny = 2\n```\n\n" +
                    "Text final <span>istoric incomplet **lizibil",
                    sources
                );
            }
            string signature = projection_signature (view.buffer);
            if (previous_signature != null) {
                assert (signature == previous_signature);
            }
            previous_signature = signature;
        }
        window.present ();
        settle_visual_frame ();
        for (int position = 0; position < 2; position++) {
            if (position == 1) {
                foreach (Gtk.ScrolledWindow scroll in scrolls) {
                    scroll.vadjustment.value = scroll.vadjustment.upper -
                        scroll.vadjustment.page_size;
                }
                settle_visual_frame ();
            }
            string file = Path.build_filename (output_dir,
                "%s-%s.png".printf (theme == 0 ? "light" : "dark",
                                    position == 0 ? "top" : "bottom"));
            try {
                int status;
                Process.spawn_command_line_sync (
                    "import -window root " + Shell.quote (file),
                    null, null, out status
                );
                assert (status == 0);
            } catch (Error error) {
                GLib.error ("Visual smoke capture failed: %s", error.message);
            }
        }
        window.destroy ();
        settle_visual_frame ();
    }
    return 0;
}

int
main (string[] args) {
    Gtk.init ();
    string? smoke_dir = Environment.get_variable ("ATM_PRESENTATION_SMOKE_DIR");
    if (smoke_dir != null) {
        return visual_smoke (smoke_dir);
    }
    Test.init (ref args);

    Test.add_func (
        "/presentation-renderer/projection",
        test_rendering_projection
    );
    Test.add_func (
        "/presentation-renderer/chart-widget-newlines",
        test_chart_widget_newline_rendering
    );
    Test.add_func (
        "/presentation-renderer/palette",
        test_palette_and_theme_update
    );
    Test.add_func (
        "/presentation-renderer/pending-assistant",
        test_pending_assistant_lifecycle
    );
    Test.add_func (
        "/presentation-renderer/semantic-anchor-copy",
        test_semantic_anchor_copy_projection
    );

    Test.add_func (
        "/presentation-renderer/completed-turn-parity",
        test_completed_turn_parity
    );

    return Test.run ();
}
