#include "chart_view.h"
#include "chart_fixture.h"
#include "energy_institute_admission.h"
#include "energy_institute_verified_series.h"
#include "bnr_bls_verified_series.h"
#include "bnr_bls_series_contract.h"
#include <string.h>

static void settle (void)
{
    gint64 deadline = g_get_monotonic_time () + 400000;
    do { while (g_main_context_iteration (NULL, FALSE)); g_usleep (1000); }
    while (g_get_monotonic_time () < deadline);
}
static GtkExpander *find_expander (GtkWidget *widget)
{
    if (GTK_IS_EXPANDER (widget))
        return GTK_EXPANDER (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child)) {
        GtkExpander *found = find_expander (child);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static GtkLinkButton *find_link_button_with_uri (GtkWidget *widget, const char *uri)
{
    if (GTK_IS_LINK_BUTTON (widget) &&
        g_strcmp0 (gtk_link_button_get_uri (GTK_LINK_BUTTON (widget)), uri) == 0)
        return GTK_LINK_BUTTON (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child)) {
        GtkLinkButton *found = find_link_button_with_uri (child, uri);
        if (found != NULL)
            return found;
    }
    return NULL;
}
static GtkLabel *find_label_containing (GtkWidget *widget, const char *needle)
{
    if (GTK_IS_LABEL (widget) &&
        strstr (gtk_label_get_text (GTK_LABEL (widget)), needle) != NULL)
        return GTK_LABEL (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child)) {
        GtkLabel *found = find_label_containing (child, needle);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static GtkColumnView *find_column_view (GtkWidget *widget)
{
    if (GTK_IS_COLUMN_VIEW (widget))
        return GTK_COLUMN_VIEW (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child)) {
        GtkColumnView *found = find_column_view (child);
        if (found != NULL)
            return found;
    }
    return NULL;
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
static AtmChartSpec *build_gistemp_spec (void)
{
    return atm_chart_test_spec (ATM_CHART_LINE);
}

static AtmChartView *build_view (void)
{
    AtmChartSpec *spec = build_gistemp_spec ();
    AtmChartView *view = NULL;
    g_assert_true (atm_chart_view_new (spec, &view, NULL));
    atm_chart_spec_free (spec);
    return view;
}

static AtmChartSpec *build_ei_spec (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    const char *snapshot = "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    const char *paths[ATM_EI_SOURCE_COUNT] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    g_assert_nonnull (root);
    GBytes *sources[ATM_EI_SOURCE_COUNT] = { NULL, NULL, NULL, NULL };
    GError *error = NULL;
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, "ewd-current", paths[i], NULL);
        char *contents = NULL;
        gsize length = 0;
        g_assert_true (g_file_get_contents (path, &contents, &length, &error));
        g_assert_no_error (error);
        g_free (path);
        sources[i] = g_bytes_new_take (contents, length);
    }
    AtmEnergyInstituteAdmission *admission = NULL;
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    g_assert_true (atm_energy_institute_admission_new (
        "LaurentiuStaicu/empirical-world3-dynamics",
        snapshot, sources, &admission, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);
    g_assert_true (atm_energy_institute_verified_series_from_admission (
        admission, &series, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_energy_institute_v2 (
        ATM_CHART_LINE, series, &spec, &error));
    g_assert_no_error (error);
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
    return spec;
}

static AtmChartView *build_ei_view (void)
{
    AtmChartSpec *spec = build_ei_spec ();
    AtmChartView *view = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_view_new (spec, &view, &error));
    g_assert_no_error (error);
    atm_chart_spec_free (spec);
    return view;
}

