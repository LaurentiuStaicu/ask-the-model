#include "chart_view.h"
#include "chart_fixture.h"
#include <string.h>

static void settle (void)
{
    gint64 deadline = g_get_monotonic_time () + 400000;
    do { while (g_main_context_iteration (NULL, FALSE)); g_usleep (1000); }
    while (g_get_monotonic_time () < deadline);
}
static void screenshot (GtkWindow *window, const char *name)
{
    const char *directory = g_getenv ("ATM_CHART03_GTK_OUTPUT"); if (!directory) return;
    g_assert_cmpint (g_mkdir_with_parents (directory, 0700), ==, 0);
    GtkWidget *widget = GTK_WIDGET (window);
    GdkPaintable *paintable = gtk_widget_paintable_new (widget);
    GtkSnapshot *snapshot = gtk_snapshot_new ();
    gdk_paintable_snapshot (paintable, GDK_SNAPSHOT (snapshot), gtk_widget_get_width (widget), gtk_widget_get_height (widget));
    GskRenderNode *node = gtk_snapshot_free_to_node (snapshot); g_assert_nonnull (node);
    GdkTexture *texture = gsk_renderer_render_texture (gtk_native_get_renderer (GTK_NATIVE (window)), node, NULL);
    char *path = g_build_filename (directory, name, NULL);
    g_assert_true (gdk_texture_save_to_png (texture, path));
    g_free (path); g_object_unref (texture); gsk_render_node_unref (node); g_object_unref (paintable);
}
static AtmChartView *build_view (void)
{
    AtmChartSpec *spec = atm_chart_test_spec (ATM_CHART_LINE); AtmChartView *view = NULL;
    g_assert_true (atm_chart_view_new (spec, &view, NULL)); atm_chart_spec_free (spec); return view;
}
static void test_data_lifetime (void)
{
    AtmChartView *view = build_view ();
    g_assert_false (g_object_is_floating (view));
    g_assert_cmpuint (atm_chart_view_row_count (view), ==, 146);
    g_assert_cmpint (gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (view)), ==, GTK_ACCESSIBLE_ROLE_GROUP);
    char *data = g_strdup (atm_chart_view_data (view));
    g_assert_nonnull (strstr (data, "1880\t-0.1700\t-17\t-2\t2\t"));
    for (guint i = 0; i < 146; i++) {
        g_assert_true (atm_chart_view_select_row (view, i));
        char *text = atm_chart_view_provenance (view), *year = g_strdup_printf ("Year: %u\n",1880+i);
        g_assert_true (g_str_has_prefix (text,year));
        g_assert_nonnull (strstr (text, "Support identities:\n")); g_free (year); g_free (text);
    }
    g_assert_false (atm_chart_view_select_row (view, 146));
    g_assert_cmpuint (atm_chart_view_selected_row (view), ==, 145);
    atm_chart_view_show_data (view); g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "data");
    atm_chart_view_show_chart (view); g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    g_assert_cmpstr (atm_chart_view_data (view), ==, data); g_free (data);
    gpointer weak = view; g_object_add_weak_pointer (G_OBJECT (view), &weak); g_object_unref (view); g_assert_null (weak);
}
static void test_resize_coalescing (void)
{
    AtmChartView *view = build_view ();
    char *data = g_strdup (atm_chart_view_data (view));
    guint baseline = atm_chart_view_test_resize_apply_count (view);

    atm_chart_view_test_request_resize (view, 700, 400, 1);
    atm_chart_view_test_request_resize (view, 760, 440, 1);
    atm_chart_view_test_request_resize (view, 820, 480, 1);
    g_assert_cmpuint (atm_chart_view_test_resize_apply_count (view), ==, baseline);
    settle ();
    g_assert_cmpuint (atm_chart_view_test_resize_apply_count (view), ==, baseline + 1);
    int width = 0, height = 0, scale = 0;
    atm_chart_view_test_render_size (view, &width, &height, &scale);
    g_assert_cmpint (width, ==, 820); g_assert_cmpint (height, ==, 480); g_assert_cmpint (scale, ==, 1);
    g_assert_cmpstr (atm_chart_view_data (view), ==, data);

    atm_chart_view_test_request_resize (view, 120, 100, 1);
    settle ();
    g_assert_cmpuint (atm_chart_view_test_resize_apply_count (view), ==, baseline + 2);
    atm_chart_view_test_render_size (view, &width, &height, &scale);
    g_assert_cmpint (width, ==, 820); g_assert_cmpint (height, ==, 480); g_assert_cmpint (scale, ==, 1);
    g_assert_cmpstr (atm_chart_view_data (view), ==, data);

    atm_chart_view_test_request_resize (view, 600, 360, 2);
    settle ();
    g_assert_cmpuint (atm_chart_view_test_resize_apply_count (view), ==, baseline + 3);
    atm_chart_view_test_render_size (view, &width, &height, &scale);
    g_assert_cmpint (width, ==, 600); g_assert_cmpint (height, ==, 360); g_assert_cmpint (scale, ==, 2);
    g_assert_cmpstr (atm_chart_view_data (view), ==, data);
    g_free (data);
    g_object_unref (view);

    AtmChartView *pending = build_view ();
    atm_chart_view_test_request_resize (pending, 740, 420, 1);
    gpointer weak = pending;
    g_object_add_weak_pointer (G_OBJECT (pending), &weak);
    g_object_unref (pending);
    g_assert_null (weak);
    while (g_main_context_iteration (NULL, FALSE));
}

