namespace AskTheModel {
    public class ActivityRing : Gtk.DrawingArea {
        private const double ROTATION_SECONDS = 1.8;
        private const double ARC_SPAN = 0.95;

        // Clear activity gold. The steady LCD uses a dimmer,
        // warmer lamp-amber so motion remains easy to notice.
        private const double ACTIVE_GOLD_R = 0.902;
        private const double ACTIVE_GOLD_G = 0.741;
        private const double ACTIVE_GOLD_B = 0.400;

        private bool working = false;
        private bool animated = true;
        private uint tick_id = 0;
        private int64 start_time = 0;
        private double phase = 0.0;

        public ActivityRing () {
            accessible_role =
                Gtk.AccessibleRole.PRESENTATION;
            can_target = false;
            focusable = false;
            hexpand = true;
            vexpand = true;
            halign = Gtk.Align.FILL;
            valign = Gtk.Align.FILL;
            visible = false;

            set_draw_func (draw_ring);
        }

        public void set_working (
            bool value,
            bool animations_enabled
        ) {
            working = value;
            animated = animations_enabled;
            visible = value;

            if (!working) {
                stop_animation ();
                phase = 0.0;
                queue_draw ();
                return;
            }

            start_time = 0;
            phase = 0.0;

            if (animated) {
                start_animation ();
            } else {
                stop_animation ();
            }

            queue_draw ();
        }

        private void start_animation () {
            if (tick_id != 0) {
                return;
            }

            tick_id = add_tick_callback ((widget, frame_clock) => {
                if (!working || !animated) {
                    tick_id = 0;
                    return false;
                }

                int64 frame_time =
                    frame_clock.get_frame_time ();

                if (start_time == 0) {
                    start_time = frame_time;
                }

                double elapsed_seconds =
                    (double) (frame_time - start_time) /
                    1000000.0;

                phase =
                    elapsed_seconds *
                    (2.0 * Math.PI / ROTATION_SECONDS);

                queue_draw ();
                return true;
            });
        }

        private void stop_animation () {
            if (tick_id == 0) {
                return;
            }

            remove_tick_callback (tick_id);
            tick_id = 0;
        }

        private void draw_ring (
            Gtk.DrawingArea area,
            Cairo.Context context,
            int width,
            int height
        ) {
            if (!working || width <= 0 || height <= 0) {
                return;
            }

            double diameter =
                width < height ? width : height;
            double radius = diameter / 2.0 - 2.0;

            if (radius <= 0.0) {
                return;
            }

            double center_x = width / 2.0;
            double center_y = height / 2.0;
            double start_angle =
                phase - (Math.PI / 2.0);
            double end_angle =
                start_angle + ARC_SPAN;

            context.set_line_cap (Cairo.LineCap.ROUND);

            // A very faint fixed sleeve makes the orbit readable on
            // both light and dark header backgrounds.
            context.set_line_width (1.0);
            context.set_source_rgba (
                0.78,
                0.64,
                0.35,
                0.20
            );
            context.arc (
                center_x,
                center_y,
                radius,
                0.0,
                2.0 * Math.PI
            );
            context.stroke ();

            // Soft halo behind the moving highlight.
            context.set_line_width (4.0);
            context.set_source_rgba (
                0.78,
                0.64,
                0.35,
                0.12
            );
            context.arc (
                center_x,
                center_y,
                radius,
                start_angle,
                end_angle
            );
            context.stroke ();

            // The visible moving light.
            context.set_line_width (2.0);
            context.set_source_rgba (
                ACTIVE_GOLD_R,
                ACTIVE_GOLD_G,
                ACTIVE_GOLD_B,
                0.78
            );
            context.arc (
                center_x,
                center_y,
                radius,
                start_angle,
                end_angle
            );
            context.stroke ();
        }
    }
}
