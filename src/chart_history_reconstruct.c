#include "chart_history_reconstruct.h"
#include "gistemp_admission.h"
#include "verified_series.h"
#include "verified_series_contract.h"
#include <gio/gio.h>
#include <string.h>

static gboolean
reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}

static gboolean
generic_series_metadata_matches (const AtmGistempAdmission *admission,
                                 const AtmSeriesContract *generic)
{
    const AtmSeriesAdapterMetadata *metadata =
        atm_verified_series_gistemp_metadata ();
    char *expected_repository_version = g_strdup_printf (
        "snapshot:%s", atm_gistemp_admission_snapshot (admission));

    gboolean matches =
        generic != NULL &&
        generic->x_kind == ATM_SERIES_X_CALENDAR_YEAR &&
        g_strcmp0 (generic->subject,
                   atm_gistemp_admission_subject (admission)) == 0 &&
        g_strcmp0 (generic->attribute,
                   atm_gistemp_admission_attribute (admission)) == 0 &&
        g_strcmp0 (generic->x_unit, "calendar_year") == 0 &&
        g_strcmp0 (generic->x_dimension, "time") == 0 &&
        g_strcmp0 (generic->y_unit,
                   atm_gistemp_admission_unit (admission)) == 0 &&
        g_strcmp0 (generic->y_dimension, "temperature_difference") == 0 &&
        g_strcmp0 (generic->series_semantics,
                   metadata->series_semantics) == 0 &&
        g_strcmp0 (generic->scenario, metadata->scenario) == 0 &&
        g_strcmp0 (generic->time_scope, metadata->time_scope) == 0 &&
        g_strcmp0 (generic->epistemic_status,
                   atm_gistemp_admission_epistemic_status (admission)) == 0 &&
        g_strcmp0 (generic->repository_id, "ewd") == 0 &&
        g_strcmp0 (generic->repository_version,
                   expected_repository_version) == 0 &&
        g_strcmp0 (generic->snapshot_sha,
                   atm_gistemp_admission_snapshot (admission)) == 0 &&
        g_strcmp0 (generic->source_path,
                   atm_gistemp_admission_source_path (admission, 0)) == 0 &&
        g_strcmp0 (generic->profile_id, metadata->profile_id) == 0 &&
        g_strcmp0 (generic->profile_version, metadata->profile_version) == 0;

    g_free (expected_repository_version);
    return matches;
}

