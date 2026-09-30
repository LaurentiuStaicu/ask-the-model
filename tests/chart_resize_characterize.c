#include "chart_render.h"
#include "chart_fixture.h"

#include <fontconfig/fontconfig.h>
#include <stdlib.h>

#define SAMPLES 9u
#define RESIZE_SAMPLES 3u
#define RESIZE_STEPS 24u

typedef enum {
    OP_SPEC_REBUILD,
    OP_PROJECTION,
    OP_RENDER_FALLBACK,
    OP_RENDER_NORMAL,
    OP_RENDER_SCALE2
} Operation;

static int
compare_i64 (const void *a, const void *b)
{
    gint64 x = *(const gint64 *) a;
    gint64 y = *(const gint64 *) b;
    return (x > y) - (x < y);
}

static gboolean
run_once (Operation op, AtmChartSpec *spec)
{
    GError *error = NULL;
    gboolean ok = FALSE;

    if (op == OP_SPEC_REBUILD) {
        AtmChartSpec *copy = NULL;
        ok = atm_chart_spec_rebuild (spec, &copy, &error);
        atm_chart_spec_free (copy);
    } else if (op == OP_PROJECTION) {
        AtmChartProjection *projection = NULL;
        ok = atm_chart_projection_new (spec, 748, 312, &projection, &error);
        atm_chart_projection_free (projection);
    } else {
        guint width = op == OP_RENDER_FALLBACK ? 240 : 860;
        guint height = op == OP_RENDER_FALLBACK ? 140 : 500;
        guint scale = op == OP_RENDER_SCALE2 ? 2 : 1;
        AtmChartRender *render = NULL;
        ok = atm_chart_render_new (spec, width, height, scale, &render, &error);
        atm_chart_render_free (render);
    }

    if (!ok) {
        g_printerr ("characterization operation failed: %s\n",
                    error ? error->message : "unknown error");
    }
    g_clear_error (&error);
    return ok;
}

static gboolean
measure (const char *name, Operation op, AtmChartSpec *spec)
{
    for (guint i = 0; i < 3; i++)
        if (!run_once (op, spec))
            return FALSE;

    gint64 samples[SAMPLES];
    for (guint i = 0; i < SAMPLES; i++) {
        gint64 start = g_get_monotonic_time ();
        if (!run_once (op, spec))
            return FALSE;
        samples[i] = g_get_monotonic_time () - start;
    }
    qsort (samples, SAMPLES, sizeof samples[0], compare_i64);

    guint p90_index = (SAMPLES * 9 + 9) / 10 - 1;
    g_print ("CHART04-RESIZE operation=%s samples=%u median_us=%" G_GINT64_FORMAT
             " p90_us=%" G_GINT64_FORMAT " min_us=%" G_GINT64_FORMAT
             " max_us=%" G_GINT64_FORMAT "\n",
             name, SAMPLES, samples[SAMPLES / 2], samples[p90_index],
             samples[0], samples[SAMPLES - 1]);
    return TRUE;
}

static gboolean
measure_resize_sequence (AtmChartSpec *spec)
{
    static const guint widths[RESIZE_STEPS] = {
        432, 480, 540, 620, 700, 780, 860, 940,
        1020, 1100, 1180, 1260, 1340, 1420, 1500, 1420,
        1340, 1260, 1180, 1100, 1020, 940, 860, 780
    };
    gint64 samples[RESIZE_SAMPLES];

    for (guint sample = 0; sample < RESIZE_SAMPLES; sample++) {
        gint64 start = g_get_monotonic_time ();
        for (guint i = 0; i < RESIZE_STEPS; i++) {
            guint height = 348 + (widths[i] - 432) / 2;
            AtmChartRender *render = NULL;
            GError *error = NULL;
            if (!atm_chart_render_new (spec, widths[i], height, 1, &render, &error)) {
                g_printerr ("resize sequence failed: %s\n",
                            error ? error->message : "unknown error");
                g_clear_error (&error);
                return FALSE;
            }
            atm_chart_render_free (render);
        }
        samples[sample] = g_get_monotonic_time () - start;
    }

    qsort (samples, RESIZE_SAMPLES, sizeof samples[0], compare_i64);
    guint p90_index = RESIZE_SAMPLES - 1;
    g_print ("CHART04-RESIZE operation=24-step-sequence samples=%u median_us=%" G_GINT64_FORMAT
             " p90_us=%" G_GINT64_FORMAT " per_step_median_us=%.1f\n",
             RESIZE_SAMPLES, samples[RESIZE_SAMPLES / 2], samples[p90_index],
             (double) samples[RESIZE_SAMPLES / 2] / RESIZE_STEPS);
    return TRUE;
}

int
main (void)
{
    AtmChartSpec *spec = atm_chart_test_spec (ATM_CHART_LINE);
    if (spec == NULL)
        return 2;

    gboolean ok =
        measure ("spec-rebuild", OP_SPEC_REBUILD, spec) &&
        measure ("projection-new", OP_PROJECTION, spec) &&
        measure ("render-fallback-240x140", OP_RENDER_FALLBACK, spec) &&
        measure ("render-normal-860x500", OP_RENDER_NORMAL, spec) &&
        measure ("render-scale2-860x500", OP_RENDER_SCALE2, spec) &&
        measure_resize_sequence (spec);

    atm_chart_spec_free (spec);
    cairo_debug_reset_static_data ();
    FcFini ();
    return ok ? 0 : 1;
}
