#include "chart_m4.h"

#include <cairo.h>
#include <stdint.h>
#include <string.h>

#define WIDTH 16u
#define HEIGHT 20u
#define MARGIN 4u
#define FRACTIONAL_COUNT 128u
#define INTEGER_PER_COLUMN 8u
#define INTEGER_COUNT (WIDTH * INTEGER_PER_COLUMN)

typedef struct {
    double x;
    double y;
} Point;

typedef enum {
    MAP_FLOOR,
    MAP_NEAREST
} ColumnMap;

static cairo_surface_t *
render (const Point *points, guint count)
{
    cairo_surface_t *surface = cairo_image_surface_create (
        CAIRO_FORMAT_ARGB32, WIDTH + 2 * MARGIN, HEIGHT + 2 * MARGIN);
    g_assert_cmpint (cairo_surface_status (surface), ==, CAIRO_STATUS_SUCCESS);

    cairo_t *cr = cairo_create (surface);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_width (cr, 1.5);
    cairo_rectangle (cr, 1, 1, WIDTH + 2 * MARGIN - 2, HEIGHT + 2 * MARGIN - 2);
    cairo_clip (cr);

    for (guint i = 0; i < count; i++) {
        double x = MARGIN + points[i].x;
        double y = MARGIN + points[i].y;
        if (i == 0)
            cairo_move_to (cr, x, y);
        else
            cairo_line_to (cr, x, y);
    }
    cairo_stroke (cr);
    g_assert_cmpint (cairo_status (cr), ==, CAIRO_STATUS_SUCCESS);
    cairo_destroy (cr);
    cairo_surface_flush (surface);
    return surface;
}

static gboolean
surface_equal (cairo_surface_t *a, cairo_surface_t *b)
{
    cairo_surface_flush (a);
    cairo_surface_flush (b);
    int height = cairo_image_surface_get_height (a);
    int stride = cairo_image_surface_get_stride (a);
    g_assert_cmpint (height, ==, cairo_image_surface_get_height (b));
    g_assert_cmpint (stride, ==, cairo_image_surface_get_stride (b));
    return memcmp (cairo_image_surface_get_data (a),
                   cairo_image_surface_get_data (b),
                   (gsize) height * stride) == 0;
}

static guint
column_for (double x, ColumnMap map)
{
    gint column = map == MAP_FLOOR ? (gint) x : (gint) (x + 0.5);
    return (guint) CLAMP (column, 0, (gint) WIDTH - 1);
}

static guint
reduce (const Point *input, guint count, ColumnMap map, Point *output)
{
    AtmChartM4Point *geometry = g_new (AtmChartM4Point, count);
    for (guint i = 0; i < count; i++) {
        geometry[i].source_index = i;
        geometry[i].pixel_column = column_for (input[i].x, map);
        geometry[i].y = input[i].y;
    }

    AtmChartM4Plan *plan = NULL;
    g_assert_true (atm_chart_m4_plan_new (geometry, count, WIDTH, &plan, NULL));
    g_assert_true (atm_chart_m4_plan_is_reduced (plan));
    guint reduced = atm_chart_m4_plan_count (plan);
    for (guint i = 0; i < reduced; i++) {
        const AtmChartM4Point *selected = atm_chart_m4_plan_point (plan, i);
        output[i] = input[selected->source_index];
    }

    atm_chart_m4_plan_free (plan);
    g_free (geometry);
    return reduced;
}

static void
test_integer_column_equivalence (void)
{
    Point input[INTEGER_COUNT], reduced[INTEGER_COUNT];
    guint n = 0;
    for (guint column = 0; column < WIDTH; column++) {
        for (guint j = 0; j < INTEGER_PER_COLUMN; j++) {
            input[n].x = column;
            input[n].y = (double) ((column * 7 + j * 11) % (HEIGHT - 1));
            n++;
        }
    }
    g_assert_cmpuint (n, ==, INTEGER_COUNT);

    cairo_surface_t *full = render (input, n);
    guint reduced_count = reduce (input, n, MAP_FLOOR, reduced);
    g_assert_cmpuint (reduced_count, <, n);
    cairo_surface_t *m4 = render (reduced, reduced_count);

    g_assert_true (surface_equal (full, m4));
    cairo_surface_destroy (m4);
    cairo_surface_destroy (full);
}

static void
build_fractional (Point *input)
{
    for (guint i = 0; i < FRACTIONAL_COUNT; i++) {
        input[i].x = (double) i / (FRACTIONAL_COUNT - 1) * WIDTH;
        input[i].y = (double) ((i * 17) % 31) / 30.0 * (HEIGHT - 1);
    }
}

static void
test_fractional_floor_not_admitted (void)
{
    Point input[FRACTIONAL_COUNT], reduced[FRACTIONAL_COUNT];
    build_fractional (input);
    cairo_surface_t *full = render (input, FRACTIONAL_COUNT);
    guint reduced_count = reduce (input, FRACTIONAL_COUNT, MAP_FLOOR, reduced);
    cairo_surface_t *m4 = render (reduced, reduced_count);

    g_assert_false (surface_equal (full, m4));
    cairo_surface_destroy (m4);
    cairo_surface_destroy (full);
}

static void
test_fractional_nearest_not_admitted (void)
{
    Point input[FRACTIONAL_COUNT], reduced[FRACTIONAL_COUNT];
    build_fractional (input);
    cairo_surface_t *full = render (input, FRACTIONAL_COUNT);
    guint reduced_count = reduce (input, FRACTIONAL_COUNT, MAP_NEAREST, reduced);
    cairo_surface_t *m4 = render (reduced, reduced_count);

    g_assert_false (surface_equal (full, m4));
    cairo_surface_destroy (m4);
    cairo_surface_destroy (full);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-m4-cairo/integer-column-equivalence",
                     test_integer_column_equivalence);
    g_test_add_func ("/chart-m4-cairo/fractional-floor-not-admitted",
                     test_fractional_floor_not_admitted);
    g_test_add_func ("/chart-m4-cairo/fractional-nearest-not-admitted",
                     test_fractional_nearest_not_admitted);
    int result = g_test_run ();
    cairo_debug_reset_static_data ();
    return result;
}
