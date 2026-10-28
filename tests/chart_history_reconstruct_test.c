#include "chart_history_reconstruct.h"
#include "chart_spec_v4.h"
#include "verified_series.h"
#include "energy_institute_admission.h"
#include "energy_institute_verified_series.h"
#include "bnr_bls_verified_series.h"
#include <glib.h>
#include <glib/gstdio.h>

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

    AtmChartSpec *live = NULL;
    g_assert_true (atm_chart_live_reconstruct_gistemp (
        snapshot,
        "ewd",
        "2026-08-31",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        &live,
        &error
    ));
    g_assert_no_error (error);
    g_assert_nonnull (live);
    g_assert_cmpstr (
        atm_chart_spec_id (live), ==, atm_chart_spec_id (reference)
    );

    atm_chart_spec_free (live);
    atm_chart_spec_free (spec);
    atm_chart_spec_free (reference);
    atm_verified_series_free (series);
    atm_gistemp_admission_free (admission);
    for (guint i = 0; i < 4; i++)
        g_clear_pointer (&sources[i], g_bytes_unref);
    g_free (snapshot);
}

static void
test_live_reconstruct_current_snapshot (void)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    char *snapshot = g_build_filename (root, "ewd-current", NULL);
    AtmChartSpec *spec = NULL;
    GError *error = NULL;
    g_assert_true (atm_chart_live_reconstruct_gistemp (
        snapshot,
        "ewd",
        "main",
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb",
        &spec,
        &error
    ));
    g_assert_no_error (error);
    g_assert_nonnull (spec);
    atm_chart_spec_free (spec);
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


typedef struct {
    const char *repository_id;
    const char *repository_version;
    const char *snapshot_sha;
    const char *chart_schema;
    const char *chart_spec_id;
    const char *chart_kind;
    const char *reconstruction_profile;
    const char *series_profile;
    const char *admission_profile;
    const char *scientific_id;
    const char *qualified_id;
    const char *source_path;
} EiHistoryRecipe;

static const char *ei_source_paths[ATM_EI_SOURCE_COUNT] = {
    "science/data/processed/energy_institute_global_2026.csv",
    "science/data/processed/energy_institute_global_2026.provenance.json",
    "science/data/input_manifest.json",
    "science/data/registry.csv"
};

static char *
fixture_snapshot_named (const char *name)
{
    const char *root = g_getenv ("ATM_CHART01_FIXTURE");
    g_assert_nonnull (root);
    return g_build_filename (root, name, NULL);
}

static void
load_ei_fixture_sources (const char *snapshot_path,
                         GBytes *sources[ATM_EI_SOURCE_COUNT])
{
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *path = g_build_filename (snapshot_path, ei_source_paths[i], NULL);
        char *contents = NULL;
        gsize length = 0;
        GError *error = NULL;
        g_assert_true (g_file_get_contents (path, &contents, &length, &error));
        g_assert_no_error (error);
        g_assert_nonnull (contents);
        sources[i] = g_bytes_new_take ((guint8 *) contents, length);
        g_free (path);
    }
}

static gboolean
reconstruct_ei_recipe (const char *snapshot_path,
                       const EiHistoryRecipe *r,
                       AtmChartSpec **out,
                       GError **error)
{
    return atm_chart_history_reconstruct_energy_institute (
        snapshot_path,
        r->repository_id,
        r->repository_version,
        r->snapshot_sha,
        r->chart_schema,
        r->chart_spec_id,
        r->chart_kind,
        r->reconstruction_profile,
        r->series_profile,
        r->admission_profile,
        r->scientific_id,
        r->qualified_id,
        r->source_path,
        out,
        error
    );
}