static AtmChartSpec *build_bnr_spec (void)
{
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);
    const char *names[] = {
        "bnr_bls_realised_rounds.csv",
        "processed_data_inventory_registry.json",
        "bnr_bls_canonical_panel_promotion_review.json",
        "bnr_bls_cross_round_mapping_audit.json",
        "bnr_bls_missing_round_semantic_mapping_audit.json"
    };
    GBytes *sources[ATM_BNR_BLS_SOURCE_COUNT] = {NULL};
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, names[i], NULL);
        char *contents = NULL;
        gsize length = 0;
        GError *error = NULL;
        g_assert_true (g_file_get_contents (path, &contents, &length, &error));
        g_assert_no_error (error);
        g_free (path);
        sources[i] = g_bytes_new_take (contents, length);
    }
    AtmBnrBlsPanel *panel = NULL;
    AtmBnrBlsSra *sra = NULL;
    AtmBnrBlsSeriesContract *contract = NULL;
    AtmBnrBlsVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    GError *error = NULL;
    g_assert_true (atm_bnr_bls_panel_new (
        ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        ATM_BNR_BLS_SNAPSHOT_SHA, sources, &panel, &error));
    g_assert_no_error (error);
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++)
        g_bytes_unref (sources[i]);
    g_assert_true (atm_bnr_bls_sra_new (panel, &sra, &error));
    g_assert_no_error (error);
    g_assert_true (atm_bnr_bls_series_contract_from_sra (
        panel, sra, &contract, &error));
    g_assert_no_error (error);
    g_assert_true (atm_bnr_bls_verified_series_from_contract (
        contract, &series, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_new_bnr_bls_c01_v3 (
        ATM_CHART_LINE, series, &spec, &error));
    g_assert_no_error (error);
    atm_bnr_bls_verified_series_free (series);
    atm_bnr_bls_series_contract_free (contract);
    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
    return spec;
}

static AtmChartView *build_bnr_view (void)
{
    AtmChartSpec *spec = build_bnr_spec ();
    AtmChartView *view = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_view_new (spec, &view, &error));
    g_assert_no_error (error);
    atm_chart_spec_free (spec);
    return view;
}

static GtkWidget *chart_page_named (AtmChartView *view, const char *name);

static void
test_bnr_quarterly_data_and_missing_provenance (void)
{
    AtmChartView *view = build_bnr_view ();
    g_assert_false (g_object_is_floating (view));
    g_assert_cmpuint (atm_chart_view_row_count (view), ==, 12u);
    GtkWidget *chart_page = chart_page_named (view, "chart");
    GtkWidget *data_page = chart_page_named (view, "data");
    GtkExpander *source_details = find_expander (GTK_WIDGET (view));
    g_assert_nonnull (source_details);
    g_assert_cmpstr (gtk_expander_get_label (source_details), ==, "Source details");
    g_assert_true (gtk_expander_get_expanded (source_details));
    GtkColumnView *data_table = find_column_view (GTK_WIDGET (view));
    g_assert_nonnull (data_table);
    g_assert_cmpuint (g_list_model_get_n_items (gtk_column_view_get_columns (data_table)), ==, 3u);
    GtkLinkButton *primary_link = find_link_button_with_uri (
        chart_page, "https://www.bnr.ro/en/12067-bank-lending-survey");
    g_assert_nonnull (primary_link);
    GtkLinkButton *source_link = find_link_button_with_uri (
        chart_page,
        "https://github.com/LaurentiuStaicu/romanian-monetary-dynamics/blob/"
        "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170/data/processed/bnr_bls_realised_rounds.csv");
    g_assert_nonnull (source_link);
    GtkLabel *source_summary = find_label_containing (
        chart_page, "Primary source: National Bank of Romania");
    g_assert_nonnull (source_summary);
    g_assert_nonnull (strstr (gtk_label_get_text (source_summary),
        "2025-Q2 is missing, not zero"));
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    const char *data = atm_chart_view_data (view);
    g_assert_nonnull (data);
    g_assert_nonnull (strstr (data, "# source_snapshot\te43eb4e899f123d3b1aa0f6e20b7c8ed35434170"));
    g_assert_nonnull (strstr (data, "0\t2022-Q4\tNUMERIC\t11.89"));
    g_assert_nonnull (strstr (data, "0\t2025-Q2\tMISSING\t\t\t0\tnone\t"));
    g_assert_nonnull (strstr (data, "source_round_not_retained"));
    g_assert_nonnull (strstr (data, "0\t2025-Q3\tNUMERIC\t9.46"));

    g_assert_true (atm_chart_view_select_row (view, 0u));
    char *provenance = atm_chart_view_provenance (view);
    g_assert_nonnull (provenance);
    g_assert_true (g_str_has_prefix (provenance, "Quarter: 2022-Q4\nCredit standards net percentage: 11.89%"));
    g_assert_nonnull (strstr (provenance, "Primary source: National Bank of Romania, Bank Lending Survey (BLS)"));
    g_assert_nonnull (strstr (provenance, "Pinned dataset: Romanian Monetary Dynamics"));
    g_assert_null (strstr (provenance, "Physical source row:"));
    g_assert_null (strstr (provenance, "Snapshot:"));
    g_assert_null (strstr (provenance, "Canonical decimal:"));
    g_assert_null (strstr (provenance, "Support identities:"));
    g_free (provenance);

    g_assert_true (atm_chart_view_select_row (view, 10u));
    provenance = atm_chart_view_provenance (view);
    g_assert_nonnull (provenance);
    g_assert_true (g_str_has_prefix (provenance, "Quarter: 2025-Q2\nStatus: Missing"));
    g_assert_nonnull (strstr (provenance, "Reason: The source round was not retained."));
    g_assert_nonnull (strstr (provenance, "No numeric value is available; this is not zero."));
    g_assert_null (strstr (provenance, "source_round_not_retained"));
    g_assert_null (strstr (provenance, "Physical source row:"));
    g_assert_null (strstr (provenance, "Exact net percentage: 0%"));
    g_assert_null (strstr (provenance, "Snapshot:"));
    g_free (provenance);

    g_assert_true (atm_chart_view_select_row (view, 11u));
    provenance = atm_chart_view_provenance (view);
    g_assert_true (g_str_has_prefix (provenance, "Quarter: 2025-Q3\nCredit standards net percentage: 9.46%"));
    g_assert_null (strstr (provenance, "Physical source row:"));
    g_free (provenance);

    atm_chart_view_show_data (view);
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "data");
    g_assert_nonnull (find_link_button_with_uri (
        data_page, "https://www.bnr.ro/en/12067-bank-lending-survey"));
    g_assert_nonnull (find_link_button_with_uri (
        data_page,
        "https://github.com/LaurentiuStaicu/romanian-monetary-dynamics/blob/"
        "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170/data/processed/bnr_bls_realised_rounds.csv"));
    atm_chart_view_show_chart (view);
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    g_assert_cmpstr (atm_chart_view_data (view), ==, data);
    g_object_unref (view);
}