static void test_keyboard_window (void)
{
    if (!g_getenv ("ATM_CHART03_KEYBOARD")) { g_test_skip ("Real XTest keyboard exercised by dedicated GTK workflow."); return; }
    AtmChartView *view = build_view (); GtkWindow *window = GTK_WINDOW (gtk_window_new ());
    gtk_window_set_title (window, "AtM chart component test"); gtk_window_set_default_size (window, 900, 680);
    gtk_widget_set_margin_start (GTK_WIDGET (view), 12); gtk_widget_set_margin_end (GTK_WIDGET (view), 12);
    gtk_widget_set_margin_top (GTK_WIDGET (view), 12); gtk_widget_set_margin_bottom (GTK_WIDGET (view), 12);
    gtk_window_set_child (window, GTK_WIDGET (view)); gtk_window_present (window); settle ();
    screenshot (window, "atm-gtk-chart.png");
    int status; GError *error = NULL;
    g_assert_true (g_spawn_command_line_sync (
        "xdotool search --onlyvisible --name '^AtM chart component test$' windowfocus --sync key --clearmodifiers alt+2",
        NULL,NULL,&status,&error)); g_assert_no_error (error); g_assert_cmpint (status, ==, 0); settle ();
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "data");
    g_assert_true (g_spawn_command_line_sync ("xdotool key Down",NULL,NULL,&status,&error));
    g_assert_no_error (error); g_assert_cmpint (status, ==, 0); settle ();
    g_assert_cmpuint (atm_chart_view_selected_row (view), ==, 1);
    char *provenance = atm_chart_view_provenance (view); g_assert_true (g_str_has_prefix (provenance, "Year: 1881\n")); g_free (provenance);
    screenshot (window, "atm-gtk-data.png");
    g_assert_true (g_spawn_command_line_sync ("xdotool key --clearmodifiers alt+1",NULL,NULL,&status,&error));
    g_assert_no_error (error); g_assert_cmpint (status, ==, 0); settle ();
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    char *before = g_strdup (atm_chart_view_data (view));
    gtk_window_set_default_size (window, 300, 420); settle ();
    g_assert_cmpstr (atm_chart_view_data (view), ==, before); g_free (before);
    screenshot (window, "atm-gtk-small.png");
    gtk_window_destroy (window); settle ();
    gpointer weak = view; g_object_add_weak_pointer (G_OBJECT (view), &weak); g_object_unref (view); g_assert_null (weak);
}
static void test_invalid (void)
{
    AtmChartSpec *spec = atm_chart_test_spec (ATM_CHART_LINE); AtmChartView *view = NULL;
    g_assert_false (atm_chart_view_new (NULL,&view,NULL)); g_assert_null (view);
    g_assert_false (atm_chart_view_new (spec,NULL,NULL));
    g_assert_true (atm_chart_view_new (spec,&view,NULL)); AtmChartView *saved = view;
    g_assert_false (atm_chart_view_new (spec,&view,NULL)); g_assert_true (view == saved); g_object_unref (view); view = NULL;
    ((AtmScientificDecimal *) atm_chart_spec_y_min (spec))->exponent++;
    g_assert_false (atm_chart_view_new (spec,&view,NULL)); g_assert_null (view); atm_chart_spec_free (spec);
    g_assert_null (atm_chart_view_data (NULL)); g_assert_null (atm_chart_view_provenance (NULL));
    g_assert_false (atm_chart_view_select_row (NULL,0));
}
int main (int argc, char **argv)
{
    gtk_init (); g_test_init (&argc,&argv,NULL);
    g_test_add_func ("/chart-view/data-lifetime",test_data_lifetime);
    g_test_add_func ("/chart-view/resize-coalescing",test_resize_coalescing);
    g_test_add_func ("/chart-view/keyboard-window",test_keyboard_window);
    g_test_add_func ("/chart-view/invalid",test_invalid);
    return g_test_run ();
}