static char *
make_ei_fixture_copy (const char *source_path,
                      gint missing_index,
                      gint tamper_index)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp ("atm-ei-history-fixture-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (root);

    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *source = g_build_filename (source_path, ei_source_paths[i], NULL);
        char *destination = g_build_filename (root, ei_source_paths[i], NULL);
        char *contents = NULL;
        gsize length = 0;

        g_assert_true (g_file_get_contents (source, &contents, &length, &error));
        g_assert_no_error (error);
        g_free (source);

        if ((gint) i == missing_index) {
            g_free (contents);
            g_free (destination);
            continue;
        }

        if ((gint) i == tamper_index) {
            g_assert_cmpuint (length, >, 0);
            contents[0] = contents[0] == '{' ? '[' : '{';
        }

        char *parent = g_path_get_dirname (destination);
        g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
        g_assert_true (g_file_set_contents (destination, contents, (gssize) length, &error));
        g_assert_no_error (error);
        g_free (parent);
        g_free (contents);
        g_free (destination);
    }

    return root;
}

static void
remove_ei_fixture_copy (char *root)
{
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        char *path = g_build_filename (root, ei_source_paths[i], NULL);
        g_remove (path);
        g_free (path);
    }

    char *processed = g_build_filename (root, "science", "data", "processed", NULL);
    char *data = g_build_filename (root, "science", "data", NULL);
    char *science = g_build_filename (root, "science", NULL);
    g_rmdir (processed);
    g_rmdir (data);
    g_rmdir (science);
    g_rmdir (root);
    g_free (processed);
    g_free (data);
    g_free (science);
    g_free (root);
}

