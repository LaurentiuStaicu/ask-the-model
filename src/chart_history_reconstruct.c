#include "chart_history_reconstruct.h"
#include "chart_catalog.h"
#include "conversation_store.h"
#include "chart_spec_v4.h"
#include "gistemp_admission.h"
#include "verified_series.h"
#include "verified_series_contract.h"
#include "energy_institute_admission.h"
#include "energy_institute_verified_series.h"
#include "bnr_bls_verified_series.h"
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
atm_chart_history_reconstruct_energy_institute (
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
    static const char *expected_repository =
        "LaurentiuStaicu/empirical-world3-dynamics";
    static const char *expected_repository_key = "ewd";
    static const char *expected_snapshot_initial =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    static const char *expected_snapshot_current =
        "dc710d8e604c0fff713a3acfdb18ab47a1a902bb";
    static const char *expected_reconstruction =
        "atm-chart-reconstruct/energy-institute-tes-complete-annual/1";
    static const char *expected_source =
        "science/data/processed/energy_institute_global_2026.csv";
    static const char *source_paths[ATM_EI_SOURCE_COUNT] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };

    GBytes *sources[ATM_EI_SOURCE_COUNT] = { NULL, NULL, NULL, NULL };
    AtmEnergyInstituteAdmission *admission = NULL;
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    AtmChartKind kind;
    const char *admission_repository = NULL;
    char *expected_repository_version = NULL;
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
        return reject (error,
            "Incomplete persisted Energy Institute chart reconstruction recipe.");
    }

    if ((g_strcmp0 (repository_id, expected_repository_key) != 0 &&
         g_strcmp0 (repository_id, expected_repository) != 0) ||
        (g_strcmp0 (snapshot_sha, expected_snapshot_initial) != 0 &&
         g_strcmp0 (snapshot_sha, expected_snapshot_current) != 0)) {
        return reject (error,
            "Persisted Energy Institute repository identity or snapshot is not admitted.");
    }

    /* The store persists the canonical repository key; the admission contract
     * binds the immutable EWD repository identity. EI's v4 tuple additionally
     * requires repository_version to be exactly snapshot:<snapshot_sha>. */
    admission_repository =
        g_strcmp0 (repository_id, expected_repository_key) == 0
            ? expected_repository
            : repository_id;
    expected_repository_version =
        g_strconcat ("snapshot:", snapshot_sha, NULL);

    if (g_strcmp0 (repository_version, expected_repository_version) != 0 ||
        g_strcmp0 (chart_schema, ATM_CHART_SPEC_V2_SCHEMA) != 0 ||
        g_strcmp0 (series_profile, ATM_EI_VERIFIED_SERIES_PROFILE) != 0 ||
        g_strcmp0 (admission_profile, ATM_EI_ADMISSION_PROFILE) != 0 ||
        g_strcmp0 (reconstruction_profile, expected_reconstruction) != 0 ||
        g_strcmp0 (source_path, expected_source) != 0) {
        g_free (expected_repository_version);
        return reject (error,
            "Persisted Energy Institute chart reconstruction profile is not admitted.");
    }
    g_free (expected_repository_version);

    if (!chart_kind_from_text (chart_kind, &kind, error))
        return FALSE;

    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        if (!read_source (snapshot_path, source_paths[i], &sources[i], error))
            goto cleanup;
    }

    if (!atm_energy_institute_admission_new (
            admission_repository,
            snapshot_sha,
            sources,
            &admission,
            error))
        goto cleanup;

    if (!atm_energy_institute_verified_series_from_admission (
            admission,
            &series,
            error))
        goto cleanup;

    /* Reconstruct the complete source-bound chain from the pinned bytes.
     * The opaque EI series constructor rebuilds admission, Evidence, finalized
     * SRA and all 61 source points before ChartSpec v2 can be created. */
    if (g_strcmp0 (
            atm_energy_institute_admission_repository (admission),
            expected_repository) != 0 ||
        g_strcmp0 (
            atm_energy_institute_admission_snapshot (admission),
            snapshot_sha) != 0 ||
        g_strcmp0 (
            atm_energy_institute_admission_source_path (admission, 0),
            source_path) != 0 ||
        g_strcmp0 (
            atm_energy_institute_verified_series_scientific_id (series),
            scientific_id) != 0 ||
        g_strcmp0 (
            atm_energy_institute_verified_series_qualified_id (series),
            qualified_id) != 0) {
        reject (error,
            "Persisted Energy Institute series identities disagree with rebuilt source.");
        goto cleanup;
    }

    if (!atm_chart_spec_new_energy_institute_v2 (
            kind,
            series,
            &spec,
            error))
        goto cleanup;

    if (g_strcmp0 (atm_chart_spec_schema (spec), chart_schema) != 0 ||
        g_strcmp0 (atm_chart_spec_id (spec), chart_spec_id) != 0 ||
        g_strcmp0 (atm_chart_spec_series_profile (spec),
                   ATM_EI_VERIFIED_SERIES_PROFILE) != 0 ||
        g_strcmp0 (atm_chart_spec_series_scientific_id (spec, 0),
                   scientific_id) != 0 ||
        g_strcmp0 (atm_chart_spec_series_qualified_id (spec, 0),
                   qualified_id) != 0 ||
        g_strcmp0 (
            atm_energy_institute_admission_source_path (admission, 0),
            source_path) != 0) {
        reject (error,
            "Persisted Energy Institute ChartSpec identity disagrees with rebuilt source.");
        goto cleanup;
    }

    *out = spec;
    spec = NULL;
    ok = TRUE;