static void test_ei_data_lifetime (void)
{
    AtmChartView *view = build_ei_view ();
    g_assert_false (g_object_is_floating (view));
    g_assert_cmpuint (atm_chart_view_row_count (view), ==, 61u);
    GtkWidget *chart_page = chart_page_named (view, "chart");
    GtkWidget *data_page = chart_page_named (view, "data");
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    GtkColumnView *data_table = find_column_view (GTK_WIDGET (view));
    g_assert_nonnull (data_table);
    g_assert_cmpuint (g_list_model_get_n_items (gtk_column_view_get_columns (data_table)), ==, 2u);
    GtkLinkButton *primary_link = find_link_button_with_uri (
        chart_page,
        "https://www.energyinst.org/statistical-review/resources-and-data-downloads");
    g_assert_nonnull (primary_link);
    GtkLinkButton *source_link = find_link_button_with_uri (
        chart_page,
        "https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/"
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb/"
        "science/data/processed/energy_institute_global_2026.csv");
    g_assert_nonnull (source_link);
    GtkLabel *source_summary = find_label_containing (
        chart_page, "Primary source: Energy Institute, Statistical Review of World Energy 2026");
    g_assert_nonnull (source_summary);
    g_assert_nonnull (strstr (gtk_label_get_text (source_summary),
        "gross supply, not net energy or EROI"));
    const char *data = atm_chart_view_data (view);
    g_assert_nonnull (data);
    g_assert_nonnull (strstr (data, "# unit\texajoules/year"));
    g_assert_nonnull (strstr (data, "# source_snapshot\tdc710d8e604c0fff713a3acfdb18ab47a1a902bb"));
    char *saved_data = g_strdup (data);

    g_assert_true (atm_chart_view_select_row (view, 0));
    char *provenance = atm_chart_view_provenance (view);
    g_assert_nonnull (provenance);
    g_assert_true (g_str_has_prefix (provenance, "Year: 1965\nGlobal total primary energy supply: "));
    g_assert_nonnull (strstr (provenance, "Method: Physical Energy Content"));
    g_assert_nonnull (strstr (provenance, "Primary source: Energy Institute, Statistical Review of World Energy 2026"));
    g_assert_null (strstr (provenance, "Source row:"));
    g_assert_null (strstr (provenance, "Snapshot:"));
    g_assert_null (strstr (provenance, "Canonical decimal:"));
    g_free (provenance);

    g_assert_true (atm_chart_view_select_row (view, 60));
    provenance = atm_chart_view_provenance (view);
    g_assert_true (g_str_has_prefix (provenance, "Year: 2025\nGlobal total primary energy supply: "));
    g_assert_null (strstr (provenance, "Source row:"));
    g_free (provenance);

    atm_chart_view_show_data (view);
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "data");
    g_assert_nonnull (find_link_button_with_uri (
        data_page,
        "https://www.energyinst.org/statistical-review/resources-and-data-downloads"));
    g_assert_nonnull (find_link_button_with_uri (
        data_page,
        "https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/"
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb/"
        "science/data/processed/energy_institute_global_2026.csv"));
    atm_chart_view_show_chart (view);
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    g_assert_cmpstr (atm_chart_view_data (view), ==, saved_data);
    g_free (saved_data);
    g_object_unref (view);
}

