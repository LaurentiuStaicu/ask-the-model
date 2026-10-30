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
gboolean atm_chart_view_test_is_fallback (AtmChartView *self)
{
    return view_is_fallback (self);
}
#endif