cleanup:
    atm_chart_spec_free (spec);
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
        g_clear_pointer (&sources[i], g_bytes_unref);
    return ok;
}



gboolean
atm_chart_live_reconstruct_bnr_bls_c01 (
    const char *snapshot_path,
    const char *repository_id,
    const char *snapshot_sha,
    const char *chart_kind,
    AtmChartSpec **out,
    GError **error
)
{
    static const char *expected_repository =
        ATM_BNR_BLS_UPSTREAM_REPOSITORY;
    static const char *expected_repository_key = "rmd";
    static const char *expected_snapshot =
        ATM_BNR_BLS_SNAPSHOT_SHA;
    static const char *source_paths[ATM_BNR_BLS_SOURCE_COUNT] = {
        "data/processed/bnr_bls_realised_rounds.csv",
        "data/provenance/processed_data_inventory_registry.json",
        "model/calibration_validation/bnr_bls_canonical_panel_promotion_review.json",
        "model/calibration_validation/bnr_bls_cross_round_mapping_audit.json",
        "model/calibration_validation/bnr_bls_missing_round_semantic_mapping_audit.json"
    };
    GBytes *sources[ATM_BNR_BLS_SOURCE_COUNT] = {NULL};
    AtmBnrBlsPanel *panel = NULL;
    AtmBnrBlsSra *sra = NULL;
    AtmBnrBlsSeriesContract *contract = NULL;
    AtmBnrBlsVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    AtmChartKind kind;
    const char *admission_repository = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (out != NULL && *out == NULL, FALSE);
    if (snapshot_path == NULL || repository_id == NULL ||
        snapshot_sha == NULL || chart_kind == NULL ||
        (g_strcmp0 (repository_id, expected_repository_key) != 0 &&
         g_strcmp0 (repository_id, expected_repository) != 0) ||
        g_strcmp0 (snapshot_sha, expected_snapshot) != 0)
        return reject (error,
            "Live BNR BLS C01 chart request is not bound to the admitted RMD snapshot.");

    if (!chart_kind_from_text (chart_kind, &kind, error))
        return FALSE;
    admission_repository =
        g_strcmp0 (repository_id, expected_repository_key) == 0
            ? expected_repository : repository_id;

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        if (!read_source (snapshot_path, source_paths[i], &sources[i], error))
            goto cleanup;
    }
    if (!atm_bnr_bls_panel_new (
            admission_repository, expected_snapshot, sources, &panel, error))
        goto cleanup;
    if (!atm_bnr_bls_sra_new (panel, &sra, error))
        goto cleanup;
    if (!atm_bnr_bls_series_contract_from_sra (
            panel, sra, &contract, error))
        goto cleanup;
    if (!atm_bnr_bls_verified_series_from_contract (
            contract, &series, error))
        goto cleanup;

    const AtmSeriesContract *generic =
        atm_bnr_bls_series_contract_value (
            atm_bnr_bls_verified_series_contract (series));
    if (generic == NULL ||
        atm_bnr_bls_panel_observation_count (panel) != 11u ||
        atm_bnr_bls_verified_series_count (series) != 12u ||
        g_strcmp0 (atm_bnr_bls_panel_repository (panel), expected_repository) != 0 ||
        g_strcmp0 (atm_bnr_bls_panel_snapshot (panel), expected_snapshot) != 0 ||
        g_strcmp0 (generic->repository_version,
                   "snapshot:" ATM_BNR_BLS_SNAPSHOT_SHA) != 0 ||
        g_strcmp0 (generic->source_path, source_paths[0]) != 0) {
        reject (error,
            "Live BNR BLS C01 series does not match the pinned source identity.");
        goto cleanup;
    }

    if (!atm_chart_spec_new_bnr_bls_c01_v3 (kind, series, &spec, error))
        goto cleanup;
    if (atm_chart_spec_version (spec) != ATM_CHART_SPEC_VERSION_BNR_BLS_C01_V3 ||
        atm_chart_spec_point_count (spec, 0) != 12u ||
        g_strcmp0 (atm_chart_spec_series_snapshot_sha (spec, 0), expected_snapshot) != 0 ||
        g_strcmp0 (atm_chart_spec_series_source_path (spec, 0), source_paths[0]) != 0) {
        reject (error, "Live BNR BLS C01 ChartSpec does not match the pinned source.");
        goto cleanup;
    }

    *out = spec;
    spec = NULL;
    ok = TRUE;