static void test_data_lifetime (void)
{
    AtmChartView *view = build_view ();
    g_assert_false (g_object_is_floating (view));
    g_assert_cmpuint (atm_chart_view_row_count (view), ==, 146);
    GtkWidget *chart_page = chart_page_named (view, "chart");
    GtkWidget *data_page = chart_page_named (view, "data");
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "chart");
    GtkLinkButton *primary_link = find_link_button_with_uri (
        chart_page, "https://data.giss.nasa.gov/gistemp/data_v4.html");
    g_assert_nonnull (primary_link);
    GtkLinkButton *pinned_link = find_link_button_with_uri (
        chart_page,
        "https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/"
        "d9e249339663015f6d1c05752338a955bf64ad0b/"
        "science/data/processed/nasa_gistemp_global_2026.csv");
    g_assert_nonnull (pinned_link);
    GtkLabel *source_summary = find_label_containing (
        chart_page, "Primary source: NASA Goddard Institute for Space Studies (GISS), GISTEMP v4");
    g_assert_nonnull (source_summary);
    g_assert_nonnull (strstr (gtk_label_get_text (source_summary),
        "1951-1980 mean"));
    g_assert_cmpint (gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (view)), ==, GTK_ACCESSIBLE_ROLE_GROUP);
    char *data = g_strdup (atm_chart_view_data (view));
    g_assert_nonnull (strstr (data, "1880\t-0.1700\t-17\t-2\t2\t"));
    for (guint i = 0; i < 146; i++) {
        g_assert_true (atm_chart_view_select_row (view, i));
        char *text = atm_chart_view_provenance (view), *year = g_strdup_printf ("Year: %u\n",1880+i);
        g_assert_true (g_str_has_prefix (text,year));
        g_assert_nonnull (strstr (text, "Primary source: NASA Goddard Institute for Space Studies (GISS), GISTEMP v4"));
        g_assert_nonnull (strstr (text, "Baseline: 1951-1980 mean"));
        g_assert_null (strstr (text, "Support identities:"));
        g_assert_null (strstr (text, "Snapshot:"));
        g_assert_null (strstr (text, "Canonical decimal:"));
        g_assert_null (strstr (text, "Source row:"));
        g_free (year); g_free (text);
    }
    g_assert_false (atm_chart_view_select_row (view, 146));
    g_assert_cmpuint (atm_chart_view_selected_row (view), ==, 145);
    atm_chart_view_show_data (view); g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "data");
    g_assert_nonnull (find_link_button_with_uri (
        data_page, "https://data.giss.nasa.gov/gistemp/data_v4.html"));
    g_assert_nonnull (find_link_button_with_uri (
        data_page,
        "https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/"
        "d9e249339663015f6d1c05752338a955bf64ad0b/"
        "science/data/processed/nasa_gistemp_global_2026.csv"));
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

