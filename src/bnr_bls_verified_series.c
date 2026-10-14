#include "bnr_bls_verified_series.h"

#include <string.h>

#define BNR_SUBJECT "non_financial_corporations"
#define BNR_ATTRIBUTE "nfc_credit_standards"
#define BNR_UNIT "percent"
#define BNR_DIMENSION "survey_net_percentage"
#define BNR_SEMANTICS "empirical_quarterly_observations"
#define BNR_SCENARIO "not_applicable"
#define BNR_EPISTEMIC_STATUS "empirical"
#define BNR_TIME_SCOPE "2022-Q4..2025-Q3"
#define BNR_SOURCE_PATH "data/processed/bnr_bls_realised_rounds.csv"

static const char *const expected_quarters[] = {
    "2022-Q4", "2023-Q1", "2023-Q2", "2023-Q3",
    "2023-Q4", "2024-Q1", "2024-Q2", "2024-Q3",
    "2024-Q4", "2025-Q1", "2025-Q2", "2025-Q3"
};

struct AtmBnrBlsVerifiedSeries {
    AtmBnrBlsSeriesContract *contract;
};

GQuark
atm_bnr_bls_verified_series_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-bnr-bls-verified-series-error-quark"
    );
}

static gboolean
reject (GError **error,
        AtmBnrBlsVerifiedSeriesError code,
        const char *message)
{
    g_set_error_literal (
        error,
        ATM_BNR_BLS_VERIFIED_SERIES_ERROR,
        code,
        message
    );
    return FALSE;
}

static gboolean
same (const char *a, const char *b)
{
    return g_strcmp0 (a, b) == 0;
}

static gboolean
profile_shape_valid (const AtmBnrBlsSeriesContract *series,
                     GError **error)
{
    if (series == NULL ||
        !atm_bnr_bls_series_contract_validate (series, error))
        return FALSE;

    const AtmSeriesContract *contract =
        atm_bnr_bls_series_contract_value (series);
    if (contract == NULL ||
        contract->x_kind != ATM_SERIES_X_CALENDAR_QUARTER ||
        !same (contract->subject, BNR_SUBJECT) ||
        !same (contract->attribute, BNR_ATTRIBUTE) ||
        !same (contract->x_unit, "calendar_quarter") ||
        !same (contract->x_dimension, "time") ||
        !same (contract->y_unit, BNR_UNIT) ||
        !same (contract->y_dimension, BNR_DIMENSION) ||
        !same (contract->series_semantics, BNR_SEMANTICS) ||
        !same (contract->scenario, BNR_SCENARIO) ||
        !same (contract->time_scope, BNR_TIME_SCOPE) ||
        !same (contract->epistemic_status, BNR_EPISTEMIC_STATUS) ||
        !same (contract->repository_id, "rmd") ||
        !same (contract->repository_version,
               "snapshot:" ATM_BNR_BLS_SNAPSHOT_SHA) ||
        !same (contract->snapshot_sha, ATM_BNR_BLS_SNAPSHOT_SHA) ||
        !same (contract->source_path, BNR_SOURCE_PATH) ||
        !same (contract->profile_id, ATM_BNR_BLS_VERIFIED_SERIES_PROFILE) ||
        !same (contract->profile_version, "1") ||
        contract->points == NULL ||
        contract->points->len != G_N_ELEMENTS (expected_quarters) ||
        contract->scientific_content_id == NULL ||
        contract->qualified_series_id == NULL)
        return reject (
            error,
            ATM_BNR_BLS_VERIFIED_SERIES_ERROR_PROFILE,
            "BNR VerifiedSeries source/profile tuple is unsupported."
        );

    for (guint i = 0; i < G_N_ELEMENTS (expected_quarters); i++) {
        const AtmSeriesContractPoint *point =
            g_ptr_array_index (contract->points, i);
        if (point == NULL || point->order != i ||
            !same (point->x, expected_quarters[i]) ||
            point->break_before ||
            point->break_reason != NULL ||
            point->support == NULL || point->support->len == 0)
            return reject (
                error,
                ATM_BNR_BLS_VERIFIED_SERIES_ERROR_PROFILE,
                "BNR VerifiedSeries contains an unexpected quarter, break, or support."
            );

        if (i == 10u) {
            if (point->y_status != ATM_SERIES_Y_MISSING ||
                point->source_decimal != NULL ||
                point->coefficient != NULL ||
                point->exponent != 0 ||
                !same (point->missing_reason, ATM_BNR_BLS_MISSING_REASON) ||
                point->support->len != ATM_BNR_BLS_SOURCE_COUNT)
                return reject (
                    error,
                    ATM_BNR_BLS_VERIFIED_SERIES_ERROR_PROFILE,
                    "BNR VerifiedSeries does not preserve the authorized 2025-Q2 absence."
                );
        } else if (point->y_status != ATM_SERIES_Y_NUMERIC ||
                   point->source_decimal == NULL ||
                   point->coefficient == NULL ||
                   point->missing_reason != NULL) {
            return reject (
                error,
                ATM_BNR_BLS_VERIFIED_SERIES_ERROR_PROFILE,
                "BNR VerifiedSeries contains an unexpected non-numeric observation."
            );
        }
    }

    return TRUE;
}

