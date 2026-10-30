#include "chart_view.h"
#include "chart_catalog.h"
#include "chart_render_v4.h"
#include "chart_spec_v4.h"
#include <string.h>

struct _AtmChartView {
    GtkBox parent_instance;
    AtmChartRender *render;
    AtmChartRenderV4 *render_v4;
    char *data;
    GtkStack *stack;
    GtkDrawingArea *area;
    GtkScrolledWindow *data_scroll;
    GtkScrolledWindow *details_scroll;
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
    atm_chart_render_free (self->render);
    atm_chart_render_v4_free (self->render_v4);
    g_free (self->data);
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
static const char *chart_source_profile (const AtmChartSpec *spec)
{
    if (spec == NULL)
        return "gistemp";
    guint version = atm_chart_spec_version (spec);
    if (version == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3)
        return "bnr";
    if (version == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2)
        return "energy-institute";
    return "gistemp";
}
static const char *chart_primary_source_url (const char *profile)
{
    if (g_strcmp0 (profile, "bnr") == 0)
        return "https://www.bnr.ro/en/12067-bank-lending-survey";
    if (g_strcmp0 (profile, "energy-institute") == 0)
        return "https://www.energyinst.org/statistical-review/resources-and-data-downloads";
    return "https://data.giss.nasa.gov/gistemp/data_v4.html";
}
static const char *chart_primary_source_label (const char *profile)
{
    if (g_strcmp0 (profile, "bnr") == 0)
        return "Open BNR survey";
    if (g_strcmp0 (profile, "energy-institute") == 0)
        return "Open Energy Institute data";
    return "Open NASA GISTEMP data";
}
static const char *chart_primary_source_summary (const char *profile)
{
    if (g_strcmp0 (profile, "bnr") == 0)
        return "Primary source: National Bank of Romania, Bank Lending Survey (BLS). Quarterly net percentage for credit standards on loans to non-financial corporations; positive values indicate tightening and negative values easing. 2025-Q2 is missing, not zero.";
    if (g_strcmp0 (profile, "energy-institute") == 0)
        return "Primary source: Energy Institute, Statistical Review of World Energy 2026. Global total energy supply uses the Physical Energy Content method, in exajoules per year (EJ/year; 1965-2025). This is gross supply, not net energy or EROI.";
    return "Primary source: NASA Goddard Institute for Space Studies (GISS), GISTEMP v4. Global annual Land-Ocean Temperature Index anomalies in degrees Celsius relative to the 1951-1980 mean (1880-2025; data vintage 31 August 2026).";
}
static char *chart_pinned_dataset_url (const AtmChartSpec *spec)
{
    if (spec == NULL)
        return NULL;
    const char *snapshot = atm_chart_spec_series_snapshot_sha (spec, 0);
    const char *path = atm_chart_spec_series_source_path (spec, 0);
    if (snapshot == NULL || snapshot[0] == '\0' || path == NULL || path[0] == '\0')
        return NULL;
    const char *repository =
        g_strcmp0 (chart_source_profile (spec), "bnr") == 0
            ? ATM_BNR_BLS_UPSTREAM_REPOSITORY
            : "LaurentiuStaicu/empirical-world3-dynamics";
    return g_strdup_printf ("https://github.com/%s/blob/%s/%s", repository, snapshot, path);
}
static void append_chart_source_attribution (GtkBox *container, const AtmChartSpec *spec)
{
    const char *profile = chart_source_profile (spec);
    GtkWidget *summary = gtk_label_new (chart_primary_source_summary (profile));
    gtk_label_set_wrap (GTK_LABEL (summary), TRUE);
    gtk_label_set_xalign (GTK_LABEL (summary), 0);
    gtk_widget_set_halign (summary, GTK_ALIGN_FILL);
    gtk_widget_set_margin_start (summary, 8);
    gtk_widget_set_margin_end (summary, 8);
    gtk_widget_set_margin_top (summary, 4);
    gtk_widget_add_css_class (summary, "atm-source-summary");
    gtk_box_append (container, summary);

    GtkWidget *links = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start (links, 8);
    gtk_widget_set_margin_end (links, 8);
    gtk_widget_set_margin_bottom (links, 6);
    GtkWidget *primary = gtk_link_button_new_with_label (
        chart_primary_source_url (profile), chart_primary_source_label (profile));
    gtk_widget_set_halign (primary, GTK_ALIGN_START);
    gtk_widget_add_css_class (primary, "atm-source-link");
    gtk_box_append (GTK_BOX (links), primary);

    char *pinned_url = chart_pinned_dataset_url (spec);
    if (pinned_url != NULL) {
        GtkWidget *pinned = gtk_link_button_new_with_label (
            pinned_url, "Open pinned repository copy");
        gtk_widget_set_halign (pinned, GTK_ALIGN_START);
        gtk_widget_add_css_class (pinned, "atm-source-link");
        gtk_box_append (GTK_BOX (links), pinned);
        g_free (pinned_url);
    }
    gtk_box_append (container, links);
}

/* CHART-06f: catalog-bound presentation metadata for the v4 path. The old
 * v1/v2/v3 constructors keep their historical wording and exact Data schema. */
static void append_v4_source_attribution (GtkBox *container,
                                          const AtmChartSpecV4 *spec_v4)
{
    const AtmChartCatalogEntry *entry =
        atm_chart_spec_v4_catalog_entry (spec_v4);
    const AtmChartSpec *source =
        atm_chart_spec_v4_source_spec (spec_v4);
    if (entry == NULL || source == NULL)
        return;

    /*
     * Keep the visible source summary human-readable and concise. Exact
     * snapshot/path/count/decimal metadata stays in the trusted catalog,
     * identities and Data/provenance contract, not in the ordinary paragraph.
     */
    char *summary_text = g_strdup_printf (
        "Primary source: %s.\n%s\n"
        "AtM reconstructs the pinned processed dataset; it does not independently "
        "reproduce the publisher's original processing pipeline.",
        entry->publisher_name != NULL ? entry->publisher_name : "Registered publisher",
        entry->description != NULL ? entry->description
            : (entry->display_name != NULL ? entry->display_name : "Qualified source series.")
    );

    GtkWidget *summary = gtk_label_new (summary_text);
    g_free (summary_text);
    gtk_label_set_wrap (GTK_LABEL (summary), TRUE);
    gtk_label_set_xalign (GTK_LABEL (summary), 0);
    gtk_widget_set_halign (summary, GTK_ALIGN_FILL);
    gtk_widget_set_margin_start (summary, 8);
    gtk_widget_set_margin_end (summary, 8);
    gtk_widget_set_margin_top (summary, 4);
    gtk_widget_add_css_class (summary, "atm-source-summary");
    gtk_box_append (container, summary);

    GtkWidget *links = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start (links, 8);
    gtk_widget_set_margin_end (links, 8);
    gtk_widget_set_margin_bottom (links, 6);

    if (entry->publisher_url != NULL && entry->publisher_url[0] != '\0') {
        GtkWidget *primary = gtk_link_button_new_with_label (
            entry->publisher_url,
            entry->publisher_name != NULL ? entry->publisher_name : "Open publisher source");
        gtk_widget_set_halign (primary, GTK_ALIGN_START);
        gtk_widget_add_css_class (primary, "atm-source-link");
        gtk_box_append (GTK_BOX (links), primary);
    }

    const char *snapshot = atm_chart_spec_series_snapshot_sha (source, 0);
    const char *path = entry->source_path;
    if (snapshot != NULL && path != NULL && entry->repository_full_name != NULL &&
        atm_chart_catalog_snapshot_admitted (entry, snapshot)) {
        char *pinned_url = g_strdup_printf ("https://github.com/%s/blob/%s/%s",
            entry->repository_full_name, snapshot, path);
        GtkWidget *pinned = gtk_link_button_new_with_label (
            pinned_url, "Open pinned dataset used by AtM");
        gtk_widget_set_halign (pinned, GTK_ALIGN_START);
        gtk_widget_add_css_class (pinned, "atm-source-link");
        gtk_box_append (GTK_BOX (links), pinned);
        g_free (pinned_url);
    }
    gtk_box_append (container, links);
}

static const AtmChartSpec *view_source_spec (const AtmChartView *self)
{
    if (self == NULL)
        return NULL;
    if (self->render_v4 != NULL)
        return atm_chart_spec_v4_source_spec (
            atm_chart_render_v4_spec (self->render_v4));
    return self->render != NULL ? atm_chart_render_spec (self->render) : NULL;
}

static const AtmChartCatalogEntry *view_catalog_entry (const AtmChartView *self)
{
    if (self == NULL || self->render_v4 == NULL)
        return NULL;
    return atm_chart_spec_v4_catalog_entry (
        atm_chart_render_v4_spec (self->render_v4));
}

static cairo_surface_t *view_surface (const AtmChartView *self)
{
    if (self == NULL)
        return NULL;
    return self->render_v4 != NULL
        ? atm_chart_render_v4_surface (self->render_v4)
        : (self->render != NULL ? atm_chart_render_surface (self->render) : NULL);
}

static const char *view_data (const AtmChartView *self)
{
    if (self == NULL)
        return NULL;
    return self->render_v4 != NULL
        ? atm_chart_render_v4_data (self->render_v4)
        : (self->render != NULL ? atm_chart_render_data (self->render) : NULL);
}

static gboolean view_is_fallback (const AtmChartView *self)
{
    if (self == NULL)
        return FALSE;
    return self->render_v4 != NULL
        ? atm_chart_render_v4_is_fallback (self->render_v4)
        : (self->render != NULL && atm_chart_render_is_fallback (self->render));
}

static void append_view_source_attribution (GtkBox *container, AtmChartView *self)
{
    if (self->render_v4 != NULL)
        append_v4_source_attribution (container,
            atm_chart_render_v4_spec (self->render_v4));
    else
        append_chart_source_attribution (container, view_source_spec (self));
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
    if (self->render != NULL || self->render_v4 != NULL) {
        cairo_set_source_surface (cr, view_surface (self), 0, 0); cairo_paint (cr);
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
    gboolean ok = width >= 240 && height >= 140 && scale >= 1 && scale <= 2;

    if (ok && self->render_v4 != NULL) {
        AtmChartRenderV4 *resized = NULL;
        ok = atm_chart_render_v4_new (
            atm_chart_render_v4_spec (self->render_v4),
            (guint) width, (guint) height, (guint) scale, &resized, &error);
        if (ok) {
            AtmChartRenderV4 *previous = self->render_v4;
            self->render_v4 = resized;
            atm_chart_render_v4_free (previous);
        }
    } else if (ok && self->render != NULL) {
        ok = atm_chart_render_resize (
            self->render, (guint) width, (guint) height, (guint) scale, &error);
    } else {
        ok = FALSE;
    }

    if (ok) {
        self->render_width = width; self->render_height = height; self->render_scale = scale;
    }

    const AtmChartCatalogEntry *entry = view_catalog_entry (self);
    const AtmChartSpec *spec = view_source_spec (self);
    gboolean is_ei = spec != NULL &&
        atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2;
    gtk_widget_set_visible (GTK_WIDGET (self->status), !ok);

    if (entry != NULL) {
        char *status = !ok
            ? g_strdup_printf ("Chart unavailable at this size or scale. Data retains %u coordinates (%u numeric observations).",
                entry->coordinate_count, entry->numeric_observation_count)
            : g_strdup ("");
        gtk_label_set_text (self->status, status);
        char *description = g_strdup_printf (
            "%s Coverage %s–%s. Unit: %s. Exact source observations and provenance are in Data.",
            entry->observation_meaning != NULL ? entry->observation_meaning : entry->display_name,
            entry->coverage_start != NULL ? entry->coverage_start : "unknown",
            entry->coverage_end != NULL ? entry->coverage_end : "unknown",
            entry->y_unit != NULL ? entry->y_unit : "source-defined");
        accessible (GTK_WIDGET (self->area),
            entry->display_name != NULL ? entry->display_name : "Qualified scientific chart",
            (!ok || view_is_fallback (self))
                ? "Chart unavailable at this size. Switch to Data for the exact qualified observations."
                : description);
        g_free (description);
        g_free (status);
    } else {
        gtk_label_set_text (self->status, !ok
            ? (is_ei
                ? "Chart unavailable at this size or scale. All 61 exact TES observations remain in Data."
                : "Chart unavailable at this size or scale. Exact values are in Data.")
            : "");
        accessible (GTK_WIDGET (self->area),
            is_ei ? "Energy Institute global total energy supply chart"
                  : "Global annual temperature anomaly chart",
            !ok || view_is_fallback (self)
                ? (is_ei
                    ? "Chart unavailable. Switch to Data for all 61 exact TES observations."
                    : "Chart unavailable. Switch to Data for all 146 exact observations.")
                : (is_ei
                    ? "61 annual empirical observations from 1965 to 2025. Total energy supply under Physical Energy Content. Units are exajoules per year. Exact values are in Data."
                    : "146 empirical observations from 1880 to 2025. Degrees Celsius relative to 1951-1980. Exact values are in Data."));
    }
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
    guint col = GPOINTER_TO_UINT (data);
    const char *value = col < g_strv_length (cells) ? cells[col] : "";
    if (col == 2 && g_strcmp0 (value, "MISSING") == 0)
        value = "Missing";
    else if (col == 2 && g_strcmp0 (value, "NUMERIC") == 0)
        value = "Available";
    gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (item)), value);
    g_strfreev (cells);
}
static void selection_changed (GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) object; (void) pspec;
    AtmChartView *self = data;
    GtkStringObject *row = GTK_STRING_OBJECT (gtk_single_selection_get_selected_item (self->selection));
    if (!row) {
        gtk_text_buffer_set_text (self->provenance, "Select an observation to inspect its source.", -1);
        return;
    }
    char **c = g_strsplit (gtk_string_object_get_string (row), "\t", -1);
    const AtmChartSpec *spec = view_source_spec (self);
    gboolean is_bnr = spec != NULL &&
        atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3;
    guint required = is_bnr ? 10u : 8u;
    if (g_strv_length (c) != required) {
        g_strfreev (c);
        gtk_text_buffer_set_text (self->provenance, "Select an observation to inspect its source.", -1);
        return;
    }

    GString *text = g_string_new (NULL);
    gboolean is_ei = spec != NULL &&
        atm_chart_spec_version (spec) == ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2;
    if (is_bnr) {
        if (g_strcmp0 (c[2], "MISSING") == 0) {
            if (g_strcmp0 (c[1], "2025-Q2") != 0 || c[3][0] != '\0' ||
                c[4][0] != '\0' || g_strcmp0 (c[5], "0") != 0 ||
                g_strcmp0 (c[6], "none") != 0 ||
                g_strcmp0 (c[9], "source_round_not_retained") != 0) {
                g_strfreev (c);
                g_string_free (text, TRUE);
                gtk_text_buffer_set_text (self->provenance, "The selected missing observation failed source validation.", -1);
                return;
            }
            g_string_append_printf (text,
                "Quarter: %s\nStatus: Missing\n"
                "Reason: The source round was not retained.\n"
                "No numeric value is available; this is not zero.\n"
                "Primary source: National Bank of Romania, Bank Lending Survey (BLS)\n"
                "Measure: Quarterly net percentage for credit standards on loans to non-financial corporations.\n"
                "Positive values indicate net tightening; negative values indicate net easing.\n"
                "Pinned dataset: Romanian Monetary Dynamics\n",
                c[1]);
        } else if (g_strcmp0 (c[2], "NUMERIC") == 0 &&
                   c[3][0] != '\0' && c[4][0] != '\0' &&
                   c[6][0] != '\0' && g_strcmp0 (c[9], "") == 0) {
            g_string_append_printf (text,
                "Quarter: %s\nCredit standards net percentage: %s%%\n"
                "Primary source: National Bank of Romania, Bank Lending Survey (BLS)\n"
                "Measure: Net percentage for credit standards on loans to non-financial corporations.\n"
                "Positive values indicate net tightening; negative values indicate net easing.\n"
                "Pinned dataset: Romanian Monetary Dynamics\n",
                c[1], c[3]);
        } else {
            g_strfreev (c);
            g_string_free (text, TRUE);
            gtk_text_buffer_set_text (self->provenance, "The selected observation failed source validation.", -1);
            return;
        }
    } else if (is_ei) {
        g_string_append_printf (text,
            "Year: %s\nGlobal total primary energy supply: %s EJ/year\n"
            "Method: Physical Energy Content\n"
            "Primary source: Energy Institute, Statistical Review of World Energy 2026\n"
            "This is gross energy supply, not net energy or EROI.\n"
            "Pinned dataset: Empirical World3 Dynamics\n",
            c[1], c[2]);
    } else {
        g_string_append_printf (text,
            "Year: %s\nGlobal temperature anomaly: %s deg C\n"
            "Baseline: 1951-1980 mean\n"
            "Primary source: NASA Goddard Institute for Space Studies (GISS), GISTEMP v4\n"
            "Measure: Global annual Land-Ocean Temperature Index anomaly.\n"
            "Pinned dataset: Empirical World3 Dynamics, processed extract\n"
            "Data vintage: 2026-08-31\n",
            c[1], c[2]);
    }
    gtk_text_buffer_set_text (self->provenance, text->str, -1);
    g_strfreev (c);
    g_string_free (text, TRUE);
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
static gboolean atm_chart_view_new_internal (const AtmChartSpec *spec,
                                      const AtmChartSpecV4 *spec_v4,
                                      AtmChartView **out,
                                      GError **error)
{
    if (!out || *out) return reject (error, "Invalid chart widget output.");
    AtmChartRender *initial = NULL;
    AtmChartRenderV4 *initial_v4 = NULL;
    if (spec_v4 != NULL) {
        if (!atm_chart_render_v4_new (spec_v4, 860, 500, 1, &initial_v4, error))
            return FALSE;
    } else if (!atm_chart_render_new (spec, 860, 500, 1, &initial, error)) {
        return FALSE;
    }
    AtmChartView *self = g_object_ref_sink (g_object_new (ATM_TYPE_CHART_VIEW, "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP, NULL));
    self->render = initial;
    self->render_v4 = initial_v4;
    self->requested_width = self->render_width = 860;
    self->requested_height = self->render_height = 500;
    self->requested_scale = self->render_scale = 1;
    self->data = g_strdup (view_data (self));
    self->rows = gtk_string_list_new (NULL);
    char **lines = g_strsplit (self->data, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        if (!lines[i][0] || lines[i][0] == '#' || g_str_has_prefix (lines[i], "series\t")) continue;
        const AtmChartSpec *spec_for_rows = view_source_spec (self);
        gboolean is_bnr_row = spec_for_rows != NULL &&
            atm_chart_spec_version (spec_for_rows) == ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3;
        char **cells = g_strsplit (lines[i], "\t", -1);
        gboolean valid = g_strv_length (cells) == (is_bnr_row ? 10u : 8u);
        g_strfreev (cells);
        if (!valid) { g_strfreev (lines); g_object_unref (self); return reject (error, "Invalid exact Data view row."); }
        gtk_string_list_append (self->rows, lines[i]);
    }
    g_strfreev (lines);
    const AtmChartSpec *initial_spec = view_source_spec (self);
    gboolean is_bnr = atm_chart_spec_version (initial_spec) ==
        ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3;
    gboolean is_ei = atm_chart_spec_version (initial_spec) ==
        ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2;
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
    self->area = GTK_DRAWING_AREA (gtk_drawing_area_new ());
    /* The view is embedded at a GtkTextView child anchor. Reserve enough
     * natural width for the longest qualified renderer footer. At 432 logical
     * pixels EI meets the projection minimum but falls back because the footer
     * does not fit; 480 leaves 448px for its label and the 32px side margins. */
    gtk_drawing_area_set_content_width (self->area, 480);
    gtk_drawing_area_set_content_height (self->area, 348);
    gtk_widget_set_hexpand (GTK_WIDGET (self->area), TRUE); gtk_widget_set_vexpand (GTK_WIDGET (self->area), TRUE);
    gtk_widget_set_focusable (GTK_WIDGET (self->area), TRUE);
    accessible (GTK_WIDGET (self->area),
        is_bnr ? "BNR BLS C01 credit standards chart" :
            (is_ei ? "Energy Institute global total energy supply chart"
                   : "Global annual temperature anomaly chart"),
        is_bnr
            ? "Quarterly net percentage observations. 2025-Q2 is missing, not zero; source details are in Data."
            : (is_ei
                ? "Exact TES observations and source details are in Data."
                : "Exact observations and source details are in Data."));
    gtk_drawing_area_set_draw_func (self->area, draw, self, NULL);
    g_signal_connect (self->area, "resize", G_CALLBACK (resize), self);
    g_signal_connect (self->area, "notify::scale-factor", G_CALLBACK (scale_changed), self);
    gtk_box_append (GTK_BOX (chart), GTK_WIDGET (self->area));
    append_view_source_attribution (GTK_BOX (chart), self);
    gtk_stack_add_titled (self->stack, chart, "chart", "Chart");
    GtkWidget *data = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    append_view_source_attribution (GTK_BOX (data), self);

    self->selection = gtk_single_selection_new (G_LIST_MODEL (g_object_ref (self->rows)));
    self->table = GTK_COLUMN_VIEW (gtk_column_view_new (GTK_SELECTION_MODEL (g_object_ref (self->selection))));
    gtk_column_view_set_show_row_separators (self->table, TRUE); gtk_widget_add_css_class (GTK_WIDGET (self->table), "data-table");
    accessible (GTK_WIDGET (self->table),
        is_bnr ? "Quarterly observations" : "Annual observations",
        is_bnr
            ? "Select a quarter with arrow keys to inspect its status and source details. The missing quarter has no numeric value."
            : "Select a year with arrow keys to inspect its source details. Source order is preserved.");
    const char *titles[] = {
        is_bnr ? "Quarter" : "Year",
        is_bnr ? "Status" : (is_ei ? "TES (EJ/year)" : "Anomaly (deg C)"),
        "Net %"
    };
    guint fields[] = {1, 2, 3};
    guint columns = is_bnr ? 3u : 2u;
    for (guint i = 0; i < columns; i++) {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
        g_signal_connect (factory, "setup", G_CALLBACK (cell_setup), NULL);
        g_signal_connect (factory, "bind", G_CALLBACK (cell_bind), GUINT_TO_POINTER (fields[i]));
        GtkColumnViewColumn *column = gtk_column_view_column_new (titles[i], factory);
        gtk_column_view_column_set_expand (column, i == 1 || (is_bnr && i == 2));
        gtk_column_view_append_column (self->table, column); g_object_unref (column);
    }
    self->data_scroll = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (self->data_scroll, GTK_WIDGET (self->table));
    gtk_scrolled_window_set_min_content_height (self->data_scroll, 180);
    gtk_widget_set_vexpand (GTK_WIDGET (self->data_scroll), TRUE);
    gtk_box_append (GTK_BOX (data), GTK_WIDGET (self->data_scroll));
    GtkWidget *detail = gtk_text_view_new (); gtk_text_view_set_editable (GTK_TEXT_VIEW (detail), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (detail), FALSE); gtk_text_view_set_monospace (GTK_TEXT_VIEW (detail), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (detail), GTK_WRAP_CHAR);
    accessible (detail, "Selected observation source details", "Observation value, source and relevant qualification notes. Read-only text.");
    self->provenance = g_object_ref (gtk_text_view_get_buffer (GTK_TEXT_VIEW (detail)));
    self->details_scroll = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (self->details_scroll, detail);
    gtk_scrolled_window_set_min_content_height (self->details_scroll, 140);
    GtkWidget *expander = gtk_expander_new ("Source details");
    gtk_expander_set_expanded (GTK_EXPANDER (expander), TRUE);
    gtk_expander_set_child (GTK_EXPANDER (expander), GTK_WIDGET (self->details_scroll));
    gtk_box_append (GTK_BOX (data), expander);
    gtk_stack_add_titled (self->stack, data, "data", "Data");
    gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->stack));
    g_signal_connect (self->selection, "notify::selected", G_CALLBACK (selection_changed), self);
    selection_changed (NULL, NULL, self);
    GtkEventController *keys = gtk_event_controller_key_new (); gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
    g_signal_connect_object (keys, "key-pressed", G_CALLBACK (key_pressed), self, 0);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);
    *out = self; return TRUE;
}

