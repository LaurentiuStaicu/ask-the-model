namespace AskTheModel {
    public class Application : Gtk.Application {
        private const string APP_ID = "io.github.laurentiustaicu.ask_the_model";
        private const string IDENTITY_RESOURCE =
            "/io/github/laurentiustaicu/ask_the_model/identity-icon.png";
        private const string STYLE_RESOURCE =
            "/io/github/laurentiustaicu/ask_the_model/style.css";

        public Application () {
            Object (
                application_id: APP_ID,
                flags: ApplicationFlags.DEFAULT_FLAGS
            );
        }

        protected override void startup () {
            base.startup ();

            var css_provider = new Gtk.CssProvider ();
            css_provider.load_from_resource (STYLE_RESOURCE);

            var display = Gdk.Display.get_default ();
            if (display != null) {
                Gtk.StyleContext.add_provider_for_display (
                    display,
                    css_provider,
                    Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
                );
            }
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
                main_window.add_css_class ("atm-window");
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
            headerbar.add_css_class ("atm-headerbar");

            // The identity mark owns the top-left corner. Native window
            // controls stay on the right.
            headerbar.set_decoration_layout (":minimize,maximize,close");

            var titlebar_overlay = new Gtk.Overlay () {
                child = headerbar,
                overflow = Gtk.Overflow.VISIBLE
            };

            // Use the prepared AtM artwork itself for the medallion relief.
            // Do not rebuild the disc with CSS shadows: that produced the
            // flattened/oval "blob" seen in the previous visual test.
            var identity_picture = new Gtk.Picture.for_resource (
                IDENTITY_RESOURCE
            ) {
                alternative_text = "Ask the Model",
                can_shrink = true,
                content_fit = Gtk.ContentFit.CONTAIN,
                width_request = 68,
                height_request = 68,
                halign = Gtk.Align.START,
                valign = Gtk.Align.START,
                margin_start = 0,
                margin_top = 0,
                can_target = false,
                overflow = Gtk.Overflow.VISIBLE
            };
            identity_picture.add_css_class ("atm-identity-mark");

            titlebar_overlay.add_overlay (identity_picture);
            titlebar_overlay.set_measure_overlay (identity_picture, false);
            titlebar_overlay.set_clip_overlay (identity_picture, false);

            return titlebar_overlay;
        }

        private Gtk.Widget build_main_content () {
            var transcript = new Gtk.TextView () {
                editable = false,
                cursor_visible = false,
                monospace = true,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                left_margin = 12,
                right_margin = 12,
                top_margin = 20,
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
                left_margin = 10,
                right_margin = 10,
                top_margin = 10,
                bottom_margin = 10,
                height_request = 72,
                hexpand = true
            };

            var prompt_overlay = new Gtk.Overlay () {
                child = prompt_view
            };

            var prompt_placeholder = new Gtk.Label ("Ask something…") {
                halign = Gtk.Align.START,
                valign = Gtk.Align.START,
                margin_start = 14,
                margin_top = 12,
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
            prompt_frame.add_css_class ("atm-input-frame");

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
            content.append (composer);

            return content;
        }

        public static int main (string[] args) {
            return new Application ().run (args);
        }
    }
}