cleanup:
    atm_chart_spec_free (spec);
    atm_bnr_bls_verified_series_free (series);
    atm_bnr_bls_series_contract_free (contract);
    atm_bnr_bls_sra_free (sra);
    atm_bnr_bls_panel_free (panel);
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++)
        g_clear_pointer (&sources[i], g_bytes_unref);
    return ok;
}

gboolean
atm_chart_history_reconstruct_bnr_bls_c01 (
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
    const char *expected_repository = ATM_BNR_BLS_UPSTREAM_REPOSITORY;
    const char *expected_key = "rmd";
    const char *expected_snapshot = ATM_BNR_BLS_SNAPSHOT_SHA;
    char *expected_version = g_strconcat ("snapshot:", expected_snapshot, NULL);
    gboolean valid_input =
        snapshot_path != NULL &&
        (g_strcmp0 (repository_id, expected_key) == 0 ||
         g_strcmp0 (repository_id, expected_repository) == 0) &&
        g_strcmp0 (repository_version, expected_version) == 0 &&
        g_strcmp0 (snapshot_sha, expected_snapshot) == 0 &&
        g_strcmp0 (chart_schema, ATM_CHART_SPEC_V3_SCHEMA) == 0 &&
        chart_spec_id != NULL && chart_spec_id[0] != '\0' &&
        chart_kind != NULL &&
        g_strcmp0 (reconstruction_profile,
                   "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1") == 0 &&
        g_strcmp0 (series_profile,
                   ATM_BNR_BLS_VERIFIED_SERIES_PROFILE) == 0 &&
        g_strcmp0 (admission_profile,
                   ATM_BNR_BLS_PROFILE) == 0 &&
        scientific_id != NULL && scientific_id[0] != '\0' &&
        qualified_id != NULL && qualified_id[0] != '\0' &&
        g_strcmp0 (source_path,
                   "data/processed/bnr_bls_realised_rounds.csv") == 0;
    g_free (expected_version);
    if (!valid_input)
        return reject (error,
            "Persisted BNR BLS C01 chart reconstruction recipe is not admitted.");

    AtmChartSpec *spec = NULL;
    if (!atm_chart_live_reconstruct_bnr_bls_c01 (
            snapshot_path, repository_id, snapshot_sha, chart_kind, &spec, error))
        return FALSE;

    if (g_strcmp0 (atm_chart_spec_schema (spec), chart_schema) != 0 ||
        g_strcmp0 (atm_chart_spec_id (spec), chart_spec_id) != 0 ||
        g_strcmp0 (atm_chart_spec_series_profile (spec), series_profile) != 0 ||
        g_strcmp0 (atm_chart_spec_series_scientific_id (spec, 0), scientific_id) != 0 ||
        g_strcmp0 (atm_chart_spec_series_qualified_id (spec, 0), qualified_id) != 0 ||
        g_strcmp0 (atm_chart_spec_series_snapshot_sha (spec, 0), snapshot_sha) != 0 ||
        g_strcmp0 (atm_chart_spec_series_source_path (spec, 0), source_path) != 0 ||
        g_strcmp0 (atm_chart_spec_series_repository_id (spec, 0), "rmd") != 0) {
        atm_chart_spec_free (spec);
        return reject (error,
            "Persisted BNR BLS C01 ChartSpec identity disagrees with rebuilt source.");
    }

    *out = spec;
    return TRUE;
}


