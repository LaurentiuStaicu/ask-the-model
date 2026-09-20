namespace AskTheModel {
    public class AnnunciatorLabel : Gtk.Label {
        public AnnunciatorLabel (
            string text,
            string? extra_class = null
        ) {
            Object (
                label: text,
                valign: Gtk.Align.CENTER,
                single_line_mode: true
            );

            add_css_class ("atm-annunciator");

            if (extra_class != null) {
                add_css_class (extra_class);
            }

            set_active (false);
        }

        public void set_active (bool active) {
            if (active) {
                add_css_class ("active");
            } else {
                remove_css_class ("active");
            }
        }
    }
}
