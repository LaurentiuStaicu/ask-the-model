namespace AskTheModel {
    public class SelectorTriangle : Gtk.DrawingArea {
        public SelectorTriangle () {
            can_target = false;
            focusable = false;
            width_request = 8;
            height_request = 6;
            halign = Gtk.Align.END;
            valign = Gtk.Align.CENTER;
            margin_end = 8;
            add_css_class ("atm-selector-triangle");

            set_draw_func (draw_triangle);
        }

        private void draw_triangle (
            Gtk.DrawingArea area,
            Cairo.Context context,
            int width,
            int height
        ) {
            Gdk.RGBA color = area.get_color ();

            double center_x = width / 2.0;
            double top = 1.0;
            double bottom = height - 1.0;
            double half_width = 3.0;

            context.set_source_rgba (
                color.red,
                color.green,
                color.blue,
                color.alpha
            );
            context.move_to (
                center_x - half_width,
                top
            );
            context.line_to (
                center_x + half_width,
                top
            );
            context.line_to (
                center_x,
                bottom
            );
            context.close_path ();
            context.fill ();
        }
    }
}