static GtkWidget *find_drawing_area (GtkWidget *widget)
{
    if (GTK_IS_DRAWING_AREA (widget))
        return widget;

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child)) {
        GtkWidget *found = find_drawing_area (child);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static GtkWidget *chart_page_named (AtmChartView *view, const char *name)
{
    GtkWidget *stack_widget = gtk_widget_get_last_child (GTK_WIDGET (view));
    g_assert_true (GTK_IS_STACK (stack_widget));
    GtkWidget *page = gtk_stack_get_child_by_name (
        GTK_STACK (stack_widget), name);
    g_assert_nonnull (page);
    return page;
}

static GtkWidget *chart_drawing_area (AtmChartView *view)
{
    GtkWidget *area = find_drawing_area (chart_page_named (view, "chart"));
    g_assert_true (GTK_IS_DRAWING_AREA (area));
    return area;
}

static void test_transcript_anchor_minimum_viewport (void)
{
    AtmChartView *view = build_ei_view ();
    GtkWidget *transcript = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (transcript), GTK_WRAP_WORD_CHAR);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (transcript));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter (buffer, &end);
    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor (buffer, &end);
    gtk_text_view_add_child_at_anchor (GTK_TEXT_VIEW (transcript), GTK_WIDGET (view), anchor);

    GtkWidget *scroller = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (
        GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), transcript);
    GtkWindow *window = GTK_WINDOW (gtk_window_new ());
    gtk_window_set_title (window, "AtM chart transcript-anchor regression");
    gtk_window_set_default_size (window, 1000, 760);
    gtk_window_set_child (window, scroller);
    gtk_window_present (window);
    settle ();

    GtkWidget *stack_widget = gtk_widget_get_last_child (GTK_WIDGET (view));
    g_assert_true (GTK_IS_STACK (stack_widget));
    g_assert_true (gtk_stack_get_vhomogeneous (GTK_STACK (stack_widget)));

    GtkWidget *area = chart_drawing_area (view);
    g_assert_cmpint (gtk_widget_get_width (area), >=, minimum_width);
    g_assert_cmpint (gtk_widget_get_height (area), >=, 370);

    int render_width = 0, render_height = 0, render_scale = 0;
    atm_chart_view_test_render_size (view, &render_width, &render_height, &render_scale);
    g_assert_cmpint (render_width, >=, 480);
    g_assert_cmpint (render_height, >=, 370);
    g_assert_cmpint (render_scale, >=, 1);

    char *data_before = g_strdup (atm_chart_view_data (view));
    int initial_window_height = gtk_widget_get_height (GTK_WIDGET (window));
    gtk_window_set_default_size (window, 300, 420);
    settle ();
    g_assert_cmpint (
        gtk_widget_get_height (GTK_WIDGET (window)), <, initial_window_height);
    g_assert_cmpint (gtk_widget_get_height (area), >=, 370);
    atm_chart_view_test_render_size (view, &render_width, &render_height, &render_scale);
    g_assert_cmpint (render_height, ==, gtk_widget_get_height (area));
    g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);
    g_free (data_before);
    gtk_window_destroy (window);
    settle ();
    g_object_unref (view);
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
typedef AtmChartSpec *(*ChartSpecBuilder) (void);

