#include "conversation_store.h"
#include "chart_history_reconstruct.h"
#include "chart_spec.h"
#include "gistemp_admission.h"
#include "verified_series.h"
#include <glib.h>
#include <glib/gstdio.h>

static char *
fixture_snapshot (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    return g_build_filename (root, "ewd", NULL);
}

static gboolean
build_reference_spec (
    const char *snapshot,
    AtmChartSpec **out_spec,
    AtmVerifiedSeries **out_series,
    GError **error
)
{
    const char *paths[] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[4] = { NULL, NULL, NULL, NULL };
    AtmGistempAdmission *admission = NULL;
    AtmVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    gboolean ok = FALSE;

    for (guint i = 0; i < 4; i++) {
        char *path = g_build_filename (snapshot, paths[i], NULL);
        char *contents = NULL;
        gsize length = 0;

        if (!g_file_get_contents (path, &contents, &length, error)) {
            g_free (path);
            goto cleanup;
        }

        sources[i] = g_bytes_new_take ((guint8 *) contents, length);
        g_free (path);
    }

    if (!atm_gistemp_admission_new (
            "LaurentiuStaicu/empirical-world3-dynamics",
            "d9e249339663015f6d1c05752338a955bf64ad0b",
            sources,
            &admission,
            error)) {
        goto cleanup;
    }

    if (!atm_verified_series_from_gistemp (admission, &series, error))
        goto cleanup;

    const AtmVerifiedSeries *inputs[1] = { series };
    if (!atm_chart_spec_new (
            ATM_CHART_LINE,
            inputs,
            1,
            &spec,
            error)) {
        goto cleanup;
    }

    *out_spec = spec;
    *out_series = series;
    spec = NULL;
    series = NULL;
    ok = TRUE;

cleanup:
    atm_chart_spec_free (spec);
    atm_verified_series_free (series);
    if (admission != NULL)
        atm_gistemp_admission_free (admission);
    for (guint i = 0; i < 4; i++)
        g_clear_pointer (&sources[i], g_bytes_unref);
    return ok;
}

