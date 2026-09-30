#include "chart_view.h"
#include <string.h>

struct _AtmChartView {
    GtkBox parent_instance;
    AtmChartSpec *spec;
    AtmChartRender *render;
    char *data;
    GtkStack *stack;
    GtkDrawingArea *area;
    GtkColumnView *table;
    GtkStringList *rows;
    GtkSingleSelection *selection;
    GtkTextBuffer *provenance;
    GtkLabel *status;
    GSource *resize_source;
    int requested_width, requested_height, requested_scale;
    int render_width, render_height, render_scale;
#ifdef ATM_CHART_VIEW_TESTING
    guint test_resize_apply_count;
#endif
};
G_DEFINE_TYPE (AtmChartView, atm_chart_view, GTK_TYPE_BOX)
static gboolean reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message); return FALSE;
}
static void dispose (GObject *object)
{
    AtmChartView *self = ATM_CHART_VIEW (object);
    if (self->resize_source) {
        g_source_destroy (self->resize_source);
        g_source_unref (self->resize_source);
        self->resize_source = NULL;
    }
    if (self->area) {
        g_signal_handlers_disconnect_by_data (self->area, self);
        gtk_drawing_area_set_draw_func (self->area, NULL, NULL, NULL); self->area = NULL;
    }
    if (self->selection) g_signal_handlers_disconnect_by_data (self->selection, self);
    g_clear_object (&self->selection); g_clear_object (&self->rows); g_clear_object (&self->provenance);
    self->stack = NULL; self->table = NULL; self->status = NULL;
    G_OBJECT_CLASS (atm_chart_view_parent_class)->dispose (object);
}
static void finalize (GObject *object)
{
    AtmChartView *self = ATM_CHART_VIEW (object);
    atm_chart_spec_free (self->spec); atm_chart_render_free (self->render); g_free (self->data);
    G_OBJECT_CLASS (atm_chart_view_parent_class)->finalize (object);
}
static void atm_chart_view_class_init (AtmChartViewClass *klass)
{
    GObjectClass *object = G_OBJECT_CLASS (klass); object->dispose = dispose; object->finalize = finalize;
    gtk_widget_class_set_accessible_role (GTK_WIDGET_CLASS (klass), GTK_ACCESSIBLE_ROLE_GROUP);
}
static void atm_chart_view_init (AtmChartView *self)
{
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing (GTK_BOX (self), 8);
}
static void accessible (GtkWidget *widget, const char *label, const char *description)
{
    gtk_accessible_update_property (GTK_ACCESSIBLE (widget), GTK_ACCESSIBLE_PROPERTY_LABEL, label,
        GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description, -1);
}
static void draw (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    (void) area; (void) width; (void) height;
    AtmChartView *self = data;
    cairo_set_source_rgb (cr, 1, 1, 1); cairo_paint (cr);
    if (self->render) {
        cairo_set_source_surface (cr, atm_chart_render_surface (self->render), 0, 0); cairo_paint (cr);
    }
}
static gboolean apply_pending_resize (gpointer data)
{
    AtmChartView *self = data;
    GSource *source = self->resize_source;
    self->resize_source = NULL;
    if (source) g_source_unref (source);
#ifdef ATM_CHART_VIEW_TESTING
    self->test_resize_apply_count++;
#endif
    int width = self->requested_width, height = self->requested_height, scale = self->requested_scale;
    GError *error = NULL;
    gboolean ok = width >= 240 && height >= 140 && scale >= 1 && scale <= 2 &&
        atm_chart_render_resize (self->render, (guint) width, (guint) height, (guint) scale, &error);
    if (ok) {
        self->render_width = width; self->render_height = height; self->render_scale = scale;
    }
    gtk_widget_set_visible (GTK_WIDGET (self->status), !ok);
    gtk_label_set_text (self->status, !ok ? "Chart unavailable at this size or scale. Exact values are in Data." : "");
    accessible (GTK_WIDGET (self->area), "Global annual temperature anomaly chart",
        !ok || atm_chart_render_is_fallback (self->render) ? "Chart unavailable. Switch to Data for all 146 exact observations." :
        "146 empirical observations from 1880 to 2025. Degrees Celsius relative to 1951-1980. Exact values are in Data.");
    g_clear_error (&error);
    gtk_widget_queue_draw (GTK_WIDGET (self->area));
    return G_SOURCE_REMOVE;
}
static void request_resize (AtmChartView *self, int width, int height, int scale)
{
    if (width == self->requested_width && height == self->requested_height && scale == self->requested_scale) {
        if (self->resize_source ||
            (width == self->render_width && height == self->render_height && scale == self->render_scale))
            return;
    }
    self->requested_width = width; self->requested_height = height; self->requested_scale = scale;
    if (self->resize_source) return;
    self->resize_source = g_idle_source_new ();
    g_source_set_priority (self->resize_source, G_PRIORITY_DEFAULT_IDLE);
    g_source_set_callback (self->resize_source, apply_pending_resize, self, NULL);
    g_source_attach (self->resize_source, NULL);
}
static void resize (GtkDrawingArea *area, int width, int height, gpointer data)
{
    AtmChartView *self = data;
    request_resize (self, width, height, gtk_widget_get_scale_factor (GTK_WIDGET (area)));
}
static void scale_changed (GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) pspec;
    GtkWidget *area = GTK_WIDGET (object);
    request_resize (data, gtk_widget_get_width (area), gtk_widget_get_height (area),
        gtk_widget_get_scale_factor (area));
}
static void cell_setup (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void) factory; (void) data;
    GtkWidget *label = gtk_label_new (NULL); gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_widget_set_margin_start (label, 8); gtk_widget_set_margin_end (label, 8);
    gtk_widget_set_margin_top (label, 4); gtk_widget_set_margin_bottom (label, 4);
    gtk_widget_add_css_class (label, "monospace");
    gtk_list_item_set_child (item, label);
}
static void cell_bind (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void) factory;
    const char *line = gtk_string_object_get_string (GTK_STRING_OBJECT (gtk_list_item_get_item (item)));
    char **cells = g_strsplit (line, "\t", -1);
    gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (item)), cells[GPOINTER_TO_UINT (data)]);
    g_strfreev (cells);
}
static void selection_changed (GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) object; (void) pspec;
    AtmChartView *self = data;
    GtkStringObject *row = GTK_STRING_OBJECT (gtk_single_selection_get_selected_item (self->selection));
    if (!row) { gtk_text_buffer_set_text (self->provenance, "Select an observation to inspect its source.", -1); return; }
    char **c = g_strsplit (gtk_string_object_get_string (row), "\t", -1);
    GString *text = g_string_new (NULL);
    g_string_append_printf (text, "Year: %s\nExact anomaly: %s deg C (1951-1980)\nCanonical decimal: %se%s\n"
        "Source row: %s\nRepository: LaurentiuStaicu/empirical-world3-dynamics\n"
        "Snapshot: d9e249339663015f6d1c05752338a955bf64ad0b\n"
        "Path: science/data/processed/nasa_gistemp_global_2026.csv\n"
        "Vintage: 2026-08-31\nQualified series: %s\nSupport identities:\n", c[1],c[2],c[3],c[4],c[5],c[6]);
    char **ids = g_strsplit (c[7], ";", -1);
    for (guint i = 0; ids[i]; i++) g_string_append_printf (text, "%s\n", ids[i]);
    gtk_text_buffer_set_text (self->provenance, text->str, -1);
    g_strfreev (ids); g_strfreev (c); g_string_free (text, TRUE);
}
void atm_chart_view_show_chart (AtmChartView *self)
{
    if (!self || !self->stack) return;
    gtk_stack_set_visible_child_name (self->stack, "chart"); gtk_widget_grab_focus (GTK_WIDGET (self->area));
}
void atm_chart_view_show_data (AtmChartView *self)
{
    if (!self || !self->stack) return;
    gtk_stack_set_visible_child_name (self->stack, "data");
    gtk_column_view_scroll_to (self->table, gtk_single_selection_get_selected (self->selection),
        NULL, GTK_LIST_SCROLL_FOCUS, NULL);
    gtk_widget_grab_focus (GTK_WIDGET (self->table));
}
static gboolean key_pressed (GtkEventControllerKey *controller, guint key, guint code, GdkModifierType state, gpointer data)
{
    (void) controller; (void) code;
    if ((state & gtk_accelerator_get_default_mod_mask ()) != GDK_ALT_MASK) return FALSE;
    if (key == GDK_KEY_1) { atm_chart_view_show_chart (data); return TRUE; }
    if (key == GDK_KEY_2) { atm_chart_view_show_data (data); return TRUE; }
    return FALSE;
}
gboolean atm_chart_view_new (const AtmChartSpec *spec, AtmChartView **out, GError **error)
{
    if (!out || *out) return reject (error, "Invalid chart widget output.");
    AtmChartSpec *owned = NULL;
    if (!atm_chart_spec_rebuild (spec, &owned, error)) return FALSE;
    AtmChartRender *initial = NULL;
    if (!atm_chart_render_new (owned, 860, 500, 1, &initial, error)) { atm_chart_spec_free (owned); return FALSE; }
    AtmChartView *self = g_object_ref_sink (g_object_new (ATM_TYPE_CHART_VIEW, "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP, NULL));
    self->spec = owned; self->render = initial;
    self->requested_width = self->render_width = 860;
    self->requested_height = self->render_height = 500;
    self->requested_scale = self->render_scale = 1;
    self->data = g_strdup (atm_chart_render_data (initial));
    self->rows = gtk_string_list_new (NULL);
    char **lines = g_strsplit (self->data, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        if (!lines[i][0] || lines[i][0] == '#' || g_str_has_prefix (lines[i], "series\t")) continue;
        char **cells = g_strsplit (lines[i], "\t", -1); gboolean valid = g_strv_length (cells) == 8;
        g_strfreev (cells);
        if (!valid) { g_strfreev (lines); g_object_unref (self); return reject (error, "Invalid exact Data view row."); }
        gtk_string_list_append (self->rows, lines[i]);
    }
    g_strfreev (lines);
    accessible (GTK_WIDGET (self), "Qualified scientific chart and exact data", "Alt+1 opens Chart. Alt+2 opens Data.");
    self->stack = GTK_STACK (gtk_stack_new ()); gtk_stack_set_transition_type (self->stack, GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand (GTK_WIDGET (self->stack), TRUE);
    GtkWidget *switcher = gtk_stack_switcher_new (); gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), self->stack);
    gtk_widget_set_halign (switcher, GTK_ALIGN_START); gtk_box_append (GTK_BOX (self), switcher);
    gtk_widget_set_tooltip_text (switcher, "Chart: Alt+1. Data: Alt+2.");
    GtkWidget *chart = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    self->status = GTK_LABEL (gtk_label_new ("")); gtk_label_set_wrap (self->status, TRUE);
    gtk_label_set_xalign (self->status, 0); gtk_widget_set_visible (GTK_WIDGET (self->status), FALSE);
    gtk_box_append (GTK_BOX (chart), GTK_WIDGET (self->status));
    self->area = GTK_DRAWING_AREA (gtk_drawing_area_new ()); gtk_drawing_area_set_content_height (self->area, 140);
    gtk_widget_set_hexpand (GTK_WIDGET (self->area), TRUE); gtk_widget_set_vexpand (GTK_WIDGET (self->area), TRUE);
    gtk_widget_set_focusable (GTK_WIDGET (self->area), TRUE);
    accessible (GTK_WIDGET (self->area), "Global annual temperature anomaly chart", "Exact observations and provenance are in Data.");
    gtk_drawing_area_set_draw_func (self->area, draw, self, NULL);
    g_signal_connect (self->area, "resize", G_CALLBACK (resize), self);
    g_signal_connect (self->area, "notify::scale-factor", G_CALLBACK (scale_changed), self);
    gtk_box_append (GTK_BOX (chart), GTK_WIDGET (self->area)); gtk_stack_add_titled (self->stack, chart, "chart", "Chart");
    GtkWidget *data = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *unit = gtk_label_new ("Global annual anomaly (deg C), relative to 1951-1980. Exact source decimals.");
    gtk_label_set_wrap (GTK_LABEL (unit), TRUE); gtk_label_set_xalign (GTK_LABEL (unit), 0);
    gtk_box_append (GTK_BOX (data), unit);
    self->selection = gtk_single_selection_new (G_LIST_MODEL (g_object_ref (self->rows)));
    self->table = GTK_COLUMN_VIEW (gtk_column_view_new (GTK_SELECTION_MODEL (g_object_ref (self->selection))));
    gtk_column_view_set_show_row_separators (self->table, TRUE); gtk_widget_add_css_class (GTK_WIDGET (self->table), "data-table");
    accessible (GTK_WIDGET (self->table), "Exact annual observations", "Select a year with arrow keys to inspect its provenance. Source order is preserved.");
    const char *titles[] = {"Year", "Anomaly (deg C)", "Source row"}; guint fields[] = {1,2,5};
    for (guint i = 0; i < 3; i++) {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
        g_signal_connect (factory, "setup", G_CALLBACK (cell_setup), NULL);
        g_signal_connect (factory, "bind", G_CALLBACK (cell_bind), GUINT_TO_POINTER (fields[i]));
        GtkColumnViewColumn *column = gtk_column_view_column_new (titles[i], factory);
        gtk_column_view_column_set_expand (column, i == 1); gtk_column_view_append_column (self->table, column); g_object_unref (column);
    }
    GtkWidget *scroll = gtk_scrolled_window_new (); gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), GTK_WIDGET (self->table));
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroll), 180); gtk_widget_set_vexpand (scroll, TRUE);
    gtk_box_append (GTK_BOX (data), scroll);
    GtkWidget *detail = gtk_text_view_new (); gtk_text_view_set_editable (GTK_TEXT_VIEW (detail), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (detail), FALSE); gtk_text_view_set_monospace (GTK_TEXT_VIEW (detail), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (detail), GTK_WRAP_CHAR);
    accessible (detail, "Selected observation provenance", "Exact value, source location and evidence support identities. Read-only text.");
    self->provenance = g_object_ref (gtk_text_view_get_buffer (GTK_TEXT_VIEW (detail)));
    GtkWidget *details_scroll = gtk_scrolled_window_new (); gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (details_scroll), detail);
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (details_scroll), 140);
    GtkWidget *expander = gtk_expander_new ("Provenance of selected observation"); gtk_expander_set_child (GTK_EXPANDER (expander), details_scroll);
    gtk_box_append (GTK_BOX (data), expander); gtk_stack_add_titled (self->stack, data, "data", "Data");
    gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->stack));
    g_signal_connect (self->selection, "notify::selected", G_CALLBACK (selection_changed), self);
    selection_changed (NULL, NULL, self);
    GtkEventController *keys = gtk_event_controller_key_new (); gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
    g_signal_connect_object (keys, "key-pressed", G_CALLBACK (key_pressed), self, 0);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);
    *out = self; return TRUE;
}
guint atm_chart_view_row_count (AtmChartView *self) { return self && self->rows ? g_list_model_get_n_items (G_LIST_MODEL (self->rows)) : 0; }
const char *atm_chart_view_data (AtmChartView *self) { return self ? self->data : NULL; }
const char *atm_chart_view_visible_page (AtmChartView *self) { return self && self->stack ? gtk_stack_get_visible_child_name (self->stack) : NULL; }
gboolean atm_chart_view_select_row (AtmChartView *self, guint row)
{
    if (!self || !self->selection || row >= atm_chart_view_row_count (self)) return FALSE;
    gtk_single_selection_set_selected (self->selection, row); return TRUE;
}
guint atm_chart_view_selected_row (AtmChartView *self) { return self && self->selection ? gtk_single_selection_get_selected (self->selection) : GTK_INVALID_LIST_POSITION; }
char *atm_chart_view_provenance (AtmChartView *self)
{
    if (!self || !self->provenance) return NULL;
    GtkTextIter start, end; gtk_text_buffer_get_bounds (self->provenance, &start, &end);
    return gtk_text_buffer_get_text (self->provenance, &start, &end, FALSE);
}

#ifdef ATM_CHART_VIEW_TESTING
void atm_chart_view_test_request_resize (AtmChartView *self, int width, int height, int scale)
{
    if (self) request_resize (self, width, height, scale);
}
guint atm_chart_view_test_resize_apply_count (AtmChartView *self)
{
    return self ? self->test_resize_apply_count : 0;
}
void atm_chart_view_test_render_size (AtmChartView *self, int *width, int *height, int *scale)
{
    if (!self) return;
    if (width) *width = self->render_width;
    if (height) *height = self->render_height;
    if (scale) *scale = self->render_scale;
}
#endif
