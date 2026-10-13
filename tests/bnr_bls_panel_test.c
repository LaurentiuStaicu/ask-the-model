#include "bnr_bls_panel.h"

#include <string.h>

static char *buffers[ATM_BNR_BLS_SOURCE_COUNT];
static gsize lengths[ATM_BNR_BLS_SOURCE_COUNT];
static GBytes *inputs[ATM_BNR_BLS_SOURCE_COUNT];

static const char *const fixture_names[ATM_BNR_BLS_SOURCE_COUNT] = {
    "bnr_bls_realised_rounds.csv",
    "processed_data_inventory_registry.json",
    "bnr_bls_canonical_panel_promotion_review.json",
    "bnr_bls_cross_round_mapping_audit.json",
    "bnr_bls_missing_round_semantic_mapping_audit.json"
};

static const char *const expected_quarters[] = {
    "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3", "2023-Q4",
    "2024-Q1", "2024-Q2", "2024-Q3", "2024-Q4", "2025-Q1", "2025-Q3"
};

static const char *const expected_values[] = {
    "11.89", "16.34", "15.54", "0.0", "0.0", "0.0",
    "0.0", "12.54", "12.08", "11.71", "9.46"
};

static const char *const expected_digests[ATM_BNR_BLS_SOURCE_COUNT] = {
    "c5dad2c9577431e9765bc02b166e8d5a2b4c4b4c8e761fbb5bbce50f4e6498ff",
    "9fd12a73499c51d6be200c3e6fba467fc4f0e8dbff33b946fcca8a4bec5b2bf9",
    "2df50e5c1ab64032c449b6c1fbdc3c93c3e5622e6e221fd716f22fcf337aa380",
    "818e16c4ec0598a7c1410044b74c30bc3744cdb5533dba606a9904bb9420b07a",
    "679ef4f2e401d27ebb9eadadc20a603b4ce57e9296c9fd9bc695a0999b515b8c"
};

static void
load_sources (void)
{
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, fixture_names[i], NULL);
        GError *error = NULL;
        g_assert_true (g_file_get_contents (
            path, &buffers[i], &lengths[i], &error));
        g_assert_no_error (error);
        g_assert_nonnull (buffers[i]);
        inputs[i] = g_bytes_new_static (buffers[i], lengths[i]);
        g_free (path);
    }
}

static void
free_sources (void)
{
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        g_clear_pointer (&inputs[i], g_bytes_unref);
        g_clear_pointer (&buffers[i], g_free);
        lengths[i] = 0;
    }
}

static void
reject_bundle (
    const char *repository,
    const char *snapshot,
    GBytes *const sources[ATM_BNR_BLS_SOURCE_COUNT],
    AtmBnrBlsPanelError expected_error
)
{
    AtmBnrBlsPanel *panel = NULL;
    GError *error = NULL;
    g_assert_false (atm_bnr_bls_panel_new (
        repository, snapshot, sources, &panel, &error));
    g_assert_error (error, ATM_BNR_BLS_PANEL_ERROR, expected_error);
    g_assert_null (panel);
    g_clear_error (&error);
}

static void
test_exact_source_binding_and_candidate (void)
{
    AtmBnrBlsPanel *panel = NULL;
    GError *error = NULL;
    g_assert_true (atm_bnr_bls_panel_new (
        ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        ATM_BNR_BLS_SNAPSHOT_SHA,
        inputs, &panel, &error));
    g_assert_no_error (error);
    g_assert_nonnull (panel);
    g_assert_cmpstr (atm_bnr_bls_panel_repository (panel), ==,
                     ATM_BNR_BLS_UPSTREAM_REPOSITORY);
    g_assert_cmpstr (atm_bnr_bls_panel_snapshot (panel), ==,
                     ATM_BNR_BLS_SNAPSHOT_SHA);
    g_assert_cmpuint (atm_bnr_bls_panel_observation_count (panel), ==, 11u);
    g_assert_cmpstr (atm_bnr_bls_panel_missing_quarter (panel), ==, "2025-Q2");
    g_assert_cmpstr (atm_bnr_bls_panel_missing_reason (panel), ==,
                     "source_round_not_retained");

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        g_assert_cmpstr (atm_bnr_bls_panel_source_sha256 (panel, i), ==,
                         expected_digests[i]);
        g_assert_nonnull (atm_bnr_bls_panel_source_path (panel, i));
    }
    g_assert_null (atm_bnr_bls_panel_source_sha256 (panel, G_MAXUINT));
    g_assert_null (atm_bnr_bls_panel_source_path (panel, G_MAXUINT));

    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        AtmBnrBlsObservation point = {0};
        g_assert_true (atm_bnr_bls_panel_observation (panel, i, &point));
        g_assert_cmpstr (point.quarter, ==, expected_quarters[i]);
        g_assert_cmpuint (point.source_row, ==, i + 2u);
        g_assert_cmpstr (point.source_decimal, ==, expected_values[i]);
        g_assert_nonnull (point.coefficient);
        g_assert_cmpstr (point.source_id, !=, "");
        g_assert_cmpstr (point.source_kind, !=, "");

        /* A blank ancillary workbook date does not erase the measured C01
         * value or change the quarter coordinate. */
        if (i == 2u || i == 6u) {
            g_assert_null (point.reference_date);
            g_assert_cmpstr (point.source_decimal, ==,
                             i == 2u ? "15.54" : "0.0");
        } else {
            g_assert_nonnull (point.reference_date);
            g_assert_cmpstr (point.reference_date, !=, "");
        }
    }

    AtmBnrBlsObservation out_of_range = {0};
    g_assert_false (atm_bnr_bls_panel_observation (
        panel, G_MAXUINT, &out_of_range));

    /* 2025-Q2 is an authorized absence, not an inserted CSV observation. */
    for (guint i = 0; i < atm_bnr_bls_panel_observation_count (panel); i++) {
        AtmBnrBlsObservation point = {0};
        g_assert_true (atm_bnr_bls_panel_observation (panel, i, &point));
        g_assert_cmpstr (point.quarter, !=, "2025-Q2");
    }

    g_assert_true (atm_bnr_bls_panel_validate (panel, &error));
    g_assert_no_error (error);
    atm_bnr_bls_panel_free (panel);
}

