namespace AskTheModel {
    public class Application : Gtk.Application {
        private const string APP_ID = "io.github.laurentiustaicu.ask_the_model";
        private const string IDENTITY_RESOURCE =
            "/io/github/laurentiustaicu/ask_the_model/identity-icon.png";

        public Application () {
            Object (
                application_id: APP_ID,
                flags: ApplicationFlags.DEFAULT_FLAGS
            );
        }

        protected override void activate () {
            var main_window = this.active_window as Gtk.ApplicationWindow;

            if (main_window == null) {
                main_window = new Gtk.ApplicationWindow (this) {
                    title = "Ask the Model",
                    default_width = 780,
                    default_height = 700,
                    titlebar = build_titlebar (),
                    child = build_main_content ()
                };
            }

            main_window.present ();
        }

        private Gtk.Widget build_titlebar () {
            var title_label = new Gtk.Label ("Ask the Model");
            title_label.add_css_class ("title");

            var headerbar = new Gtk.HeaderBar () {
                show_title_buttons = true,
                title_widget = title_label
            };

            // The overlay keeps the real HeaderBar at its normal theme-driven
            // height while allowing the AtM identity mark to extend below it.
            var titlebar_overlay = new Gtk.Overlay () {
                child = headerbar
            };

            var identity_mark = new Gtk.Picture.for_resource (
                IDENTITY_RESOURCE
            ) {
                alternative_text = "Ask the Model",
                can_shrink = true,
                content_fit = Gtk.ContentFit.CONTAIN,
                width_request = 56,
                height_request = 56,
                halign = Gtk.Align.START,
                valign = Gtk.Align.START,
                margin_start = 52,
                margin_top = 4,
                can_target = false
            };

            titlebar_overlay.add_overlay (identity_mark);
            titlebar_overlay.set_measure_overlay (identity_mark, false);
            titlebar_overlay.set_clip_overlay (identity_mark, false);

            return titlebar_overlay;
        }

        private Gtk.Widget build_main_content () {
            // General interface chrome inherits the elementary theme font.
            // TextView.monospace requests the system monospace style instead
            // of hard-coding Roboto Mono or a point size.
            var transcript = new Gtk.TextView () {
                editable = false,
                cursor_visible = false,
                monospace = true,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                left_margin = 12,
                right_margin = 12,
                top_margin = 12,
                bottom_margin = 12,
                vexpand = true
            };

            var transcript_scroll = new Gtk.ScrolledWindow () {
                child = transcript,
                hscrollbar_policy = Gtk.PolicyType.NEVER,
                vscrollbar_policy = Gtk.PolicyType.AUTOMATIC,
                vexpand = true
            };

            var prompt_view = new Gtk.TextView () {
                monospace = true,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                accepts_tab = false,
                left_margin = 8,
                right_margin = 8,
                top_margin = 8,
                bottom_margin = 8,
                height_request = 72,
                hexpand = true
            };

            var prompt_overlay = new Gtk.Overlay () {
                child = prompt_view
            };

            var prompt_placeholder = new Gtk.Label ("Ask something…") {
                halign = Gtk.Align.START,
                valign = Gtk.Align.START,
                margin_start = 12,
                margin_top = 10,
                can_target = false
            };
            prompt_placeholder.add_css_class ("dim-label");
            prompt_placeholder.add_css_class ("monospace");
            prompt_overlay.add_overlay (prompt_placeholder);

            prompt_view.buffer.changed.connect (() => {
                prompt_placeholder.visible =
                    prompt_view.buffer.get_char_count () == 0;
            });

            var prompt_frame = new Gtk.Frame (null) {
                child = prompt_overlay,
                hexpand = true
            };

            var send_button = new Gtk.Button.with_label ("Send") {
                valign = Gtk.Align.END
            };

            var composer = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 8) {
                margin_top = 12,
                margin_bottom = 12,
                margin_start = 12,
                margin_end = 12
            };
            composer.append (prompt_frame);
            composer.append (send_button);

            var content = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
            content.append (transcript_scroll);
            content.append (new Gtk.Separator (Gtk.Orientation.HORIZONTAL));
            content.append (composer);

            return content;
        }

        public static int main (string[] args) {
            return new Application ().run (args);
        }
    }
}
