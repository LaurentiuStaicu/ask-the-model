namespace AskTheModel {
    public enum TranscriptSpeaker {
        USER,
        ASSISTANT,
        SYSTEM
    }

    public class PresentationTranscriptRenderer : Object {
        public const string TAG_SPEAKER_USER =
            "atm-speaker-user";
        public const string TAG_SPEAKER_ASSISTANT =
            "atm-speaker-assistant";
        public const string TAG_SPEAKER_SYSTEM =
            "atm-speaker-system";
        public const string TAG_HEADING =
            "atm-presentation-heading";
        public const string TAG_INLINE_CODE =
            "atm-presentation-inline-code";
        public const string TAG_CODE =
            "atm-presentation-code";
        public const string TAG_QUOTE =
            "atm-presentation-quote";

        private static void ensure_tag (
            Gtk.TextBuffer buffer,
            string name
        ) {
            if (buffer.tag_table.lookup (name) != null) {
                return;
            }

            var tag = new Gtk.TextTag (name);
            buffer.tag_table.add (tag);
        }

        private static void ensure_tags (
            Gtk.TextBuffer buffer
        ) {
            ensure_tag (buffer, TAG_SPEAKER_USER);
            ensure_tag (
                buffer,
                TAG_SPEAKER_ASSISTANT
            );
            ensure_tag (buffer, TAG_SPEAKER_SYSTEM);
            ensure_tag (buffer, TAG_HEADING);
            ensure_tag (buffer, TAG_INLINE_CODE);
            ensure_tag (buffer, TAG_CODE);
            ensure_tag (buffer, TAG_QUOTE);
        }

        public static void configure_palette (
            Gtk.TextBuffer buffer,
            bool dark
        ) {
            ensure_tags (buffer);

            var user =
                buffer.tag_table.lookup (
                    TAG_SPEAKER_USER
                );
            var assistant =
                buffer.tag_table.lookup (
                    TAG_SPEAKER_ASSISTANT
                );
            var system =
                buffer.tag_table.lookup (
                    TAG_SPEAKER_SYSTEM
                );
            var heading =
                buffer.tag_table.lookup (
                    TAG_HEADING
                );
            var inline_code =
                buffer.tag_table.lookup (
                    TAG_INLINE_CODE
                );
            var code =
                buffer.tag_table.lookup (
                    TAG_CODE
                );
            var quote =
                buffer.tag_table.lookup (
                    TAG_QUOTE
                );

            if (user != null) {
                user.weight =
                    (int) Pango.Weight.BOLD;
                user.foreground = dark
                    ? "#FAFBF9"
                    : "#1B1B18";
            }

            if (assistant != null) {
                assistant.weight =
                    (int) Pango.Weight.BOLD;
                assistant.foreground = dark
                    ? "#C9CAC7"
                    : "#5F605C";
            }

            if (system != null) {
                system.weight =
                    (int) Pango.Weight.BOLD;
                system.foreground = dark
                    ? "#9FA19D"
                    : "#5F605C";
            }

            if (heading != null) {
                heading.weight =
                    (int) Pango.Weight.NORMAL;
                heading.style =
                    Pango.Style.NORMAL;
                heading.scale = 1.06;
            }

            if (inline_code != null) {
                inline_code.weight =
                    (int) Pango.Weight.NORMAL;
                inline_code.style =
                    Pango.Style.NORMAL;
                inline_code.family = "monospace";
            }

            if (code != null) {
                code.weight =
                    (int) Pango.Weight.NORMAL;
                code.style =
                    Pango.Style.NORMAL;
                code.family = "monospace";
            }

            if (quote != null) {
                quote.weight =
                    (int) Pango.Weight.NORMAL;
                quote.style =
                    Pango.Style.NORMAL;
                quote.foreground = dark
                    ? "#C9CAC7"
                    : "#5F605C";
            }
        }

        private static string speaker_label (
            TranscriptSpeaker speaker
        ) {
            switch (speaker) {
            case TranscriptSpeaker.USER:
                return "You:";
            case TranscriptSpeaker.ASSISTANT:
                return "Assistant:";
            case TranscriptSpeaker.SYSTEM:
                return "System:";
            }

            assert_not_reached ();
        }

        private static string speaker_tag_name (
            TranscriptSpeaker speaker
        ) {
            switch (speaker) {
            case TranscriptSpeaker.USER:
                return TAG_SPEAKER_USER;
            case TranscriptSpeaker.ASSISTANT:
                return TAG_SPEAKER_ASSISTANT;
            case TranscriptSpeaker.SYSTEM:
                return TAG_SPEAKER_SYSTEM;
            }

            assert_not_reached ();
        }

        private static void insert_raw (
            Gtk.TextBuffer buffer,
            string text
        ) {
            Gtk.TextIter end;
            buffer.get_end_iter (out end);
            buffer.insert (ref end, text, -1);
        }

        private static void insert_tagged (
            Gtk.TextBuffer buffer,
            string text,
            string tag_name
        ) {
            int start_offset =
                buffer.get_char_count ();

            insert_raw (buffer, text);

            int end_offset =
                buffer.get_char_count ();
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

        private static void append_message_header (
            Gtk.TextBuffer buffer,
            TranscriptSpeaker speaker,
            bool dark
        ) {
            configure_palette (buffer, dark);

            if (buffer.get_char_count () > 0) {
                insert_raw (buffer, "\n\n");
            }

            insert_tagged (
                buffer,
                speaker_label (speaker),
                speaker_tag_name (speaker)
            );
            insert_raw (buffer, " ");
        }

        public static void append_plain_message (
            Gtk.TextBuffer buffer,
            TranscriptSpeaker speaker,
            string content,
            bool dark
        ) {
            append_message_header (
                buffer,
                speaker,
                dark
            );
            insert_raw (buffer, content);
        }

        private static void apply_block_tag (
            Gtk.TextBuffer buffer,
            string tag_name,
            int start_offset,
            int end_offset
        ) {
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

        private static void render_document (
            Gtk.TextBuffer buffer,
            PresentationNative.Document document
        ) {
            bool have_previous = false;
            bool have_output = false;
            bool output_ends_newline = false;
            PresentationNative.BlockType previous =
                PresentationNative.BlockType.SPACER;

            uint count =
                PresentationNative.block_count (
                    document
                );

            for (uint i = 0; i < count; i++) {
                PresentationNative.BlockType type =
                    PresentationNative.block_type_at (
                        document,
                        i
                    );

                if (
                    type ==
                    PresentationNative.BlockType.SPACER
                ) {
                    if (have_output) {
                        if (!output_ends_newline) {
                            insert_raw (
                                buffer,
                                "\n"
                            );
                        }
                        insert_raw (buffer, "\n");
                        output_ends_newline = true;
                    }

                    previous = type;
                    have_previous = true;
                    continue;
                }

                if (have_previous && have_output) {
                    bool adjacent_list_items =
                        previous ==
                            PresentationNative.BlockType.LIST_ITEM &&
                        type ==
                            PresentationNative.BlockType.LIST_ITEM;

                    if (!output_ends_newline) {
                        insert_raw (buffer, "\n");
                        output_ends_newline = true;
                    }

                    if (
                        !adjacent_list_items &&
                        previous !=
                            PresentationNative.BlockType.SPACER
                    ) {
                        insert_raw (buffer, "\n");
                    }
                }

                if (
                    type ==
                    PresentationNative.BlockType.LIST_ITEM
                ) {
                    uint nesting =
                        PresentationNative.nesting_level_at (
                            document,
                            i
                        );

                    for (
                        uint depth = 1;
                        depth < nesting;
                        depth++
                    ) {
                        insert_raw (buffer, "  ");
                    }

                    if (
                        PresentationNative.ordered_at (
                            document,
                            i
                        )
                    ) {
                        insert_raw (
                            buffer,
                            "%u. ".printf (
                                PresentationNative.ordinal_at (
                                    document,
                                    i
                                )
                            )
                        );
                    } else {
                        insert_raw (buffer, "• ");
                    }

                    have_output = true;
                    output_ends_newline = false;
                }

                int block_start =
                    buffer.get_char_count ();

                uint segment_count =
                    PresentationNative.segment_count_at (
                        document,
                        i
                    );

                for (
                    uint segment_index = 0;
                    segment_index < segment_count;
                    segment_index++
                ) {
                    string? text =
                        PresentationNative.segment_text_at (
                            document,
                            i,
                            segment_index
                        );

                    if (text == null || text.length == 0) {
                        continue;
                    }

                    if (
                        PresentationNative.segment_type_at (
                            document,
                            i,
                            segment_index
                        ) ==
                        PresentationNative.SegmentType.INLINE_CODE
                    ) {
                        insert_tagged (
                            buffer,
                            text,
                            TAG_INLINE_CODE
                        );
                    } else {
                        insert_raw (buffer, text);
                    }

                    have_output = true;
                    output_ends_newline =
                        text.has_suffix ("\n");
                }

                int block_end =
                    buffer.get_char_count ();

                switch (type) {
                case PresentationNative.BlockType.HEADING:
                    apply_block_tag (
                        buffer,
                        TAG_HEADING,
                        block_start,
                        block_end
                    );
                    break;
                case PresentationNative.BlockType.CODE:
                    apply_block_tag (
                        buffer,
                        TAG_CODE,
                        block_start,
                        block_end
                    );
                    break;
                case PresentationNative.BlockType.QUOTE:
                    apply_block_tag (
                        buffer,
                        TAG_QUOTE,
                        block_start,
                        block_end
                    );
                    break;
                default:
                    break;
                }

                previous = type;
                have_previous = true;
            }
        }

        public static bool append_assistant_message (
            Gtk.TextBuffer buffer,
            string content,
            bool dark
        ) {
            append_message_header (
                buffer,
                TranscriptSpeaker.ASSISTANT,
                dark
            );

            try {
                PresentationNative.Document document;

                if (
                    !PresentationNative.normalize (
                        content,
                        (size_t) content.length,
                        out document
                    )
                ) {
                    insert_raw (buffer, content);
                    return true;
                }

                bool fallback =
                    PresentationNative.is_fallback (
                        document
                    );

                render_document (
                    buffer,
                    document
                );
                return fallback;
            } catch (GLib.Error error) {
                /*
                 * Presentation is display-only. A renderer failure may
                 * never suppress provider content.
                 */
                insert_raw (buffer, content);
                return true;
            }
        }
    }
}