/* Live EI reconstruction uses the immutable conversation snapshot SHA as source
 * identity. CITATION.cff's release version remains the conversation pin; it is
 * not substituted for the ChartSpec v2 source version. */
gboolean
atm_chart_live_reconstruct_energy_institute (
    const char *snapshot_path,
    const char *repository_id,
    const char *snapshot_sha,
    const char *chart_kind,
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
    static const char *source_paths[ATM_EI_SOURCE_COUNT] = {
        "science/data/processed/energy_institute_global_2026.csv",
        "science/data/processed/energy_institute_global_2026.provenance.json",
        "science/data/input_manifest.json",
        "science/data/registry.csv"
    };
    GBytes *sources[ATM_EI_SOURCE_COUNT] = { NULL, NULL, NULL, NULL };
    AtmEnergyInstituteAdmission *admission = NULL;
    AtmEnergyInstituteVerifiedSeries *series = NULL;
    AtmChartSpec *spec = NULL;
    AtmChartKind kind;
    const char *admission_repository = NULL;
    char *expected_version = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (out != NULL && *out == NULL, FALSE);
    if (snapshot_path == NULL || repository_id == NULL ||
        snapshot_sha == NULL || chart_kind == NULL ||
        (g_strcmp0 (repository_id, expected_repository_key) != 0 &&
         g_strcmp0 (repository_id, expected_repository) != 0) ||
        (g_strcmp0 (snapshot_sha, expected_snapshot_initial) != 0 &&
         g_strcmp0 (snapshot_sha, expected_snapshot_current) != 0)) {
        return reject (error,
            "Live Energy Institute chart request is not bound to an admitted EWD snapshot.");
    }

    if (!chart_kind_from_text (chart_kind, &kind, error))
        return FALSE;
    admission_repository =
        g_strcmp0 (repository_id, expected_repository_key) == 0
            ? expected_repository
            : repository_id;
    expected_version = g_strconcat ("snapshot:", snapshot_sha, NULL);

    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++) {
        if (!read_source (snapshot_path, source_paths[i], &sources[i], error))
            goto cleanup;
    }
    if (!atm_energy_institute_admission_new (
            admission_repository, snapshot_sha, sources, &admission, error))
        goto cleanup;
    if (!atm_energy_institute_verified_series_from_admission (
            admission, &series, error))
        goto cleanup;

    const AtmSeriesContract *contract =
        atm_energy_institute_verified_series_contract (series);
    if (g_strcmp0 (atm_energy_institute_admission_repository (admission),
                   expected_repository) != 0 ||
        g_strcmp0 (atm_energy_institute_admission_snapshot (admission),
                   snapshot_sha) != 0 ||
        atm_energy_institute_verified_series_count (series) != 61 ||
        contract == NULL ||
        g_strcmp0 (contract->repository_version, expected_version) != 0) {
        reject (error,
            "Live Energy Institute series does not match the pinned snapshot identity.");
        goto cleanup;
    }

    if (!atm_chart_spec_new_energy_institute_v2 (kind, series, &spec, error))
        goto cleanup;
    if (atm_chart_spec_version (spec) != ATM_CHART_SPEC_VERSION_ENERGY_INSTITUTE_V2 ||
        atm_chart_spec_point_count (spec, 0) != 61 ||
        g_strcmp0 (atm_chart_spec_series_snapshot_sha (spec, 0), snapshot_sha) != 0) {
        reject (error, "Live Energy Institute ChartSpec does not match the pinned source.");
        goto cleanup;
    }

    *out = spec;
    spec = NULL;
    ok = TRUE;