gboolean
atm_bnr_bls_verified_series_from_contract (
    const AtmBnrBlsSeriesContract *input,
    AtmBnrBlsVerifiedSeries **out,
    GError **error)
{
    if (input == NULL || out == NULL || *out != NULL)
        return reject (
            error,
            ATM_BNR_BLS_VERIFIED_SERIES_ERROR_ARGUMENT,
            "Invalid BNR VerifiedSeries construction arguments."
        );

    AtmBnrBlsSeriesContract *owned = NULL;
    if (!atm_bnr_bls_series_contract_rebuild (input, &owned, error))
        return FALSE;

    if (!profile_shape_valid (owned, error)) {
        atm_bnr_bls_series_contract_free (owned);
        return FALSE;
    }

    AtmBnrBlsVerifiedSeries *series =
        g_new0 (AtmBnrBlsVerifiedSeries, 1);
    series->contract = owned;
    *out = series;
    return TRUE;
}

gboolean
atm_bnr_bls_verified_series_validate (
    const AtmBnrBlsVerifiedSeries *series,
    GError **error)
{
    if (series == NULL || series->contract == NULL)
        return reject (
            error,
            ATM_BNR_BLS_VERIFIED_SERIES_ERROR_ARGUMENT,
            "Invalid BNR VerifiedSeries."
        );

    return profile_shape_valid (series->contract, error);
}

gboolean
atm_bnr_bls_verified_series_rebuild (
    const AtmBnrBlsVerifiedSeries *series,
    AtmBnrBlsVerifiedSeries **out,
    GError **error)
{
    if (out == NULL || *out != NULL)
        return reject (
            error,
            ATM_BNR_BLS_VERIFIED_SERIES_ERROR_ARGUMENT,
            "BNR VerifiedSeries rebuild output must be NULL."
        );
    if (!atm_bnr_bls_verified_series_validate (series, error))
        return FALSE;

    return atm_bnr_bls_verified_series_from_contract (
        series->contract, out, error
    );
}

void
atm_bnr_bls_verified_series_free (AtmBnrBlsVerifiedSeries *series)
{
    if (series == NULL)
        return;
    atm_bnr_bls_series_contract_free (series->contract);
    g_free (series);
}

const AtmBnrBlsSeriesContract *
atm_bnr_bls_verified_series_contract (
    const AtmBnrBlsVerifiedSeries *series)
{
    return series != NULL ? series->contract : NULL;
}

guint
atm_bnr_bls_verified_series_count (
    const AtmBnrBlsVerifiedSeries *series)
{
    return series != NULL
        ? atm_bnr_bls_series_contract_count (series->contract)
        : 0u;
}

const AtmSeriesContractPoint *
atm_bnr_bls_verified_series_point (
    const AtmBnrBlsVerifiedSeries *series,
    guint index)
{
    return series != NULL
        ? atm_bnr_bls_series_contract_point (series->contract, index)
        : NULL;
}

guint
atm_bnr_bls_verified_series_source_row (
    const AtmBnrBlsVerifiedSeries *series,
    guint index)
{
    return series != NULL
        ? atm_bnr_bls_series_contract_source_row (series->contract, index)
        : 0u;
}

const char *
atm_bnr_bls_verified_series_scientific_id (
    const AtmBnrBlsVerifiedSeries *series)
{
    return series != NULL
        ? atm_bnr_bls_series_contract_scientific_id (series->contract)
        : NULL;
}

const char *
atm_bnr_bls_verified_series_qualified_id (
    const AtmBnrBlsVerifiedSeries *series)
{
    return series != NULL
        ? atm_bnr_bls_series_contract_qualified_id (series->contract)
        : NULL;
}