static void
test_ei_history_reconstructs_pinned_snapshots (void)
{
    static const char *snapshots[] = {
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb"
    };
    static const char *fixture_names[] = { "ewd", "ewd-current" };
    char *qualified_ids[2] = { NULL, NULL };

    for (guint i = 0; i < G_N_ELEMENTS (snapshots); i++) {
        char *snapshot_path = fixture_snapshot_named (fixture_names[i]);
        GBytes *sources[ATM_EI_SOURCE_COUNT] = { NULL, NULL, NULL, NULL };
        AtmEnergyInstituteAdmission *admission = NULL;
        AtmEnergyInstituteVerifiedSeries *series = NULL;
        AtmChartSpec *reference = NULL;
        AtmChartSpec *restored = NULL;
        GError *error = NULL;
        char *repository_version = g_strconcat ("snapshot:", snapshots[i], NULL);

        load_ei_fixture_sources (snapshot_path, sources);
        g_assert_true (atm_energy_institute_admission_new (
            "LaurentiuStaicu/empirical-world3-dynamics",
            snapshots[i], sources, &admission, &error));
        g_assert_no_error (error);
        g_assert_true (atm_energy_institute_verified_series_from_admission (
            admission, &series, &error));
        g_assert_no_error (error);

        AtmChartSpec *live = NULL;
        g_assert_true (atm_chart_live_reconstruct_energy_institute (
            snapshot_path, "ewd", snapshots[i], "line", &live, &error));
        g_assert_no_error (error);
        g_assert_nonnull (live);

        g_assert_true (atm_chart_spec_new_energy_institute_v2 (
            ATM_CHART_LINE, series, &reference, &error));
        g_assert_no_error (error);
        g_assert_cmpuint (atm_chart_spec_point_count (reference, 0), ==, 61u);
        g_assert_cmpstr (atm_chart_spec_id (live), ==, atm_chart_spec_id (reference));
        g_assert_cmpstr (atm_chart_spec_series_qualified_id (live, 0), ==,
                         atm_chart_spec_series_qualified_id (reference, 0));
        g_assert_cmpuint (atm_chart_spec_point_count (live, 0), ==, 61u);

        EiHistoryRecipe recipe = {
            .repository_id = i == 0
                ? "LaurentiuStaicu/empirical-world3-dynamics" : "ewd",
            .repository_version = repository_version,
            .snapshot_sha = snapshots[i],
            .chart_schema = ATM_CHART_SPEC_V2_SCHEMA,
            .chart_spec_id = atm_chart_spec_id (reference),
            .chart_kind = "LINE",
            .reconstruction_profile =
                "atm-chart-reconstruct/energy-institute-tes-complete-annual/1",
            .series_profile = ATM_EI_VERIFIED_SERIES_PROFILE,
            .admission_profile = ATM_EI_ADMISSION_PROFILE,
            .scientific_id =
                atm_energy_institute_verified_series_scientific_id (series),
            .qualified_id =
                atm_energy_institute_verified_series_qualified_id (series),
            .source_path = ei_source_paths[0]
        };

        g_assert_true (reconstruct_ei_recipe (
            snapshot_path, &recipe, &restored, &error));
        g_assert_no_error (error);
        g_assert_nonnull (restored);
        g_assert_cmpstr (atm_chart_spec_schema (restored), ==,
                         ATM_CHART_SPEC_V2_SCHEMA);
        g_assert_cmpstr (atm_chart_spec_id (restored), ==,
                         atm_chart_spec_id (reference));
        g_assert_cmpstr (atm_chart_spec_series_scientific_id (restored, 0), ==,
                         recipe.scientific_id);
        g_assert_cmpstr (atm_chart_spec_series_qualified_id (restored, 0), ==,
                         recipe.qualified_id);
        g_assert_cmpuint (atm_chart_spec_point_count (restored, 0), ==, 61u);
        qualified_ids[i] = g_strdup (atm_chart_spec_series_qualified_id (restored, 0));

        /* The persisted recipe may not relabel a source bundle's version,
         * source profile, admission, scientific identity or ChartSpec ID. */
        if (i == 0) {
            EiHistoryRecipe bad = recipe;
            AtmChartSpec *rejected = NULL;

            bad.repository_version = "2026-08-31";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.chart_schema = "atm-chart-spec/1";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.series_profile = "atm-series/gistemp-complete-annual/1";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.repository_id = "cbd";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.reconstruction_profile = "atm-chart-reconstruct/gistemp-complete-annual/1";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.admission_profile = "atm-gistemp-pinned-admission/1";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.source_path = "science/data/processed/nasa_gistemp_global_2026.csv";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.scientific_id =
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.qualified_id =
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.chart_spec_id =
                "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            bad = recipe;
            bad.snapshot_sha =
                "1111111111111111111111111111111111111111";
            bad.repository_version =
                "snapshot:1111111111111111111111111111111111111111";
            g_assert_false (reconstruct_ei_recipe (snapshot_path, &bad, &rejected, &error));
            g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
            g_assert_null (rejected);
            g_clear_error (&error);

            /* History must fail if a pinned source file is absent. */
            char *missing_snapshot = make_ei_fixture_copy (snapshot_path, 0, -1);
            g_assert_false (reconstruct_ei_recipe (
                missing_snapshot, &recipe, &rejected, &error));
            g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
            g_assert_null (rejected);
            g_clear_error (&error);
            remove_ei_fixture_copy (missing_snapshot);

            /* Matching CSV/provenance is insufficient if a metadata bundle
             * member differs from the snapshot-specific admission digest. */
            char *tampered_snapshot = make_ei_fixture_copy (snapshot_path, -1, 2);
            g_assert_false (reconstruct_ei_recipe (
                tampered_snapshot, &recipe, &rejected, &error));
            g_assert_error (error,
                            ATM_EI_CANDIDATE_ERROR,
                            ATM_EI_CANDIDATE_ERROR_SHAPE);
            g_assert_null (rejected);
            g_clear_error (&error);
            remove_ei_fixture_copy (tampered_snapshot);
        }

        atm_chart_spec_free (restored);
        atm_chart_spec_free (live);
        atm_chart_spec_free (reference);
        atm_energy_institute_verified_series_free (series);
        atm_energy_institute_admission_free (admission);
        for (guint j = 0; j < ATM_EI_SOURCE_COUNT; j++)
            g_clear_pointer (&sources[j], g_bytes_unref);
        g_free (repository_version);
        g_free (snapshot_path);
    }

    /* The current and originally pinned EWD snapshots share observation bytes,
     * but the distinct complete admission envelopes must yield distinct series
     * identities in History. */
    g_assert_cmpstr (qualified_ids[0], !=, qualified_ids[1]);
    g_free (qualified_ids[0]);
    g_free (qualified_ids[1]);
}