gboolean atm_chart_view_new (const AtmChartSpec *spec, AtmChartView **out, GError **error)
{
    if (spec == NULL)
        return reject (error, "A qualified source ChartSpec is required.");
    return atm_chart_view_new_internal (spec, NULL, out, error);
}

gboolean atm_chart_view_new_v4 (const AtmChartSpecV4 *spec, AtmChartView **out, GError **error)
{
    if (spec == NULL)
        return reject (error, "A catalog-bound ChartSpec v4 is required.");
    return atm_chart_view_new_internal (NULL, spec, out, error);
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
void atm_chart_view_test_area_size (AtmChartView *self, int *width, int *height)
{
    if (!self || !self->area) return;
    if (width) *width = gtk_widget_get_width (GTK_WIDGET (self->area));
    if (height) *height = gtk_widget_get_height (GTK_WIDGET (self->area));
}
gboolean atm_chart_view_test_is_fallback (AtmChartView *self)
{
    return view_is_fallback (self);
}
void atm_chart_view_test_set_canvas_minimum (
    AtmChartView *self, int width, int height
)
{
    if (self == NULL || self->area == NULL || width < 1 || height < 1)
        return;
    gtk_drawing_area_set_content_width (self->area, width);
    gtk_drawing_area_set_content_height (self->area, height);
    gtk_widget_set_vexpand (GTK_WIDGET (self->stack), height >= 348);
    if (height < 348) {
        gtk_scrolled_window_set_min_content_height (self->data_scroll, 1);
        gtk_scrolled_window_set_min_content_height (self->details_scroll, 1);
     } else {
        gtk_scrolled_window_set_min_content_height (self->data_scroll, 180);
        gtk_scrolled_window_set_min_content_height (self->details_scroll, 140);
     }
}
#endif