static void assert_transcript_anchor_resize (AtmChartView *view,
                                             int minimum_width)
{
    GtkWidget *transcript = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (
        GTK_TEXT_VIEW (transcript), GTK_WRAP_WORD_CHAR);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (
        GTK_TEXT_VIEW (transcript));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter (buffer, &end);
    GtkTextChildAnchor *anchor =
        gtk_text_buffer_create_child_anchor (buffer, &end);
    gtk_text_view_add_child_at_anchor (
        GTK_TEXT_VIEW (transcript), GTK_WIDGET (view), anchor);

    GtkWidget *scroller = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (
        GTK_SCROLLED_WINDOW (scroller),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (
        GTK_SCROLLED_WINDOW (scroller), transcript);

    GtkWindow *window = GTK_WINDOW (gtk_window_new ());
    gtk_window_set_default_size (window, 1000, 760);
    gtk_window_set_child (window, scroller);
    gtk_window_present (window);
    settle ();

    GtkWidget *area = chart_drawing_area (view);
    g_assert_cmpint (gtk_widget_get_width (area), >=, minimum_width);
    g_assert_cmpint (gtk_widget_get_height (area), >=, 370);
    atm_chart_view_test_render_size (view, &width, &height, &scale);
    g_assert_cmpint (width, ==, gtk_widget_get_width (area));
    g_assert_cmpint (height, ==, gtk_widget_get_height (area));
    g_assert_cmpint (scale, >=, 1);
    g_assert_false (atm_chart_view_test_is_fallback (view));

    char *data_before = g_strdup (atm_chart_view_data (view));
    int initial_window_width = gtk_widget_get_width (GTK_WIDGET (window));
    int initial_window_height = gtk_widget_get_height (GTK_WIDGET (window));

    gtk_window_set_default_size (window, 300, 420);
    settle ();
    g_assert_cmpint (
        gtk_widget_get_width (GTK_WIDGET (window)), <, initial_window_width);
    g_assert_cmpint (
        gtk_widget_get_height (GTK_WIDGET (window)), <, initial_window_height);
    g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);

    int width = 0, height = 0, scale = 0;
    atm_chart_view_test_render_size (view, &width, &height, &scale);
    g_assert_cmpint (width, ==, gtk_widget_get_width (area));
    g_assert_cmpint (height, ==, gtk_widget_get_height (area));
    g_assert_cmpint (scale, >=, 1);

    gtk_window_set_default_size (window, 1000, 760);
    settle ();
    g_assert_false (atm_chart_view_test_is_fallback (view));
    g_assert_cmpint (gtk_widget_get_width (area), >=, 448);
    g_assert_cmpint (gtk_widget_get_height (area), >=, 370);
    atm_chart_view_test_render_size (view, &width, &height, &scale);
    g_assert_cmpint (width, ==, gtk_widget_get_width (area));
    g_assert_cmpint (height, ==, gtk_widget_get_height (area));
    g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);

    g_free (data_before);
    gtk_window_destroy (window);
    settle ();
}

