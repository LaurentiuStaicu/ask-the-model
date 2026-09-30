#pragma once
#include "chart_render.h"
#include <gtk/gtk.h>
G_BEGIN_DECLS
#define ATM_TYPE_CHART_VIEW (atm_chart_view_get_type ())
G_DECLARE_FINAL_TYPE (AtmChartView, atm_chart_view, ATM, CHART_VIEW, GtkBox)
/* Dormant reusable GTK component. Revalidates and owns the supplied source.
 * Returns a full, non-floating reference; *out must be NULL. */
gboolean atm_chart_view_new (const AtmChartSpec *spec, AtmChartView **out, GError **error);
guint atm_chart_view_row_count (AtmChartView *view);
/* Borrowed exact TSV, independent of canvas size and selected page. */
const char *atm_chart_view_data (AtmChartView *view);
const char *atm_chart_view_visible_page (AtmChartView *view);
void atm_chart_view_show_chart (AtmChartView *view);
void atm_chart_view_show_data (AtmChartView *view);
gboolean atm_chart_view_select_row (AtmChartView *view, guint row);
guint atm_chart_view_selected_row (AtmChartView *view);
/* Caller owns returned plain-text provenance. */
char *atm_chart_view_provenance (AtmChartView *view);
#ifdef ATM_CHART_VIEW_TESTING
/* Test-only resize qualification hooks; excluded from normal compilation. */
void atm_chart_view_test_request_resize (AtmChartView *view, int width, int height, int scale);
guint atm_chart_view_test_resize_apply_count (AtmChartView *view);
void atm_chart_view_test_render_size (AtmChartView *view, int *width, int *height, int *scale);
#endif
G_END_DECLS