cleanup:
    g_free (expected_version);
    atm_chart_spec_free (spec);
    atm_energy_institute_verified_series_free (series);
    atm_energy_institute_admission_free (admission);
    for (guint i = 0; i < ATM_EI_SOURCE_COUNT; i++)
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


/*
 * Catalog-selected live factory for the CHART-06 generic presentation path.
 * The caller supplies only a registered series ID and the already-selected
 * repository pin identity; the immutable catalog chooses the closed adapter.
 * This remains separate from History persistence, which keeps its v1/v2/v3
 * source-specific recipes until the CHART-06h migration gate is qualified.
 */
gboolean
atm_chart_live_reconstruct_v4 (
    const char *series_id,
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    AtmChartSpecV4 **out,
    GError **error
)
{
    if (out == NULL || *out != NULL)
        return reject (error,
            "ChartSpec v4 output must be non-NULL and initially empty.");

    if (series_id == NULL || series_id[0] == '\0' ||
        snapshot_path == NULL || snapshot_path[0] == '\0' ||
        repository_id == NULL || repository_id[0] == '\0' ||
        snapshot_sha == NULL || snapshot_sha[0] == '\0')
        return reject (error,
            "Live ChartSpec v4 factory requires a complete series and pinned repository identity.");

    if (!atm_chart_catalog_validate (error))
        return FALSE;

    const AtmChartCatalogEntry *entry =
        atm_chart_catalog_find_id (series_id);
    if (entry == NULL)
        return reject (error,
            "Live ChartSpec v4 series ID is not present in the compiled catalog.");

    if (g_strcmp0 (repository_id, entry->repository_key) != 0 &&
        g_strcmp0 (repository_id, entry->repository_full_name) != 0)
        return reject (error,
            "Selected repository does not match the catalog-bound series.");

    if (!atm_chart_catalog_snapshot_admitted (entry, snapshot_sha))
        return reject (error,
            "Selected repository snapshot is not admitted for the catalog-bound series.");

    /*
     * Public callers may identify a pinned repo by its stable local key or
     * full name. Source ChartSpec identities use the canonical catalog key,
     * so normalize only after verifying that the caller's repository matches
     * this exact catalog entry.
     */
    const char *canonical_repository_id = entry->repository_key;
    AtmChartSpec *source_spec = NULL;
    gboolean reconstructed = FALSE;

    switch (entry->adapter) {
    case ATM_CHART_CATALOG_ADAPTER_GISTEMP_V1:
        if (repository_version == NULL || repository_version[0] == '\0')
            return reject (error,
                "GISTEMP reconstruction requires the selected repository version.");
        reconstructed = atm_chart_live_reconstruct_gistemp (
            snapshot_path, canonical_repository_id, repository_version,
            snapshot_sha, &source_spec, error);
        break;

    case ATM_CHART_CATALOG_ADAPTER_ENERGY_INSTITUTE_V2:
        reconstructed = atm_chart_live_reconstruct_energy_institute (
            snapshot_path, canonical_repository_id, snapshot_sha, "line",
            &source_spec, error);
        break;

    case ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3:
        reconstructed = atm_chart_live_reconstruct_bnr_bls_c01 (
            snapshot_path, canonical_repository_id, snapshot_sha, "line",
            &source_spec, error);
        break;

    default:
        return reject (error,
            "Catalog entry names an unsupported native chart adapter.");
    }

    if (!reconstructed || source_spec == NULL) {
        atm_chart_spec_free (source_spec);
        if (error != NULL && *error == NULL)
            reject (error,
                "Catalog-selected source adapter could not reconstruct the pinned series.");
        return FALSE;
    }

    if (g_strcmp0 (atm_chart_spec_schema (source_spec),
                   entry->chart_spec_schema) != 0) {
        atm_chart_spec_free (source_spec);
        return reject (error,
            "Reconstructed source ChartSpec schema does not match the catalog adapter contract.");
    }

    AtmChartSpecV4 *spec_v4 = NULL;
    if (!atm_chart_spec_v4_new (entry, source_spec, &spec_v4, error)) {
        atm_chart_spec_free (source_spec);
        return FALSE;
    }

    atm_chart_spec_free (source_spec);
    *out = spec_v4;
    return TRUE;
}