static void
test_repository_snapshot_and_incomplete_bundle_rejected (void)
{
    reject_bundle ("rmd", ATM_BNR_BLS_SNAPSHOT_SHA, inputs,
                   ATM_BNR_BLS_PANEL_ERROR_REPOSITORY);
    reject_bundle (ATM_BNR_BLS_UPSTREAM_REPOSITORY,
                   "e43eb4e899f123d3b1aa0f6e20b7c8ed35434171",
                   inputs, ATM_BNR_BLS_PANEL_ERROR_SNAPSHOT);

    GBytes *incomplete[ATM_BNR_BLS_SOURCE_COUNT];
    memcpy (incomplete, inputs, sizeof incomplete);
    incomplete[ATM_BNR_BLS_SOURCE_SEMANTIC_AUDIT] = NULL;
    reject_bundle (ATM_BNR_BLS_UPSTREAM_REPOSITORY,
                   ATM_BNR_BLS_SNAPSHOT_SHA,
                   incomplete, ATM_BNR_BLS_PANEL_ERROR_ARGUMENT);

    reject_bundle (NULL, ATM_BNR_BLS_SNAPSHOT_SHA, inputs,
                   ATM_BNR_BLS_PANEL_ERROR_ARGUMENT);
    reject_bundle (ATM_BNR_BLS_UPSTREAM_REPOSITORY,
                   ATM_BNR_BLS_SNAPSHOT_SHA, NULL,
                   ATM_BNR_BLS_PANEL_ERROR_ARGUMENT);
}

static void
test_every_bundle_component_is_digest_bound (void)
{
    for (guint changed_index = 0;
         changed_index < ATM_BNR_BLS_SOURCE_COUNT;
         changed_index++) {
        GBytes *modified[ATM_BNR_BLS_SOURCE_COUNT];
        memcpy (modified, inputs, sizeof modified);

        char *changed = g_memdup2 (
            buffers[changed_index], lengths[changed_index]);
        g_assert_cmpuint (lengths[changed_index], >, 0);
        changed[lengths[changed_index] / 2] ^= 1;
        modified[changed_index] = g_bytes_new_take (
            changed, lengths[changed_index]);

        reject_bundle (
            ATM_BNR_BLS_UPSTREAM_REPOSITORY,
            ATM_BNR_BLS_SNAPSHOT_SHA,
            modified,
            ATM_BNR_BLS_PANEL_ERROR_DIGEST);

        g_bytes_unref (modified[changed_index]);
    }

    GBytes *swapped[ATM_BNR_BLS_SOURCE_COUNT];
    memcpy (swapped, inputs, sizeof swapped);
    swapped[ATM_BNR_BLS_SOURCE_PANEL] =
        inputs[ATM_BNR_BLS_SOURCE_INVENTORY];
    swapped[ATM_BNR_BLS_SOURCE_INVENTORY] =
        inputs[ATM_BNR_BLS_SOURCE_PANEL];
    reject_bundle (
        ATM_BNR_BLS_UPSTREAM_REPOSITORY,
        ATM_BNR_BLS_SNAPSHOT_SHA,
        swapped,
        ATM_BNR_BLS_PANEL_ERROR_DIGEST);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    load_sources ();

    g_test_add_func (
        "/bnr-bls-panel/exact-source-binding",
        test_exact_source_binding_and_candidate);
    g_test_add_func (
        "/bnr-bls-panel/repository-snapshot-boundary",
        test_repository_snapshot_and_incomplete_bundle_rejected);
    g_test_add_func (
        "/bnr-bls-panel/all-five-sources-digest-bound",
        test_every_bundle_component_is_digest_bound);

    int result = g_test_run ();
    free_sources ();
    return result;
}