static void
test_persist_close_reopen_reconstruct (void)
{
    char *snapshot = fixture_snapshot ();
    char *root = NULL;
    char *db_path = NULL;
    char *conversation_id = NULL;
    AtmConversationStore *store = NULL;
    AtmConversationStore *reopened = NULL;
    AtmConversationSnapshot *snapshot_data = NULL;
    AtmChartSpec *reference = NULL;
    AtmVerifiedSeries *reference_series = NULL;
    AtmChartSpec *reconstructed = NULL;
    GError *error = NULL;
    gint64 turn_no = -1;

    root = g_dir_make_tmp ("atm-pres09-chart-e2e-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (root);

    db_path = g_build_filename (root, "conversations.sqlite3", NULL);

    g_assert_true (atm_conversation_store_open (
        db_path, &store, &error));
    g_assert_no_error (error);

    AtmConversationRepositoryInput repository = {
        "ewd",
        "2026-08-31",
        "d9e249339663015f6d1c05752338a955bf64ad0b"
    };

    g_assert_true (atm_conversation_store_create_conversation (
        store,
        "PRES-09 chart E2E",
        100,
        "test-model",
        "test-digest",
        7,
        &repository,
        1,
        &conversation_id,
        &error));
    g_assert_no_error (error);
    g_assert_nonnull (conversation_id);

    g_assert_true (atm_conversation_store_commit_turn (
        store,
        conversation_id,
        "Show the annual GISTEMP series.",
        "Qualified chart response.",
        "Qualified chart response.",
        TRUE,
        101,
        NULL,
        0,
        &turn_no,
        &error));
    g_assert_no_error (error);
    g_assert_cmpint (turn_no, ==, 0);

    g_assert_true (build_reference_spec (
        snapshot,
        &reference,
        &reference_series,
        &error));
    g_assert_no_error (error);

    AtmConversationChartSeriesInput series_input = {
        "atm-series/gistemp-complete-annual/1",
        "atm-gistemp-pinned-admission/1",
        atm_verified_series_scientific_id (reference_series),
        atm_verified_series_qualified_id (reference_series),
        "ewd",
        "2026-08-31",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "science/data/processed/nasa_gistemp_global_2026.csv"
    };

    AtmConversationChartInput chart_input = {
        1,
        "atm-chart-spec/1",
        atm_chart_spec_id (reference),
        "line",
        "atm-chart-reconstruct/gistemp-complete-annual/1",
        &series_input,
        1
    };

    g_assert_true (atm_conversation_store_attach_chart (
        store,
        conversation_id,
        turn_no,
        &chart_input,
        &error));
    g_assert_no_error (error);

    g_assert_true (atm_conversation_store_set_archived (
        store,
        conversation_id,
        TRUE,
        102,
        &error));
    g_assert_no_error (error);

    atm_conversation_store_close (store);
    store = NULL;

    g_assert_true (atm_conversation_store_open (
        db_path, &reopened, &error));
    g_assert_no_error (error);

    g_assert_true (atm_conversation_store_load_snapshot (
        reopened,
        conversation_id,
        &snapshot_data,
        &error));
    g_assert_no_error (error);

    g_assert_true (atm_conversation_snapshot_archived (snapshot_data));
    g_assert_cmpuint (
        atm_conversation_snapshot_message_count (snapshot_data),
        ==,
        2);
    g_assert_cmpuint (
        atm_conversation_snapshot_message_chart_count_at (
            snapshot_data, 1),
        ==,
        1);

    g_assert_cmpstr (
        atm_conversation_snapshot_chart_schema_at (
            snapshot_data, 1, 0),
        ==,
        "atm-chart-spec/1");
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_spec_id_at (
            snapshot_data, 1, 0),
        ==,
        atm_chart_spec_id (reference));
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_kind_at (
            snapshot_data, 1, 0),
        ==,
        "line");

    g_assert_cmpuint (
        atm_conversation_snapshot_chart_series_count_at (
            snapshot_data, 1, 0),
        ==,
        1);
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_scientific_id_at (
            snapshot_data, 1, 0, 0),
        ==,
        atm_verified_series_scientific_id (reference_series));
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_qualified_id_at (
            snapshot_data, 1, 0, 0),
        ==,
        atm_verified_series_qualified_id (reference_series));
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_source_path_at (
            snapshot_data, 1, 0, 0),
        ==,
        "science/data/processed/nasa_gistemp_global_2026.csv");

    g_assert_true (atm_chart_history_reconstruct_gistemp (
        snapshot,
        atm_conversation_snapshot_chart_series_repository_id_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_series_repository_version_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_series_snapshot_sha_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_schema_at (
            snapshot_data, 1, 0),
        atm_conversation_snapshot_chart_spec_id_at (
            snapshot_data, 1, 0),
        atm_conversation_snapshot_chart_kind_at (
            snapshot_data, 1, 0),
        atm_conversation_snapshot_chart_reconstruction_profile_at (
            snapshot_data, 1, 0),
        atm_conversation_snapshot_chart_series_profile_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_series_admission_profile_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_series_scientific_id_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_series_qualified_id_at (
            snapshot_data, 1, 0, 0),
        atm_conversation_snapshot_chart_series_source_path_at (
            snapshot_data, 1, 0, 0),
        &reconstructed,
        &error));

    g_assert_no_error (error);
    g_assert_nonnull (reconstructed);
    g_assert_cmpstr (
        atm_chart_spec_id (reconstructed),
        ==,
        atm_chart_spec_id (reference));

    atm_chart_spec_free (reconstructed);
    atm_conversation_snapshot_free (snapshot_data);
    atm_conversation_store_close (reopened);
    atm_verified_series_free (reference_series);
    atm_chart_spec_free (reference);
    g_free (conversation_id);
    g_free (db_path);
    g_rmdir (root);
    g_free (root);
    g_free (snapshot);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func (
        "/pres09/chart-history/persist-close-reopen-reconstruct",
        test_persist_close_reopen_reconstruct);
    return g_test_run ();
}