gboolean
atm_chart_wrap_qualified_spec_v4 (
    const char *series_id,
    const AtmChartSpec *qualified_source_spec,
    AtmChartSpecV4 **out,
    GError **error
)
{
    if (out == NULL || *out != NULL)
        return reject (error,
            "ChartSpec v4 wrapper output must be non-NULL and initially empty.");
    if (series_id == NULL || series_id[0] == '\0' ||
        qualified_source_spec == NULL)
        return reject (error,
            "ChartSpec v4 wrapper requires a registered series ID and qualified source spec.");

    if (!atm_chart_catalog_validate (error))
        return FALSE;
    const AtmChartCatalogEntry *entry =
        atm_chart_catalog_find_id (series_id);
    if (entry == NULL)
        return reject (error,
            "ChartSpec v4 wrapper series ID is not present in the compiled catalog.");

    if (g_strcmp0 (atm_chart_spec_schema (qualified_source_spec),
                   entry->chart_spec_schema) != 0)
        return reject (error,
            "Qualified source spec schema does not match the catalog entry.");

    return atm_chart_spec_v4_new (
        entry, qualified_source_spec, out, error);
}


/*
 * Convert a qualified v4 spec into the durable sidecar claims. Pointers in
 * the output are borrowed from the immutable spec/catalog and remain valid
 * while the spec remains alive. No numeric observations or display state are
 * copied to History.
 */
gboolean
atm_chart_history_presentation_input_from_v4 (
    const AtmChartSpecV4 *spec,
    AtmConversationChartPresentationInput *out,
    GError **error
)
{
    if (spec == NULL || out == NULL) {
        return reject (error,
            "History v4 presentation input requires a spec and output.");
    }
    if (!atm_chart_spec_v4_validate (spec, error))
        return FALSE;

    const AtmChartCatalogEntry *entry =
        atm_chart_spec_v4_catalog_entry (spec);
    const AtmChartSpec *source =
        atm_chart_spec_v4_source_spec (spec);
    if (entry == NULL || source == NULL) {
        return reject (error,
            "Qualified v4 spec has no catalog entry or source recipe.");
    }

    const char *missing_policy = NULL;
    if (entry->missing_policy == ATM_CHART_CATALOG_MISSING_REJECT) {
        missing_policy = "reject";
    } else if (entry->missing_policy ==
               ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE) {
        missing_policy = "bounded_profile";
    } else {
        return reject (error,
            "Catalog v4 missing policy is unsupported for History.");
    }

    memset (out, 0, sizeof (*out));
    out->presentation_schema = atm_chart_spec_v4_schema (spec);
    out->presentation_spec_id = atm_chart_spec_v4_id (spec);
    out->series_id = entry->series_id;
    out->catalog_schema = ATM_CHART_CATALOG_SCHEMA;
    out->adapter_id = entry->adapter_id;
    out->series_profile_id = entry->series_profile_id;
    out->series_profile_version = entry->series_profile_version;
    out->admission_profile_id = entry->admission_profile_id;
    out->admission_profile_version = entry->admission_profile_version;
    out->reconstruction_profile_id = entry->reconstruction_profile_id;
    out->source_chart_schema = atm_chart_spec_schema (source);
    out->source_chart_spec_id = atm_chart_spec_id (source);
    out->x_coordinate_semantics = entry->x_coordinate_semantics;
    out->x_frequency = entry->x_frequency;
    out->x_unit = entry->x_unit;
    out->x_dimension = entry->x_dimension;
    out->subject = entry->subject;
    out->attribute = entry->attribute;
    out->y_unit = entry->y_unit;
    out->y_dimension = entry->y_dimension;
    out->series_semantics = entry->series_semantics;
    out->missing_policy = missing_policy;
    out->missing_coordinate = entry->missing_coordinate;
    out->missing_reason = entry->missing_reason;
    out->display_profile_id = entry->display_profile_id;
    out->display_profile_version = entry->display_profile_version;
    out->render_contract = "atm-retro-mono/1";
    return TRUE;
}

