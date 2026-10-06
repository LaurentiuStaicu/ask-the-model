#include "chart_history_reconstruct.h"
#include "verified_series.h"
#include <glib.h>

static char *
fixture_snapshot (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    return g_build_filename (
        root,
        "ewd",
        NULL
    );
}

static void
test_reconstruct_and_identity (void)
{
    char *snapshot = fixture_snapshot ();
    AtmChartSpec *spec = NULL;
    GError *error = NULL;

    g_assert_true (atm_chart_history_reconstruct_gistemp (
        snapshot,
        "LaurentiuStaicu/empirical-world3-dynamics",
        "2026-08-31",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "atm-chart-spec/1",
        /* First derive the qualified identity from the real source. */
        NULL,
        "LINE",
        "atm-chart-reconstruct/gistemp-complete-annual/1",
        "atm-series/gistemp-complete-annual/1",
        "atm-gistemp-pinned-admission/1",
        "x",
        "x",
        "science/data/processed/nasa_gistemp_global_2026.csv",
        &spec,
        &error
    ) == FALSE);
    g_clear_error (&error);
    g_assert_null (spec);

    /* Build the same qualified object through the native chart fixture path,
     * then feed its exact identities back through the History boundary. */
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[4] = { NULL, NULL, NULL, NULL };
    AtmGistempAdmission *admission = NULL;
    AtmVerifiedSeries *series = NULL;
    AtmChartSpec *reference = NULL;

    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (snapshot, paths[i], NULL);
        char *contents = NULL;
        gsize length = 0;
        g_assert_true (g_file_get_contents (path, &contents, &length, &error));
        g_assert_no_error (error);
        sources[i] = g_bytes_new_take ((guint8 *) contents, length);
        g_free (path);
    }

    g_assert_true (atm_gistemp_admission_new (
        "LaurentiuStaicu/empirical-world3-dynamics",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        sources,
        &admission,
        &error
    ));
    g_assert_no_error (error);

    g_assert_true (atm_verified_series_from_gistemp (
        admission, &series, &error
    ));
    g_assert_no_error (error);

    const AtmVerifiedSeries *inputs[1] = { series };
    g_assert_true (atm_chart_spec_new (
        ATM_CHART_LINE, inputs, 1, &reference, &error
    ));
    g_assert_no_error (error);

    g_assert_true (atm_chart_history_reconstruct_gistemp (
        snapshot,
        "LaurentiuStaicu/empirical-world3-dynamics",
        "2026-08-31",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "atm-chart-spec/1",
        atm_chart_spec_id (reference),
        "LINE",
        "atm-chart-reconstruct/gistemp-complete-annual/1",
        "atm-series/gistemp-complete-annual/1",
        "atm-gistemp-pinned-admission/1",
        atm_verified_series_scientific_id (series),
        atm_verified_series_qualified_id (series),
        paths[0],
        &spec,
        &error
    ));
    g_assert_no_error (error);
    g_assert_nonnull (spec);
    g_assert_cmpstr (
        atm_chart_spec_id (spec), ==, atm_chart_spec_id (reference)
    );

    atm_chart_spec_free (spec);
    atm_chart_spec_free (reference);
    atm_verified_series_free (series);
    atm_gistemp_admission_free (admission);
    for (guint i = 0; i < 4; i++)
        g_clear_pointer (&sources[i], g_bytes_unref);
    g_free (snapshot);
}

static void
test_reject_tampered_identity_and_snapshot (void)
{
    char *snapshot = fixture_snapshot ();
    GError *error = NULL;
    AtmChartSpec *out = NULL;

    g_assert_false (atm_chart_history_reconstruct_gistemp (
        snapshot,
        "LaurentiuStaicu/empirical-world3-dynamics",
        "2026-08-31",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "atm-chart-spec/1",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "LINE",
        "atm-chart-reconstruct/gistemp-complete-annual/1",
        "atm-series/gistemp-complete-annual/1",
        "atm-gistemp-pinned-admission/1",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "science/data/processed/nasa_gistemp_global_2026.csv",
        &out,
        &error
    ));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (out);
    g_clear_error (&error);

    g_assert_false (atm_chart_history_reconstruct_gistemp (
        "/does/not/exist",
        "LaurentiuStaicu/empirical-world3-dynamics",
        "2026-08-31",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "atm-chart-spec/1",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "LINE",
        "atm-chart-reconstruct/gistemp-complete-annual/1",
        "atm-series/gistemp-complete-annual/1",
        "atm-gistemp-pinned-admission/1",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "science/data/processed/nasa_gistemp_global_2026.csv",
        &out,
        &error
    ));
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_assert_null (out);
    g_clear_error (&error);
    g_free (snapshot);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func (
        "/chart/history-reconstruct/identity",
        test_reconstruct_and_identity
    );
    g_test_add_func (
        "/chart/history-reconstruct/fail-closed",
        test_reject_tampered_identity_and_snapshot
    );
    return g_test_run ();
}