static void assert_v4_catalog_view (const char *series_id,
                                    ChartSpecBuilder build_source_spec,
                                    const char *expected_first_detail,
                                    gboolean exercise_resize)
{
    const AtmChartCatalogEntry *entry = atm_chart_catalog_find_id (series_id);
    g_assert_nonnull (entry);
    AtmChartSpec *source_spec = build_source_spec ();
    g_assert_nonnull (source_spec);

    AtmChartView *legacy_view = NULL;
    AtmChartSpecV4 *spec_v4 = NULL;
    AtmChartView *view = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_view_new (source_spec, &legacy_view, &error));
    g_assert_no_error (error);
    g_assert_true (atm_chart_spec_v4_new (entry, source_spec, &spec_v4, &error));
    g_assert_no_error (error);
    atm_chart_spec_free (source_spec);

    /* The GTK adapter must reject a v4 spec whose nested identity was altered. */
    char *mutable_id = (char *) atm_chart_spec_v4_id (spec_v4);
    g_assert_nonnull (mutable_id);
    char original = mutable_id[0];
    mutable_id[0] = original == 'a' ? 'b' : 'a';
    AtmChartView *invalid_view = NULL;
    g_assert_false (atm_chart_view_new_v4 (spec_v4, &invalid_view, &error));
    g_assert_null (invalid_view);
    g_assert_nonnull (error);
    g_clear_error (&error);
    mutable_id[0] = original;

    g_assert_true (atm_chart_view_new_v4 (spec_v4, &view, &error));
    g_assert_no_error (error);
    AtmChartView *occupied = view;
    g_assert_false (atm_chart_view_new_v4 (spec_v4, &occupied, &error));
    g_assert_true (occupied == view);
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_assert_cmpuint (atm_chart_view_row_count (view), ==, entry->coordinate_count);
    g_assert_cmpstr (atm_chart_view_data (view), ==, atm_chart_view_data (legacy_view));

    GtkWidget *chart_page = chart_page_named (view, "chart");
    GtkWidget *data_page = chart_page_named (view, "data");
    GtkLabel *summary = find_label_containing (
        chart_page, entry->publisher_name != NULL ? entry->publisher_name : "Primary source:");
    g_assert_nonnull (summary);
    const char *summary_text = gtk_label_get_text (summary);
    g_assert_nonnull (strstr (summary_text, "Primary source:"));
    const char *human_description = entry->description != NULL
        ? entry->description
        : (entry->display_name != NULL ? entry->display_name
                                       : "Qualified source series.");
    g_assert_nonnull (strstr (summary_text, human_description));
    g_assert_nonnull (strstr (summary_text,
        "does not independently reproduce the publisher's original processing pipeline."));
    /*
     * The ordinary source paragraph is intentionally concise. Scientific
     * observation semantics remain in catalog-qualified detail/provenance,
     * while the pinned artifact is available through its explicit link below.
     */
    g_assert_null (strstr (summary_text, entry->pinned_artifact_description));
    g_assert_null (strstr (summary_text, entry->reconstruction_scope));
    g_assert_nonnull (find_link_button_with_uri (chart_page, entry->publisher_url));
    g_assert_nonnull (find_link_button_with_uri (data_page, entry->publisher_url));

    const AtmChartSpec *owned_source =
        atm_chart_spec_v4_source_spec (spec_v4);
    const char *snapshot = atm_chart_spec_series_snapshot_sha (owned_source, 0);
    g_assert_nonnull (snapshot);
    char *pinned_url = g_strdup_printf ("https://github.com/%s/blob/%s/%s",
        entry->repository_full_name, snapshot, entry->source_path);
    g_assert_nonnull (find_link_button_with_uri (chart_page, pinned_url));
    g_assert_nonnull (find_link_button_with_uri (data_page, pinned_url));
    g_free (pinned_url);

    g_assert_true (atm_chart_view_select_row (view, 0));
    char *detail = atm_chart_view_provenance (view);
    g_assert_nonnull (detail);
    g_assert_true (g_str_has_prefix (detail, expected_first_detail));
    g_assert_nonnull (strstr (detail, "Primary source:"));
    g_free (detail);

    /*
     * Retain actual GTK snapshots from the catalog-bound v4 view, not from
     * the legacy compatibility constructor. These images are the visual-review
     * evidence for the new Cairo painter; BNR's chart image must show the open
     * 2025-Q2 gap while its Data image retains the explicit MISSING row.
     */
    const char *preview_slug =
        g_strcmp0 (series_id, "climate.gistemp.global-temperature-anomaly") == 0
            ? "gistemp"
            : (g_strcmp0 (series_id,
                "energy.energy-institute.global-total-energy-supply") == 0
                ? "energy-institute"
                : "bnr-bls-c01");
    GtkWindow *v4_preview_window = GTK_WINDOW (gtk_window_new ());
    gtk_window_set_title (v4_preview_window, "AtM qualified ChartSpec v4 preview");
    gtk_window_set_default_size (v4_preview_window, 1000, 760);
    atm_chart_view_show_chart (view);
    gtk_window_set_child (v4_preview_window, GTK_WIDGET (view));
    gtk_window_present (v4_preview_window);
    settle ();
    char *v4_chart_filename =
        g_strdup_printf ("atm-chart-v4-%s-chart.png", preview_slug);
    screenshot (v4_preview_window, v4_chart_filename);
    g_free (v4_chart_filename);
    atm_chart_view_show_data (view);
    settle ();
    char *v4_data_filename =
        g_strdup_printf ("atm-chart-v4-%s-data.png", preview_slug);
    screenshot (v4_preview_window, v4_data_filename);
    g_free (v4_data_filename);
    gtk_window_destroy (v4_preview_window);
    settle ();

    if (exercise_resize) {
        atm_chart_view_show_chart (view);
        int minimum_width =
            g_strcmp0 (series_id,
                "banking.bnr.bls-c01.credit-standards-net-percentage") == 0
                ? 800 : 480;
        assert_transcript_anchor_resize (view, minimum_width);
    }

    if (exercise_resize) {
        char *data_before = g_strdup (atm_chart_view_data (view));
        int width = 0, height = 0, scale = 0;
        atm_chart_view_test_request_resize (view, 740, 420, 1);
        settle ();
        atm_chart_view_test_render_size (view, &width, &height, &scale);
        g_assert_cmpint (width, ==, 740);
        g_assert_cmpint (height, ==, 420);
        g_assert_cmpint (scale, ==, 1);
        g_assert_false (atm_chart_view_test_is_fallback (view));
        g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);

        /* Minimum valid viewport uses the source renderer's existing fallback,
         * while keeping the exact catalog-qualified Data page and rows. */
        atm_chart_view_test_request_resize (view, 240, 140, 1);
        settle ();
        atm_chart_view_test_render_size (view, &width, &height, &scale);
        g_assert_cmpint (width, ==, 240);
        g_assert_cmpint (height, ==, 140);
        g_assert_true (atm_chart_view_test_is_fallback (view));
        g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);
        g_assert_cmpuint (atm_chart_view_row_count (view), ==, entry->coordinate_count);

        /* Invalid resize must leave the last fallback render and Data intact. */
        atm_chart_view_test_request_resize (view, 100, 100, 1);
        settle ();
        atm_chart_view_test_render_size (view, &width, &height, &scale);
        g_assert_cmpint (width, ==, 240);
        g_assert_cmpint (height, ==, 140);
        g_assert_true (atm_chart_view_test_is_fallback (view));
        g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);

        /* Returning to a valid viewport must restore the rendered chart. */
        atm_chart_view_test_request_resize (view, 740, 420, 1);
        settle ();
        atm_chart_view_test_render_size (view, &width, &height, &scale);
        g_assert_cmpint (width, ==, 740);
        g_assert_cmpint (height, ==, 420);
        g_assert_cmpint (scale, ==, 1);
        g_assert_false (atm_chart_view_test_is_fallback (view));
        g_assert_cmpstr (atm_chart_view_data (view), ==, data_before);
        g_assert_cmpuint (atm_chart_view_row_count (view), ==, entry->coordinate_count);
        g_free (data_before);
    }

    atm_chart_view_show_data (view);
    g_assert_cmpstr (atm_chart_view_visible_page (view), ==, "data");
    g_assert_cmpstr (atm_chart_view_data (view), ==, atm_chart_view_data (legacy_view));
    g_object_unref (view);
    g_object_unref (legacy_view);
    atm_chart_spec_v4_free (spec_v4);
}

