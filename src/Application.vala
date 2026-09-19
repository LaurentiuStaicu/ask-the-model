namespace AskTheModel {
    public class Application : Gtk.Application {
        public Application () {
            Object (
                application_id: "io.github.laurentiustaicu.ask_the_model",
                flags: ApplicationFlags.DEFAULT_FLAGS
            );
        }

        protected override void activate () {
            var main_window = this.active_window as Gtk.ApplicationWindow;

            if (main_window == null) {
                var title_label = new Gtk.Label ("Ask the Model");

                var subtitle_label = new Gtk.Label (
                    "Local AI chat • ask naturally • your data stays local"
                );

                var content = new Gtk.Box (Gtk.Orientation.VERTICAL, 12) {
                    halign = Gtk.Align.CENTER,
                    valign = Gtk.Align.CENTER,
                    margin_top = 24,
                    margin_bottom = 24,
                    margin_start = 24,
                    margin_end = 24
                };

                content.append (title_label);
                content.append (subtitle_label);

                main_window = new Gtk.ApplicationWindow (this) {
                    title = "Ask the Model",
                    default_width = 840,
                    default_height = 560,
                    child = content
                };
            }

            main_window.present ();
        }

        public static int main (string[] args) {
            return new Application ().run (args);
        }
    }
}
