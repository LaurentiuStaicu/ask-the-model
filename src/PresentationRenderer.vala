namespace AskTheModel {
    public class PresentationRenderer : GLib.Object {
        private const string TAG_SPEAKER_YOU =
            "atm-speaker-you";
        private const string TAG_SPEAKER_ASSISTANT =
            "atm-speaker-assistant";
        private const string TAG_HEADING_1 =
            "atm-heading-1";
        private const string TAG_HEADING_2 =
            "atm-heading-2";
        private const string TAG_HEADING_3 =
            "atm-heading-3";
        private const string TAG_HEADING_4 =
            "atm-heading-4";
        private const string TAG_QUOTE =
            "atm-quote";
        private const string TAG_CODE_BLOCK =
            "atm-code-block";
        private const string TAG_INLINE_CODE =
            "atm-inline-code";

        private Gtk.TextView view;
        private Gtk.TextBuffer buffer;
        private Gtk.TextMark? pending_assistant_origin = null;
        private Gtk.TextMark? pending_assistant_start = null;
        private Gtk.Spinner? pending_spinner = null;
        private Gtk.TextTag speaker_you;
        private Gtk.TextTag speaker_assistant;
        private Gtk.TextTag heading_1;
        private Gtk.TextTag heading_2;
        private Gtk.TextTag heading_3;
        private Gtk.TextTag heading_4;
        private Gtk.TextTag quote;
        private Gtk.TextTag code_block;
        private Gtk.TextTag inline_code;

        public PresentationRenderer (
            Gtk.TextView view
        ) {
            this.view = view;
            this.buffer = view.buffer;

            speaker_you = ensure_tag (
                TAG_SPEAKER_YOU
            );
            speaker_assistant = ensure_tag (
                TAG_SPEAKER_ASSISTANT
            );
            heading_1 = ensure_tag (
                TAG_HEADING_1
            );
            heading_2 = ensure_tag (
                TAG_HEADING_2
            );
            heading_3 = ensure_tag (
                TAG_HEADING_3
            );
            heading_4 = ensure_tag (
                TAG_HEADING_4
            );
            quote = ensure_tag (
                TAG_QUOTE
            );
            code_block = ensure_tag (
                TAG_CODE_BLOCK
            );
            inline_code = ensure_tag (
                TAG_INLINE_CODE
            );

            configure_structural_tags ();
            set_dark (false);

            view.copy_clipboard.connect (() => {
                bool had_anchor = false;
                string? semantic =
                    selected_semantic_text (
                        out had_anchor
                    );

                if (!had_anchor ||
                    semantic == null) {
                    return;
                }

                view.get_clipboard ().set_text (
                    semantic
                );
                GLib.Signal.stop_emission_by_name (
                    view,
                    "copy-clipboard"
                );
            });
        }

        private Gtk.TextTag ensure_tag (
            string name
        ) {
            unowned Gtk.TextTag? existing =
                buffer.tag_table.lookup (name);

            if (existing != null) {
                return existing;
            }

            var tag = new Gtk.TextTag (name);
            buffer.tag_table.add (tag);
            return tag;
        }

        private void configure_structural_tags () {
            speaker_you.weight = 700;
            speaker_assistant.weight = 700;

            /*
             * Headings deliberately use scale/spacing rather than bold:
             * the transcript keeps emphasis syntax flattened and reserves
             * bold weight for speaker identity.
             */
            heading_1.scale = 1.16;
            heading_2.scale = 1.12;
            heading_3.scale = 1.08;
            heading_4.scale = 1.04;

            heading_1.pixels_above_lines = 4;
            heading_1.pixels_below_lines = 2;
            heading_2.pixels_above_lines = 4;
            heading_2.pixels_below_lines = 2;
            heading_3.pixels_above_lines = 4;
            heading_3.pixels_below_lines = 2;
            heading_4.pixels_above_lines = 4;
            heading_4.pixels_below_lines = 2;

            quote.left_margin = 12;
            quote.right_margin = 4;

            code_block.left_margin = 8;
            code_block.right_margin = 8;
            code_block.pixels_above_lines = 3;
            code_block.pixels_below_lines = 3;
        }

        public void set_dark (bool dark) {
            if (dark) {
                speaker_assistant.foreground =
                    "#FAFBF9";
                speaker_you.foreground =
                    "#C9CAC7";
                quote.foreground =
                    "#C9CAC7";
                code_block.paragraph_background =
                    "#2D2D2A";
                inline_code.background =
                    "#2D2D2A";
            } else {
                speaker_assistant.foreground =
                    "#1B1B18";
                speaker_you.foreground =
                    "#5F605C";
                quote.foreground =
                    "#5F605C";
                code_block.paragraph_background =
                    "#EFF0ED";
                inline_code.background =
                    "#EFF0ED";
            }
        }

        private string semantic_anchor_text (
            Gtk.TextChildAnchor anchor
        ) {
            foreach (
                unowned Gtk.Widget widget
                in anchor.get_widgets ()
            ) {
                if (
                    widget.has_css_class (
                        "atm-source-ref"
                    ) &&
                    widget is Gtk.Button
                ) {
                    unowned Gtk.Button button =
                        (Gtk.Button) widget;
                    string? label = button.label;

                    return label ?? "";
                }

                if (
                    widget.has_css_class (
                        "atm-assistant-spinner"
                    ) ||
                    widget.has_css_class (
                        "atm-turn-separator"
                    )
                ) {
                    return "";
                }
            }

            /*
             * Unknown presentation anchors are deliberately omitted rather
             * than leaking the object-replacement character to clipboard.
             */
            return "";
        }

        internal string? selected_semantic_text (
            out bool had_anchor
        ) {
            had_anchor = false;

            Gtk.TextIter start;
            Gtk.TextIter end;

            if (!buffer.get_selection_bounds (
                    out start,
                    out end
                )) {
                return null;
            }

            var text =
                new GLib.StringBuilder ();
            Gtk.TextIter iter = start;

            while (iter.compare (end) < 0) {
                unowned Gtk.TextChildAnchor? anchor =
                    iter.get_child_anchor ();

                if (anchor != null) {
                    had_anchor = true;
                    text.append (
                        semantic_anchor_text (
                            anchor
                        )
                    );
                } else {
                    unichar character =
                        iter.get_char ();

                    if (character != 0) {
                        text.append_unichar (
                            character
                        );
                    }
                }

                if (!iter.forward_char ()) {
                    break;
                }
            }

            return text.str;
        }

        public void append_sources (
            Gtk.Button[] source_buttons
        ) {
            if (source_buttons.length == 0) {
                return;
            }

            insert_raw ("\nSources: ");

            for (
                uint i = 0;
                i < source_buttons.length;
                i++
            ) {
                append_semantic_child (
                    source_buttons[i],
                    "atm-source-ref"
                );

                if (i + 1 < source_buttons.length) {
                    insert_raw (" ");
                }
            }
        }

        public unowned Gtk.TextChildAnchor
        append_semantic_child (
            Gtk.Widget child,
            string css_class
        ) {
            child.add_css_class (
                css_class
            );

            Gtk.TextIter end;
            buffer.get_end_iter (out end);
            unowned Gtk.TextChildAnchor anchor =
                buffer.create_child_anchor (
                    end
                );

            view.add_child_at_anchor (
                child,
                anchor
            );

            return anchor;
        }

        private void append_message_separator () {
            if (buffer.get_char_count () == 0) {
                return;
            }

            insert_raw ("\n\n");
        }

        private void insert_raw (
            string text
        ) {
            if (text.length == 0) {
                return;
            }

            Gtk.TextIter end;
            buffer.get_end_iter (out end);
            buffer.insert (
                ref end,
                text,
                -1
            );
        }

        private void insert_tagged (
            string text,
            string tag_name
        ) {
            if (text.length == 0) {
                return;
            }

            int start_offset =
                buffer.get_char_count ();

            insert_raw (text);

            Gtk.TextIter start;
            Gtk.TextIter end;
            buffer.get_iter_at_offset (
                out start,
                start_offset
            );
            buffer.get_end_iter (out end);
            buffer.apply_tag_by_name (
                tag_name,
                start,
                end
            );
        }

        private void apply_tag_to_range (
            string tag_name,
            int start_offset
        ) {
            int end_offset =
                buffer.get_char_count ();

            if (end_offset <= start_offset) {
                return;
            }

            Gtk.TextIter start;
            Gtk.TextIter end;
            buffer.get_iter_at_offset (
                out start,
                start_offset
            );
            buffer.get_iter_at_offset (
                out end,
                end_offset
            );
            buffer.apply_tag_by_name (
                tag_name,
                start,
                end
            );
        }

        private string? block_tag_name (
            PresentationNative.Document document,
            uint block_index
        ) {
            PresentationNative.BlockType type =
                PresentationNative.block_type_at (
                    document,
                    block_index
                );

            switch (type) {
            case PresentationNative.BlockType.HEADING:
                uint level =
                    PresentationNative.heading_level_at (
                        document,
                        block_index
                    );

                if (level <= 1) {
                    return TAG_HEADING_1;
                }
                if (level == 2) {
                    return TAG_HEADING_2;
                }
                if (level == 3) {
                    return TAG_HEADING_3;
                }
                return TAG_HEADING_4;

            case PresentationNative.BlockType.QUOTE:
                return TAG_QUOTE;

            case PresentationNative.BlockType.CODE:
                return TAG_CODE_BLOCK;

            default:
                return null;
            }
        }

        private void render_document (
            PresentationNative.Document document
        ) {
            PresentationNative.BlockType previous_type =
                PresentationNative.BlockType.SPACER;
            bool have_previous = false;
            bool document_has_text = false;
            bool document_ends_newline = false;

            uint block_count =
                PresentationNative.block_count (
                    document
                );

            for (
                uint block_index = 0;
                block_index < block_count;
                block_index++
            ) {
                PresentationNative.BlockType type =
                    PresentationNative.block_type_at (
                        document,
                        block_index
                    );

                if (type ==
                    PresentationNative.BlockType.SPACER) {
                    if (document_has_text &&
                        !document_ends_newline) {
                        insert_raw ("\n");
                    }
                    if (document_has_text) {
                        insert_raw ("\n");
                    }

                    document_ends_newline =
                        document_has_text;
                    previous_type = type;
                    have_previous = true;
                    continue;
                }

                if (have_previous &&
                    document_has_text) {
                    bool adjacent_list_items =
                        previous_type ==
                            PresentationNative.BlockType.LIST_ITEM &&
                        type ==
                            PresentationNative.BlockType.LIST_ITEM;

                    if (!document_ends_newline) {
                        insert_raw ("\n");
                    }

                    if (!adjacent_list_items &&
                        previous_type !=
                            PresentationNative.BlockType.SPACER) {
                        insert_raw ("\n");
                    }

                    document_ends_newline = true;
                }

                int block_start =
                    buffer.get_char_count ();

                if (type ==
                    PresentationNative.BlockType.LIST_ITEM) {
                    uint level =
                        PresentationNative.nesting_level_at (
                            document,
                            block_index
                        );
                    uint indent_level =
                        level > 0 ? level - 1 : 0;

                    for (
                        uint depth = 0;
                        depth < indent_level;
                        depth++
                    ) {
                        insert_raw ("  ");
                    }

                    if (
                        PresentationNative.ordered_at (
                            document,
                            block_index
                        )
                    ) {
                        insert_raw (
                            "%u. ".printf (
                                PresentationNative.ordinal_at (
                                    document,
                                    block_index
                                )
                            )
                        );
                    } else {
                        insert_raw ("• ");
                    }

                    document_has_text = true;
                    document_ends_newline = false;
                }

                uint segment_count =
                    PresentationNative.segment_count_at (
                        document,
                        block_index
                    );

                for (
                    uint segment_index = 0;
                    segment_index < segment_count;
                    segment_index++
                ) {
                    unowned string? text =
                        PresentationNative.segment_text_at (
                            document,
                            block_index,
                            segment_index
                        );

                    if (text == null ||
                        text.length == 0) {
                        continue;
                    }

                    if (
                        PresentationNative.segment_type_at (
                            document,
                            block_index,
                            segment_index
                        ) ==
                        PresentationNative.SegmentType.INLINE_CODE
                    ) {
                        insert_tagged (
                            text,
                            TAG_INLINE_CODE
                        );
                    } else {
                        insert_raw (text);
                    }

                    document_has_text = true;
                    document_ends_newline =
                        text.has_suffix ("\n");
                }

                string? tag_name =
                    block_tag_name (
                        document,
                        block_index
                    );

                if (tag_name != null) {
                    apply_tag_to_range (
                        tag_name,
                        block_start
                    );
                }

                previous_type = type;
                have_previous = true;
            }
        }

        public void append_user (
            string text
        ) {
            append_message_separator ();
            insert_tagged (
                "You:",
                TAG_SPEAKER_YOU
            );

            if (text.length > 0) {
                insert_raw (" ");
                insert_raw (text);
            }
        }

        private void render_assistant (
            string text
        ) {
            insert_tagged (
                "Assistant:",
                TAG_SPEAKER_ASSISTANT
            );
            insert_raw (" ");

            try {
                PresentationNative.Document document;

                if (
                    PresentationNative.normalize (
                        text,
                        (size_t) text.length,
                        out document
                    )
                ) {
                    render_document (document);
                    return;
                }
            } catch (GLib.Error error) {
                /*
                 * Presentation is fail-safe: a renderer/normalizer error
                 * must never truncate or suppress the assistant answer.
                 */
            }

            insert_raw (text);
        }

        private void clear_pending_assistant () {
            Gtk.TextMark? origin_mark =
                pending_assistant_origin;

            if (origin_mark == null) {
                return;
            }

            Gtk.Spinner? spinner =
                pending_spinner;

            if (spinner != null &&
                spinner.get_parent () == view) {
                view.remove (spinner);
            }
            pending_spinner = null;

            Gtk.TextIter start;
            Gtk.TextIter end;
            buffer.get_iter_at_mark (
                out start,
                origin_mark
            );
            buffer.get_end_iter (out end);

            if (start.compare (end) < 0) {
                buffer.delete (
                    ref start,
                    ref end
                );
            }

            buffer.delete_mark (
                origin_mark
            );
            pending_assistant_origin = null;

            Gtk.TextMark? start_mark =
                pending_assistant_start;

            if (start_mark != null) {
                buffer.delete_mark (
                    start_mark
                );
                pending_assistant_start = null;
            }
        }

        public void begin_assistant_generation () {
            clear_pending_assistant ();

            Gtk.TextIter origin;
            buffer.get_end_iter (out origin);
            pending_assistant_origin =
                buffer.create_mark (
                    null,
                    origin,
                    true
                );

            append_message_separator ();

            Gtk.TextIter assistant_start;
            buffer.get_end_iter (
                out assistant_start
            );
            pending_assistant_start =
                buffer.create_mark (
                    null,
                    assistant_start,
                    true
                );

            insert_tagged (
                "Assistant:",
                TAG_SPEAKER_ASSISTANT
            );
            insert_raw (" ");

            var spinner = new Gtk.Spinner () {
                spinning = true,
                halign = Gtk.Align.START,
                valign = Gtk.Align.CENTER
            };
            spinner.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Assistant is generating"
            );

            append_semantic_child (
                spinner,
                "atm-assistant-spinner"
            );
            pending_spinner = spinner;
        }

        public Gtk.TextMark? assistant_generation_start_mark () {
            return pending_assistant_start;
        }

        public void append_turn_separator () {
            var separator = new Gtk.Label (
                "╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌"
            ) {
                can_target = false,
                halign = Gtk.Align.START,
                single_line_mode = true,
                width_chars = 40
            };
            separator.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Conversation turn separator"
            );

            append_semantic_child (
                separator,
                "atm-turn-separator"
            );
        }

        public bool assistant_generation_pending () {
            return pending_assistant_origin != null;
        }

        public void cancel_assistant_generation () {
            clear_pending_assistant ();
        }

        public void complete_assistant_generation (
            string text
        ) {
            clear_pending_assistant ();
            append_assistant (text);
        }

        /* Shared display-time projection for live completion and History.
         * Only live turns have a pending row to remove. Source buttons carry
         * already-resolved citations; this layer never changes their authority.
         */
        public void append_completed_turn (
            string text,
            Gtk.Button[] source_buttons
        ) {
            complete_assistant_generation (text);

            if (text.length == 0 && source_buttons.length == 0) {
                return;
            }

            append_sources (source_buttons);
            append_turn_separator ();
        }

        public void append_assistant (
            string text
        ) {
            if (text.length == 0) {
                return;
            }

            append_message_separator ();
            render_assistant (text);
        }
    }
}
