#include "chart_history_reconstruct.h"
#include "gistemp_admission.h"
#include "verified_series.h"
#include <gio/gio.h>
#include <string.h>

static gboolean
reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}

static gboolean
read_source (
    const char *snapshot_path,
    const char *relative_path,
    GBytes **out,
    GError **error
)
{
    char *path = NULL;
    char *contents = NULL;
    gsize length = 0;

    g_return_val_if_fail (snapshot_path != NULL, FALSE);
    g_return_val_if_fail (relative_path != NULL, FALSE);
    g_return_val_if_fail (out != NULL && *out == NULL, FALSE);

    path = g_build_filename (snapshot_path, relative_path, NULL);
    if (!g_file_get_contents (path, &contents, &length, error)) {
        g_free (path);
        return FALSE;
    }

    *out = g_bytes_new_take ((guint8 *) contents, length);
    g_free (path);
    return TRUE;
}

static gboolean
chart_kind_from_text (const char *text, AtmChartKind *out, GError **error)
{
    if (g_strcmp0 (text, "LINE") == 0 ||
        g_strcmp0 (text, "line") == 0) {
        *out = ATM_CHART_LINE;
        return TRUE;
    }
    if (g_strcmp0 (text, "SCATTER") == 0 ||
        g_strcmp0 (text, "scatter") == 0) {
        *out = ATM_CHART_SCATTER;
        return TRUE;
    }

    return reject (error, "Persisted chart kind is not admitted.");
}

gboolean
atm_chart_history_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *chart_kind,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *scientific_id,
    const char *qualified_id,
    const char *source_path,
    AtmChartSpec **out,
    GError **error
)
{
    static const char *expected_source =
        "science/data/processed/nasa_gistemp_global_2026.csv";
    static const char *expected_repository =
        "LaurentiuStaicu/empirical-world3-dynamics";
    static const char *expected_snapshot =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    static const char *expected_schema = ATM_CHART_SPEC_SCHEMA;
    static const char *expected_reconstruction =
        "atm-chart-reconstruct/gistemp-complete-annual/1";

    const char *source_paths[ATM_GISTEMP_SOURCE_COUNT] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[ATM_GISTEMP_SOURCE_COUNT] = { NULL, NULL, NULL, NULL };
    AtmGistempAdmission *admission = NULL;
    AtmVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    AtmChartKind kind;
    gboolean ok = FALSE;

    g_return_val_if_fail (out != NULL && *out == NULL, FALSE);

    if (snapshot_path == NULL ||
        repository_id == NULL ||
        repository_version == NULL ||
        snapshot_sha == NULL ||
        chart_schema == NULL ||
        chart_spec_id == NULL ||
        chart_kind == NULL ||
        reconstruction_profile == NULL ||
        series_profile == NULL ||
        admission_profile == NULL ||
        scientific_id == NULL ||
        qualified_id == NULL ||
        source_path == NULL) {
        return reject (error, "Incomplete persisted chart reconstruction recipe.");
    }

    if (g_strcmp0 (repository_id, expected_repository) != 0 ||
        g_strcmp0 (snapshot_sha, expected_snapshot) != 0) {
        return reject (error, "Persisted chart repository identity is not admitted.");
    }

    if (repository_version[0] == '\0' ||
        g_strcmp0 (chart_schema, expected_schema) != 0 ||
        g_strcmp0 (series_profile, ATM_VERIFIED_SERIES_PROFILE) != 0 ||
        g_strcmp0 (admission_profile, ATM_GISTEMP_ADMISSION_PROFILE) != 0 ||
        g_strcmp0 (reconstruction_profile, expected_reconstruction) != 0 ||
        g_strcmp0 (source_path, expected_source) != 0) {
        return reject (error, "Persisted chart reconstruction profile is not admitted.");
    }

    if (!chart_kind_from_text (chart_kind, &kind, error)) {
        return FALSE;
    }

    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++) {
        if (!read_source (snapshot_path, source_paths[i], &sources[i], error)) {
            goto cleanup;
        }
    }

    if (!atm_gistemp_admission_new (
            repository_id,
            snapshot_sha,
            sources,
            &admission,
            error)) {
        goto cleanup;
    }

    if (!atm_verified_series_from_gistemp (
            admission,
            &series,
            error)) {
        goto cleanup;
    }

    if (g_strcmp0 (
            atm_verified_series_scientific_id (series),
            scientific_id) != 0 ||
        g_strcmp0 (
            atm_verified_series_qualified_id (series),
            qualified_id) != 0) {
        reject (error, "Persisted chart series identities disagree with rebuilt source.");
        goto cleanup;
    }

    const AtmVerifiedSeries *inputs[1] = { series };
    if (!atm_chart_spec_new (kind, inputs, 1, &spec, error)) {
        goto cleanup;
    }

    if (g_strcmp0 (atm_chart_spec_id (spec), chart_spec_id) != 0) {
        reject (error, "Persisted chart specification identity disagrees with rebuilt source.");
        goto cleanup;
    }

    if (g_strcmp0 (
            atm_gistemp_admission_source_path (admission, 0),
            source_path) != 0) {
        reject (error, "Persisted chart source path disagrees with rebuilt admission.");
        goto cleanup;
    }

    *out = spec;
    spec = NULL;
    ok = TRUE;

cleanup:
    atm_chart_spec_free (spec);
    atm_verified_series_free (series);
    atm_gistemp_admission_free (admission);
    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++)
        g_clear_pointer (&sources[i], g_bytes_unref);
    return ok;
}