static void test_v4_gtk_catalog_profiles (void)
{
    g_test_message ("CHART-06f GISTEMP v4 GTK Chart/Data adapter");
    assert_v4_catalog_view (
        "climate.gistemp.global-temperature-anomaly",
        build_gistemp_spec, "Year: 1880\n", TRUE);

    g_test_message ("CHART-06f Energy Institute v4 GTK Chart/Data adapter");
    assert_v4_catalog_view (
        "energy.energy-institute.global-total-energy-supply",
        build_ei_spec, "Year: 1965\n", TRUE);

    g_test_message ("CHART-06f BNR C01 v4 GTK Chart/Data adapter");
    assert_v4_catalog_view (
        "banking.bnr.bls-c01.credit-standards-net-percentage",
        build_bnr_spec, "Quarter: 2022-Q4\n", TRUE);
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
    g_test_add_func ("/chart-view/bnr-quarterly-data-missing",test_bnr_quarterly_data_and_missing_provenance);
    g_test_add_func ("/chart-view/ei-data-lifetime",test_ei_data_lifetime);
    g_test_add_func ("/chart-view/resize-coalescing",test_resize_coalescing);
    g_test_add_func ("/chart-view/transcript-anchor-minimum-viewport",test_transcript_anchor_minimum_viewport);
    g_test_add_func ("/chart-view/keyboard-window",test_keyboard_window);
    g_test_add_func ("/chart-view/v4-catalog-profiles",test_v4_gtk_catalog_profiles);
    g_test_add_func ("/chart-view/invalid",test_invalid);
    return g_test_run ();
}