static gboolean
validate_generic_gistemp_parity (const AtmGistempAdmission *admission,
                                 const AtmVerifiedSeries *legacy,
                                 GError **error)
{
    AtmSeriesContract *generic = NULL;
    if (!atm_verified_series_contract (admission, &generic, error))
        return FALSE;

    if (!generic_series_metadata_matches (admission, generic)) {
        atm_series_contract_free (generic);
        return reject (error,
            "Generic series metadata disagrees with pinned GISTEMP qualification.");
    }

    gboolean matches =
        generic->points != NULL &&
        generic->points->len == atm_verified_series_count (legacy);
    if (!matches) {
        atm_series_contract_free (generic);
        return reject (error,
            "Generic and persisted GISTEMP series have different point counts.");
    }

    for (guint i = 0; i < generic->points->len; i++) {
        const AtmSeriesContractPoint *point =
            g_ptr_array_index (generic->points, i);
        const AtmVerifiedPoint *verified =
            atm_verified_series_point (legacy, i);
        char *year = verified != NULL
            ? g_strdup_printf ("%u", verified->calendar_year)
            : NULL;

        matches = point != NULL && verified != NULL && year != NULL &&
            point->order == verified->order &&
            point->y_status == ATM_SERIES_Y_NUMERIC &&
            verified->y_status == ATM_VERIFIED_Y_NUMERIC &&
            g_strcmp0 (point->x, year) == 0 &&
            g_strcmp0 (point->source_decimal, verified->source_decimal) == 0 &&
            g_strcmp0 (point->coefficient, verified->coefficient) == 0 &&
            point->exponent == verified->exponent &&
            point->negative_zero ==
                (verified->source_decimal != NULL &&
                 verified->source_decimal[0] == '-' &&
                 g_strcmp0 (verified->coefficient, "0") == 0 &&
                 verified->exponent == 0) &&
            !point->break_before && point->missing_reason == NULL &&
            point->break_reason == NULL &&
            point->support != NULL && verified->support != NULL &&
            point->support->len == verified->support->len;
        g_free (year);

        if (matches) {
            /* Require one-to-one support equality. Membership alone would
             * permit a duplicate generic ID to mask an omitted legacy ID. */
            gboolean *matched = g_new0 (gboolean, verified->support->len);
            for (guint j = 0; j < point->support->len; j++) {
                const char *support_id = g_ptr_array_index (point->support, j);
                gboolean found = FALSE;
                for (guint k = 0; k < verified->support->len; k++) {
                    if (!matched[k] &&
                        g_strcmp0 (support_id,
                                   g_ptr_array_index (verified->support, k)) == 0) {
                        matched[k] = TRUE;
                        found = TRUE;
                        break;
                    }
                }
                if (!found) {
                    matches = FALSE;
                    break;
                }
            }
            g_free (matched);
        }

        if (!matches) {
            atm_series_contract_free (generic);
            return reject (error,
                "Generic series disagrees with the pinned VerifiedSeries points or support.");
        }
    }

    atm_series_contract_free (generic);
    return TRUE;
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
    static const char *expected_repository_key = "ewd";
    static const char *expected_snapshot_initial =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    static const char *expected_snapshot_current =
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
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
    const char *admission_repository = NULL;
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

    if ((g_strcmp0 (repository_id, expected_repository_key) != 0 &&
         g_strcmp0 (repository_id, expected_repository) != 0) ||
        (g_strcmp0 (snapshot_sha, expected_snapshot_initial) != 0 &&
         g_strcmp0 (snapshot_sha, expected_snapshot_current) != 0)) {
        return reject (error, "Persisted chart repository identity is not admitted.");
    }

    /* The durable conversation schema stores the canonical repository key
     * (ewd); the scientific admission contract uses the immutable repository
     * identity. Accept the historical full identity as an internal/native
     * caller form, but always qualify against the immutable identity. */
    admission_repository =
        g_strcmp0 (repository_id, expected_repository_key) == 0
            ? expected_repository
            : repository_id;

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
            admission_repository,
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

    /* Keep legacy persisted IDs authoritative, but require the generic
     * source-bound chain to reconstruct identical point values and support
     * before this series can reach ChartSpec or rendering. */
    if (!validate_generic_gistemp_parity (admission, series, error)) {
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


gboolean
atm_chart_live_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    AtmChartSpec **out,
    GError **error
)
{
    static const char *expected_repository =
        "LaurentiuStaicu/empirical-world3-dynamics";
    static const char *expected_repository_key = "ewd";
    static const char *expected_snapshot_initial =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    static const char *expected_snapshot_current =
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    static const char *source_paths[ATM_GISTEMP_SOURCE_COUNT] = {
        "science/data/processed/nasa_gistemp_global_2026.csv",
        "science/data/processed/nasa_gistemp_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[ATM_GISTEMP_SOURCE_COUNT] = { NULL, NULL, NULL, NULL };
    AtmGistempAdmission *admission = NULL;
    AtmVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    const char *admission_repository =
        NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (out != NULL && *out == NULL, FALSE);

    if (snapshot_path == NULL ||
        repository_id == NULL ||
        repository_version == NULL ||
        snapshot_sha == NULL ||
        repository_version[0] == '\0') {
        return reject (error, "Incomplete live chart reconstruction identity.");
    }

    if (g_strcmp0 (repository_id, expected_repository_key) != 0 &&
        g_strcmp0 (repository_id, expected_repository) != 0) {
        g_set_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY,
            "Live chart repository is not admitted (repository_id=%s).",
            repository_id);
        return FALSE;
    }
    if (g_strcmp0 (snapshot_sha, expected_snapshot_initial) != 0 &&
        g_strcmp0 (snapshot_sha, expected_snapshot_current) != 0) {
        g_set_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY,
            "Live chart snapshot is not admitted (repository_id=%s snapshot_sha=%s).",
            repository_id, snapshot_sha);
        return FALSE;
    }

    admission_repository =
        g_strcmp0 (repository_id, expected_repository_key) == 0
            ? expected_repository
            : repository_id;

    for (guint i = 0; i < ATM_GISTEMP_SOURCE_COUNT; i++) {
        if (!read_source (
                snapshot_path,
                source_paths[i],
                &sources[i],
                error)) {
            goto cleanup;
        }
    }

    if (!atm_gistemp_admission_new (
            admission_repository,
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

    /* The live route uses the same generic proof as History before ChartSpec. */
    if (!validate_generic_gistemp_parity (admission, series, error)) {
        goto cleanup;
    }

    const AtmVerifiedSeries *inputs[1] = { series };
    if (!atm_chart_spec_new (
            ATM_CHART_LINE,
            inputs,
            1,
            &spec,
            error)) {
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