static char *
make_bnr_history_snapshot (void)
{
    static const char *fixture_names[ATM_BNR_BLS_SOURCE_COUNT] = {
        "bnr_bls_realised_rounds.csv",
        "processed_data_inventory_registry.json",
        "bnr_bls_canonical_panel_promotion_review.json",
        "bnr_bls_cross_round_mapping_audit.json",
        "bnr_bls_missing_round_semantic_mapping_audit.json"
    };
    static const char *source_paths[ATM_BNR_BLS_SOURCE_COUNT] = {
        "data/processed/bnr_bls_realised_rounds.csv",
        "data/provenance/processed_data_inventory_registry.json",
        "model/calibration_validation/bnr_bls_canonical_panel_promotion_review.json",
        "model/calibration_validation/bnr_bls_cross_round_mapping_audit.json",
        "model/calibration_validation/bnr_bls_missing_round_semantic_mapping_audit.json"
    };
    const char *root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    g_assert_nonnull (root);
    GError *error = NULL;
    char *snapshot = g_dir_make_tmp ("atm-bnr-history-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (snapshot);

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *source = g_build_filename (root, fixture_names[i], NULL);
        char *contents = NULL;
        gsize length = 0;
        g_assert_true (g_file_get_contents (source, &contents, &length, &error));
        g_assert_no_error (error);
        char *destination = g_build_filename (snapshot, source_paths[i], NULL);
        char *parent = g_path_get_dirname (destination);
        g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
        g_assert_true (g_file_set_contents (destination, contents, (gssize) length, &error));
        g_assert_no_error (error);
        g_free (parent);
        g_free (destination);
        g_free (contents);
        g_free (source);
    }
    return snapshot;
}

static void
remove_bnr_history_snapshot (const char *snapshot)
{
    static const char *const paths[ATM_BNR_BLS_SOURCE_COUNT] = {
        "data/processed/bnr_bls_realised_rounds.csv",
        "data/provenance/processed_data_inventory_registry.json",
        "model/calibration_validation/bnr_bls_canonical_panel_promotion_review.json",
        "model/calibration_validation/bnr_bls_cross_round_mapping_audit.json",
        "model/calibration_validation/bnr_bls_missing_round_semantic_mapping_audit.json"
    };
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        char *path = g_build_filename (snapshot, paths[i], NULL);
        g_remove (path);
        g_free (path);
    }
    char *path = g_build_filename (snapshot, "model", "calibration_validation", NULL);
    g_rmdir (path); g_free (path);
    path = g_build_filename (snapshot, "model", NULL); g_rmdir (path); g_free (path);
    path = g_build_filename (snapshot, "data", "processed", NULL); g_rmdir (path); g_free (path);
    path = g_build_filename (snapshot, "data", "provenance", NULL); g_rmdir (path); g_free (path);
    path = g_build_filename (snapshot, "data", NULL); g_rmdir (path); g_free (path);
    g_rmdir (snapshot);
}