static gboolean
history_presentation_inputs_equal (
    const AtmConversationChartPresentationInput *actual,
    const AtmConversationChartPresentationInput *expected
)
{
    if (actual == NULL || expected == NULL)
        return FALSE;
#define SAME_FIELD(name) (g_strcmp0 (actual->name, expected->name) == 0)
    gboolean equal =
        SAME_FIELD (presentation_schema) &&
        SAME_FIELD (presentation_spec_id) &&
        SAME_FIELD (series_id) &&
        SAME_FIELD (catalog_schema) &&
        SAME_FIELD (adapter_id) &&
        SAME_FIELD (series_profile_id) &&
        SAME_FIELD (series_profile_version) &&
        SAME_FIELD (admission_profile_id) &&
        SAME_FIELD (admission_profile_version) &&
        SAME_FIELD (reconstruction_profile_id) &&
        SAME_FIELD (source_chart_schema) &&
        SAME_FIELD (source_chart_spec_id) &&
        SAME_FIELD (x_coordinate_semantics) &&
        SAME_FIELD (x_frequency) &&
        SAME_FIELD (x_unit) &&
        SAME_FIELD (x_dimension) &&
        SAME_FIELD (subject) &&
        SAME_FIELD (attribute) &&
        SAME_FIELD (y_unit) &&
        SAME_FIELD (y_dimension) &&
        SAME_FIELD (series_semantics) &&
        SAME_FIELD (missing_policy) &&
        SAME_FIELD (missing_coordinate) &&
        SAME_FIELD (missing_reason) &&
        SAME_FIELD (display_profile_id) &&
        SAME_FIELD (display_profile_version) &&
        SAME_FIELD (render_contract);
#undef SAME_FIELD
    return equal;
}

/*
 * Rebuild a v4 presentation from the already re-qualified source-specific
 * ChartSpec, then compare every persisted sidecar claim against the freshly
 * derived catalog tuple and semantic v4 identity. A 64-hex ID by itself is
 * never accepted as proof of presentation identity.
 */
gboolean
atm_chart_history_reconstruct_v4 (
    const char *series_id,
    const AtmChartSpec *qualified_source_spec,
    const AtmConversationChartPresentationInput *persisted,
    AtmChartSpecV4 **out,
    GError **error
)
{
    if (out == NULL || *out != NULL ||
        series_id == NULL || series_id[0] == '\0' ||
        qualified_source_spec == NULL || persisted == NULL) {
        return reject (error,
            "History v4 reconstruction requires a registered series, qualified source, sidecar and empty output.");
    }

    AtmChartSpecV4 *rebuilt = NULL;
    if (!atm_chart_wrap_qualified_spec_v4 (
            series_id, qualified_source_spec, &rebuilt, error)) {
        return FALSE;
    }

    AtmConversationChartPresentationInput expected = {0};
    if (!atm_chart_history_presentation_input_from_v4 (
            rebuilt, &expected, error)) {
        atm_chart_spec_v4_free (rebuilt);
        return FALSE;
    }

    if (!history_presentation_inputs_equal (persisted, &expected)) {
        atm_chart_spec_v4_free (rebuilt);
        return reject (error,
            "Persisted History v4 presentation claims differ from the rebuilt catalog-bound spec.");
    }

    *out = rebuilt;
    return TRUE;
}