static void
test_bnr_c01_history_reconstructs_pinned_snapshot (void)
{
    char *snapshot = make_bnr_history_snapshot ();
    const char *sha = ATM_BNR_BLS_SNAPSHOT_SHA;
    char *version = g_strconcat ("snapshot:", sha, NULL);
    AtmChartSpec *live = NULL;
    AtmChartSpec *restored = NULL;
    GError *error = NULL;

    g_assert_true (atm_chart_live_reconstruct_bnr_bls_c01 (
        snapshot, "rmd", sha, "LINE", &live, &error));
    g_assert_no_error (error);
    g_assert_nonnull (live);
    g_assert_cmpint (atm_chart_spec_version (live), ==,
                     ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3);
    g_assert_cmpstr (atm_chart_spec_schema (live), ==, ATM_CHART_SPEC_V3_SCHEMA);
    g_assert_cmpuint (atm_chart_spec_point_count (live, 0), ==, 12u);
    g_assert_cmpstr (atm_chart_spec_series_profile (live), ==,
                     ATM_BNR_BLS_VERIFIED_SERIES_PROFILE);
    g_assert_cmpstr (atm_chart_spec_series_snapshot_sha (live, 0), ==, sha);
    g_assert_cmpstr (atm_chart_spec_series_source_path (live, 0), ==,
                     "data/processed/bnr_bls_realised_rounds.csv");

    AtmChartPointView missing = {0};
    g_assert_true (atm_chart_spec_point (live, 0, 10u, &missing));
    g_assert_cmpstr (missing.calendar_quarter, ==, "2025-Q2");
    g_assert_cmpint (missing.y_status, ==, ATM_CHART_POINT_MISSING);
    g_assert_cmpstr (missing.missing_reason, ==, ATM_BNR_BLS_MISSING_REASON);
    g_assert_null (missing.source_decimal);
    g_assert_cmpuint (missing.source_row, ==, 0u);

    g_assert_true (atm_chart_history_reconstruct_bnr_bls_c01 (
        snapshot, "rmd", version, sha,
        ATM_CHART_SPEC_V3_SCHEMA, atm_chart_spec_id (live), "line",
        "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        ATM_BNR_BLS_VERIFIED_SERIES_PROFILE, ATM_BNR_BLS_PROFILE,
        atm_chart_spec_series_scientific_id (live, 0),
        atm_chart_spec_series_qualified_id (live, 0),
        "data/processed/bnr_bls_realised_rounds.csv",
        &restored, &error));
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    g_assert_cmpstr (atm_chart_spec_id (restored), ==, atm_chart_spec_id (live));
    g_assert_cmpstr (atm_chart_spec_series_scientific_id (restored, 0), ==,
                     atm_chart_spec_series_scientific_id (live, 0));
    g_assert_cmpstr (atm_chart_spec_series_qualified_id (restored, 0), ==,
                     atm_chart_spec_series_qualified_id (live, 0));

    AtmChartSpec *rejected = NULL;
    g_assert_false (atm_chart_history_reconstruct_bnr_bls_c01 (
        snapshot, "rmd", version, sha,
        ATM_CHART_SPEC_V3_SCHEMA,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "line", "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        ATM_BNR_BLS_VERIFIED_SERIES_PROFILE, ATM_BNR_BLS_PROFILE,
        atm_chart_spec_series_scientific_id (live, 0),
        atm_chart_spec_series_qualified_id (live, 0),
        "data/processed/bnr_bls_realised_rounds.csv", &rejected, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (rejected);
    g_clear_error (&error);

    char *missing_file = g_build_filename (snapshot,
        "model/calibration_validation/bnr_bls_cross_round_mapping_audit.json", NULL);
    g_assert_cmpint (g_remove (missing_file), ==, 0);
    g_assert_false (atm_chart_history_reconstruct_bnr_bls_c01 (
        snapshot, "rmd", version, sha,
        ATM_CHART_SPEC_V3_SCHEMA, atm_chart_spec_id (live), "line",
        "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        ATM_BNR_BLS_VERIFIED_SERIES_PROFILE, ATM_BNR_BLS_PROFILE,
        atm_chart_spec_series_scientific_id (live, 0),
        atm_chart_spec_series_qualified_id (live, 0),
        "data/processed/bnr_bls_realised_rounds.csv", &rejected, &error));
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_assert_null (rejected);
    g_clear_error (&error);
    atm_chart_spec_free (restored);
    atm_chart_spec_free (live);
    g_free (version);
    /* Rebuild the deleted member so the deterministic cleanup owns the full tree. */
    const char *fixture_root = g_getenv ("ATM_BNR_BLS_FIXTURE");
    char *fixture = g_build_filename (fixture_root, "bnr_bls_cross_round_mapping_audit.json", NULL);
    char *contents = NULL;
    gsize length = 0;
    g_assert_true (g_file_get_contents (fixture, &contents, &length, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_set_contents (missing_file, contents, (gssize) length, &error));
    g_assert_no_error (error);
    g_free (contents); g_free (fixture); g_free (missing_file);
    remove_bnr_history_snapshot (snapshot);
    g_free (snapshot);
}


static void
test_live_reconstruct_v4_catalog_dispatch (void)
{
    static const char *gistemp_id =
        "climate.gistemp.global-temperature-anomaly";
    static const char *energy_id =
        "energy.energy-institute.global-total-energy-supply";
    static const char *bnr_id =
        "banking.bnr.bls-c01.credit-standards-net-percentage";
    static const char *ewd_initial =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    static const char *rmd_snapshot =
        "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170";

    char *ewd = fixture_snapshot_named ("ewd");
    GError *error = NULL;
    AtmChartSpecV4 *v4 = NULL;

    g_assert_true (atm_chart_live_reconstruct_v4 (
        gistemp_id, ewd, "LaurentiuStaicu/empirical-world3-dynamics", "2026-08-31", ewd_initial, &v4, &error));
    g_assert_no_error (error);
    g_assert_nonnull (v4);
    g_assert_cmpstr (atm_chart_spec_v4_series_id (v4), ==, gistemp_id);
    g_assert_cmpstr (atm_chart_spec_v4_schema (v4), ==, ATM_CHART_SPEC_V4_SCHEMA);
    g_assert_cmpuint (atm_chart_spec_v4_coordinate_count (v4), ==, 146u);
    g_assert_cmpuint (atm_chart_spec_v4_numeric_observation_count (v4), ==, 146u);
    g_assert_cmpstr (atm_chart_spec_schema (
        atm_chart_spec_v4_source_spec (v4)), ==, ATM_CHART_SPEC_SCHEMA);
    g_assert_cmpstr (atm_chart_spec_series_snapshot_sha (
        atm_chart_spec_v4_source_spec (v4), 0), ==, ewd_initial);
    g_assert_cmpstr (atm_chart_spec_series_repository_id (
        atm_chart_spec_v4_source_spec (v4), 0), ==, "ewd");

    AtmConversationChartPresentationInput stored_presentation = {0};
    g_assert_true (atm_chart_history_presentation_input_from_v4 (
        v4, &stored_presentation, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (stored_presentation.presentation_spec_id, ==,
                     atm_chart_spec_v4_id (v4));
    g_assert_cmpstr (stored_presentation.source_chart_schema, ==,
                     ATM_CHART_SPEC_SCHEMA);
    g_assert_cmpstr (stored_presentation.source_chart_spec_id, ==,
                     atm_chart_spec_id (atm_chart_spec_v4_source_spec (v4)));

    AtmChartSpecV4 *reopened_v4 = NULL;
    g_assert_true (atm_chart_history_reconstruct_v4 (
        gistemp_id, atm_chart_spec_v4_source_spec (v4),
        &stored_presentation, &reopened_v4, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (atm_chart_spec_v4_id (reopened_v4), ==,
                     atm_chart_spec_v4_id (v4));
    atm_chart_spec_v4_free (reopened_v4);

    AtmConversationChartPresentationInput tampered_presentation =
        stored_presentation;
    tampered_presentation.presentation_spec_id =
        "0000000000000000000000000000000000000000000000000000000000000000";
    g_assert_false (atm_chart_history_reconstruct_v4 (
        gistemp_id, atm_chart_spec_v4_source_spec (v4),
        &tampered_presentation, &reopened_v4, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (reopened_v4);
    g_clear_error (&error);

    atm_chart_spec_v4_free (v4);
    v4 = NULL;

    g_assert_true (atm_chart_live_reconstruct_v4 (
        energy_id, ewd, "LaurentiuStaicu/empirical-world3-dynamics", NULL, ewd_initial, &v4, &error));
    g_assert_no_error (error);
    g_assert_nonnull (v4);
    g_assert_cmpstr (atm_chart_spec_v4_series_id (v4), ==, energy_id);
    g_assert_cmpuint (atm_chart_spec_v4_coordinate_count (v4), ==, 61u);
    g_assert_cmpuint (atm_chart_spec_v4_numeric_observation_count (v4), ==, 61u);
    g_assert_cmpstr (atm_chart_spec_schema (
        atm_chart_spec_v4_source_spec (v4)), ==, ATM_CHART_SPEC_V2_SCHEMA);
    atm_chart_spec_v4_free (v4);
    v4 = NULL;
    g_free (ewd);

    char *bnr = make_bnr_history_snapshot ();
    g_assert_true (atm_chart_live_reconstruct_v4 (
        bnr_id, bnr, "LaurentiuStaicu/romanian-monetary-dynamics", NULL, rmd_snapshot, &v4, &error));
    g_assert_no_error (error);
    g_assert_nonnull (v4);
    g_assert_cmpstr (atm_chart_spec_v4_series_id (v4), ==, bnr_id);
    g_assert_cmpuint (atm_chart_spec_v4_coordinate_count (v4), ==, 12u);
    g_assert_cmpuint (atm_chart_spec_v4_numeric_observation_count (v4), ==, 11u);
    g_assert_cmpstr (atm_chart_spec_series_repository_id (
        atm_chart_spec_v4_source_spec (v4), 0), ==, "rmd");
    g_assert_cmpstr (atm_chart_spec_v4_missing_coordinate (v4), ==, "2025-Q2");
    g_assert_cmpstr (atm_chart_spec_v4_missing_reason (v4), ==,
                     ATM_BNR_BLS_MISSING_REASON);
    g_assert_cmpstr (atm_chart_spec_schema (
        atm_chart_spec_v4_source_spec (v4)), ==, ATM_CHART_SPEC_V3_SCHEMA);
    AtmChartPointView missing = {0};
    g_assert_true (atm_chart_spec_point (
        atm_chart_spec_v4_source_spec (v4), 0u, 10u, &missing));
    g_assert_cmpint (missing.y_status, ==, ATM_CHART_POINT_MISSING);
    g_assert_null (missing.source_decimal);
    atm_chart_spec_v4_free (v4);
    v4 = NULL;
    remove_bnr_history_snapshot (bnr);
    g_free (bnr);
    ewd = fixture_snapshot_named ("ewd");

    /* A model-proposed/unknown selector is never allowed to pick a fallback. */
    g_assert_false (atm_chart_live_reconstruct_v4 (
        "population", ewd, "ewd", "2026-08-31", ewd_initial, &v4, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (v4);
    g_clear_error (&error);

    /* A registered ID cannot override the conversation's different repository. */
    g_assert_false (atm_chart_live_reconstruct_v4 (
        gistemp_id, ewd, "rmd", "2026-08-31", ewd_initial, &v4, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (v4);
    g_clear_error (&error);

    /* A valid series ID cannot authorize a snapshot outside its catalog allowlist. */
    g_assert_false (atm_chart_live_reconstruct_v4 (
        bnr_id, ewd, "ewd", NULL, ewd_initial, &v4, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (v4);
    g_clear_error (&error);
    g_free (ewd);

    /* GISTEMP requires its repository-version source identity as well. */
    ewd = fixture_snapshot_named ("ewd");
    g_assert_false (atm_chart_live_reconstruct_v4 (
        gistemp_id, ewd, "ewd", NULL, ewd_initial, &v4, &error));
    g_assert_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY);
    g_assert_null (v4);
    g_clear_error (&error);
    g_free (ewd);
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
        "/chart/history-reconstruct/current-snapshot",
        test_live_reconstruct_current_snapshot
    );
    g_test_add_func (
        "/chart/history-reconstruct/fail-closed",
        test_reject_tampered_identity_and_snapshot
    );
    g_test_add_func (
        "/chart/history-reconstruct/energy-institute-pinned-snapshots",
        test_ei_history_reconstructs_pinned_snapshots
    );
    g_test_add_func (
        "/chart/history-reconstruct/bnr-c01-pinned-snapshot",
        test_bnr_c01_history_reconstructs_pinned_snapshot
    );

    g_test_add_func (
        "/chart/history-reconstruct/v4-catalog-dispatch",
        test_live_reconstruct_v4_catalog_dispatch
    );
    return g_test_run ();
}
