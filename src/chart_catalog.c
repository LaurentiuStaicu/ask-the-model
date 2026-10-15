#include "chart_catalog.h"
#include <string.h>

static const char *const gistemp_aliases[] = {
    "gistemp", "global temperature anomaly", "global temperature anomalies",
    "temperature anomaly", "temperatura globala", "temperatura globală",
    "anomalia temperaturii globale", "anomalii ale temperaturii globale"
};
static const char *const ei_aliases[] = {
    "energy-institute", "total energy supply", "global total energy supply", "tes",
    "furnizarea totala de energie", "furnizarea totală de energie",
    "furnizarea totală globală de energie"
};
static const char *const bnr_aliases[] = {
    "bnr-bls-c01", "credit standards net percentage",
    "quarterly credit standards", "net percentage for credit standards",
    "standardele de credit", "procent net privind standardele de credit",
    "standardele de credit pentru companii"
};
static const char *const ewd_snapshots[] = {
    "d9e249339663015f6d1c05752338a955bf64ad0b",
    "dc710d8e604c0fff713a3acfdb18ab47a1a902bb"
};
static const char *const rmd_snapshot[] = {
    "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170"
};
static const char *const line_kind[] = { "line" };

static const AtmChartCatalogEntry entries[] = {
    {
        .series_id = "climate.gistemp.global-temperature-anomaly",
        .display_name = "Global surface temperature anomaly (GISTEMP)",
        .description = "Annual global temperature anomaly relative to the 1951-1980 mean.",
        .aliases = gistemp_aliases, .alias_count = G_N_ELEMENTS (gistemp_aliases),
        .adapter = ATM_CHART_CATALOG_ADAPTER_GISTEMP_V1,
        .adapter_id = "gistemp-pinned-annual/1",
        .series_profile_id = "atm-series/gistemp-complete-annual/1",
        .series_profile_version = "1",
        .admission_profile_id = "atm-gistemp-pinned-admission/1",
        .admission_profile_version = "1",
        .reconstruction_profile_id = "atm-chart-reconstruct/gistemp-complete-annual/1",
        .chart_spec_schema = "atm-chart-spec/1", .history_schema_version = "5",
        .repository_key = "ewd",
        .repository_full_name = "LaurentiuStaicu/empirical-world3-dynamics",
        .snapshot_shas = ewd_snapshots, .snapshot_count = G_N_ELEMENTS (ewd_snapshots),
        .source_path = "science/data/processed/nasa_gistemp_global_2026.csv",
        .x_kind = ATM_CHART_CATALOG_X_CALENDAR_YEAR,
        .x_coordinate_semantics = "calendar_year", .x_frequency = "annual",
        .x_unit = "calendar_year", .x_dimension = "time",
        .subject = "global_surface_temperature", .attribute = "annual_temperature_anomaly",
        .y_unit = "degree_Celsius_anomaly", .y_dimension = "temperature_difference",
        .series_semantics = "empirical_annual_observations",
        .observation_meaning = "Global annual land-ocean temperature anomaly in Celsius relative to 1951-1980.",
        .reference_period = "1951-1980", .geography = "Global",
        .missing_policy = ATM_CHART_CATALOG_MISSING_REJECT,
        .allowed_chart_kinds = line_kind, .allowed_chart_kind_count = 1,
        .display_profile_id = "atm-display/gistemp-linear", .display_profile_version = "1",
        .publisher_name = "NASA GISS / GISTEMP v4",
        .publisher_url = "https://data.giss.nasa.gov/gistemp/data_v4.html",
        .pinned_artifact_description = "Processed GISTEMP CSV in the exact pinned EWD snapshot.",
        .reconstruction_scope = "Reconstructs the pinned processed snapshot; does not independently reproduce NASA's processing."
    },
    {
        .series_id = "energy.energy-institute.global-total-energy-supply",
        .display_name = "Global total energy supply (Energy Institute TES)",
        .description = "Global annual TES using the Physical Energy Content accounting method.",
        .aliases = ei_aliases, .alias_count = G_N_ELEMENTS (ei_aliases),
        .adapter = ATM_CHART_CATALOG_ADAPTER_ENERGY_INSTITUTE_V2,
        .adapter_id = "energy-institute-tes-pinned-annual/1",
        .series_profile_id = "atm-series/energy-institute-tes-complete-annual/1",
        .series_profile_version = "1",
        .admission_profile_id = "atm-energy-institute-pinned-admission/3",
        .admission_profile_version = "3",
        .reconstruction_profile_id = "atm-chart-reconstruct/energy-institute-tes-complete-annual/1",
        .chart_spec_schema = "atm-chart-spec/2", .history_schema_version = "5",
        .repository_key = "ewd",
        .repository_full_name = "LaurentiuStaicu/empirical-world3-dynamics",
        .snapshot_shas = ewd_snapshots, .snapshot_count = G_N_ELEMENTS (ewd_snapshots),
        .source_path = "science/data/processed/energy_institute_global_2026.csv",
        .x_kind = ATM_CHART_CATALOG_X_CALENDAR_YEAR,
        .x_coordinate_semantics = "calendar_year", .x_frequency = "annual",
        .x_unit = "year", .x_dimension = "time",
        .subject = "global_total_energy_supply", .attribute = "total_energy_supply",
        .y_unit = "exajoules/year", .y_dimension = "energy_flow_rate",
        .series_semantics = "empirical_annual_observations",
        .observation_meaning = "Global total energy supply in exajoules/year, Physical Energy Content method.",
        .geography = "Total World", .missing_policy = ATM_CHART_CATALOG_MISSING_REJECT,
        .allowed_chart_kinds = line_kind, .allowed_chart_kind_count = 1,
        .display_profile_id = "atm-display/energy-institute-tes-linear", .display_profile_version = "1",
        .publisher_name = "Energy Institute, Statistical Review of World Energy 2026",
        .publisher_url = "https://www.energyinst.org/statistical-review/resources-and-data-downloads",
        .pinned_artifact_description = "Processed Energy Institute TES CSV in the exact pinned EWD snapshot.",
        .reconstruction_scope = "Reconstructs the pinned processed snapshot; does not independently reproduce publisher processing."
    },
    {
        .series_id = "banking.bnr.bls-c01.credit-standards-net-percentage",
        .display_name = "BNR Bank Lending Survey: credit standards net percentage",
        .description = "Quarterly net percentage for credit standards on loans to non-financial corporations.",
        .aliases = bnr_aliases, .alias_count = G_N_ELEMENTS (bnr_aliases),
        .adapter = ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3,
        .adapter_id = "bnr-bls-c01-pinned-quarterly/1",
        .series_profile_id = "atm-series/bnr-bls-c01-calendar-quarter/1",
        .series_profile_version = "1",
        .admission_profile_id = "atm-bnr-bls-canonical-panel-admission/1",
        .admission_profile_version = "1",
        .reconstruction_profile_id = "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        .chart_spec_schema = "atm-chart-spec/3", .history_schema_version = "5",
        .repository_key = "rmd",
        .repository_full_name = "LaurentiuStaicu/romanian-monetary-dynamics",
        .snapshot_shas = rmd_snapshot, .snapshot_count = G_N_ELEMENTS (rmd_snapshot),
        .source_path = "data/processed/bnr_bls_realised_rounds.csv",
        .x_kind = ATM_CHART_CATALOG_X_CALENDAR_QUARTER,
        .x_coordinate_semantics = "calendar_quarter", .x_frequency = "quarterly",
        .x_unit = "calendar_quarter", .x_dimension = "time",
        .subject = "non_financial_corporations", .attribute = "nfc_credit_standards",
        .y_unit = "percent", .y_dimension = "survey_net_percentage",
        .series_semantics = "empirical_quarterly_observations",
        .observation_meaning = "Positive net percentages indicate tightening; negative values indicate easing.",
        .geography = "Romania",
        .missing_policy = ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE,
        .missing_coordinate = "2025-Q2", .missing_reason = "source_round_not_retained",
        .allowed_chart_kinds = line_kind, .allowed_chart_kind_count = 1,
        .display_profile_id = "atm-display/bnr-bls-c01-quarterly-gap",
        .display_profile_version = "1",
        .publisher_name = "National Bank of Romania, Bank Lending Survey (BLS)",
        .publisher_url = "https://www.bnr.ro/en/12067-bank-lending-survey",
        .pinned_artifact_description = "Processed BNR C01 panel in the exact pinned RMD snapshot.",
        .reconstruction_scope = "Reconstructs the pinned panel; 2025-Q2 is source-authorized missing, not zero."
    }
};

GQuark
atm_chart_catalog_error_quark (void)
{
    return g_quark_from_static_string ("atm-chart-catalog-error-quark");
}

static gboolean
fail (GError **error, AtmChartCatalogError code, const char *message)
{
    g_set_error_literal (error, ATM_CHART_CATALOG_ERROR, code, message);
    return FALSE;
}

static char *
normalize_alias (const char *value)
{
    if (value == NULL || !g_utf8_validate (value, -1, NULL))
        return NULL;
    char *trimmed = g_strdup (value);
    g_strstrip (trimmed);
    if (*trimmed == '\0') {
        g_free (trimmed);
        return NULL;
    }
    char *normal = g_utf8_normalize (trimmed, -1, G_NORMALIZE_DEFAULT_COMPOSE);
    g_free (trimmed);
    if (normal == NULL)
        return NULL;
    char *folded = g_utf8_casefold (normal, -1);
    g_free (normal);
    return folded;
}

static gboolean
valid_sha (const char *sha)
{
    if (sha == NULL || strlen (sha) != 40)
        return FALSE;
    for (guint i = 0; i < 40; i++)
        if (!g_ascii_isxdigit (sha[i]) || (sha[i] >= 'A' && sha[i] <= 'F'))
            return FALSE;
    return TRUE;
}

const AtmChartCatalogEntry *
atm_chart_catalog_entries (gsize *out_count)
{
    if (out_count != NULL)
        *out_count = G_N_ELEMENTS (entries);
    return entries;
}

const AtmChartCatalogEntry *
atm_chart_catalog_find_id (const char *series_id)
{
    if (series_id == NULL)
        return NULL;
    for (gsize i = 0; i < G_N_ELEMENTS (entries); i++)
        if (g_strcmp0 (series_id, entries[i].series_id) == 0)
            return &entries[i];
    return NULL;
}

AtmChartCatalogLookupResult
atm_chart_catalog_find_alias (const char *alias, const AtmChartCatalogEntry **out_entry)
{
    if (out_entry == NULL)
        return ATM_CHART_CATALOG_LOOKUP_INVALID;
    *out_entry = NULL;
    char *key = normalize_alias (alias);
    if (key == NULL)
        return ATM_CHART_CATALOG_LOOKUP_INVALID;
    const AtmChartCatalogEntry *found = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS (entries); i++)
        for (gsize j = 0; j < entries[i].alias_count; j++) {
            char *candidate = normalize_alias (entries[i].aliases[j]);
            gboolean matches = candidate != NULL && g_strcmp0 (key, candidate) == 0;
            g_free (candidate);
            if (matches && found != NULL) {
                g_free (key);
                return ATM_CHART_CATALOG_LOOKUP_AMBIGUOUS;
            }
            if (matches)
                found = &entries[i];
        }
    g_free (key);
    if (found == NULL)
        return ATM_CHART_CATALOG_LOOKUP_NOT_FOUND;
    *out_entry = found;
    return ATM_CHART_CATALOG_LOOKUP_FOUND;
}

gboolean
atm_chart_catalog_snapshot_admitted (const AtmChartCatalogEntry *entry, const char *snapshot_sha)
{
    if (entry == NULL || !valid_sha (snapshot_sha))
        return FALSE;
    for (gsize i = 0; i < entry->snapshot_count; i++)
        if (g_strcmp0 (snapshot_sha, entry->snapshot_shas[i]) == 0)
            return TRUE;
    return FALSE;
}

static gboolean
policy_matches (const AtmChartCatalogEntry *e, GError **error)
{
#define IS(field, value) (g_strcmp0 ((field), (value)) == 0)
    if (e->allowed_chart_kind_count != 1 || e->allowed_chart_kinds == NULL ||
        !IS (e->allowed_chart_kinds[0], "line"))
        return fail (error, ATM_CHART_CATALOG_ERROR_SEMANTICS, "Only the qualified line chart kind is catalogued.");
    if (e->adapter == ATM_CHART_CATALOG_ADAPTER_GISTEMP_V1) {
        if (!IS(e->series_id,"climate.gistemp.global-temperature-anomaly") ||
            !IS(e->adapter_id,"gistemp-pinned-annual/1") ||
            !IS(e->series_profile_id,"atm-series/gistemp-complete-annual/1") ||
            !IS(e->series_profile_version,"1") ||
            !IS(e->admission_profile_id,"atm-gistemp-pinned-admission/1") ||
            !IS(e->admission_profile_version,"1") ||
            !IS(e->reconstruction_profile_id,"atm-chart-reconstruct/gistemp-complete-annual/1") ||
            !IS(e->chart_spec_schema,"atm-chart-spec/1") ||
            !IS(e->history_schema_version,"5") ||
            !IS(e->repository_key,"ewd") ||
            !IS(e->repository_full_name,"LaurentiuStaicu/empirical-world3-dynamics") ||
            !IS(e->source_path,"science/data/processed/nasa_gistemp_global_2026.csv") ||
            !IS(e->x_coordinate_semantics,"calendar_year") ||
            !IS(e->x_frequency,"annual") ||
            !IS(e->x_unit,"calendar_year") || !IS(e->x_dimension,"time") ||
            !IS(e->subject,"global_surface_temperature") ||
            !IS(e->attribute,"annual_temperature_anomaly") ||
            !IS(e->y_unit,"degree_Celsius_anomaly") ||
            !IS(e->y_dimension,"temperature_difference") ||
            !IS(e->series_semantics,"empirical_annual_observations") ||
            !IS(e->reference_period,"1951-1980") || !IS(e->geography,"Global") ||
            !IS(e->display_profile_id,"atm-display/gistemp-linear") ||
            !IS(e->display_profile_version,"1") ||
            !IS(e->publisher_name,"NASA GISS / GISTEMP v4") ||
            !IS(e->publisher_url,"https://data.giss.nasa.gov/gistemp/data_v4.html") ||
            e->x_kind != ATM_CHART_CATALOG_X_CALENDAR_YEAR ||
            e->missing_policy != ATM_CHART_CATALOG_MISSING_REJECT ||
            e->missing_coordinate != NULL || e->missing_reason != NULL ||
            e->snapshot_count != 2 ||
            !atm_chart_catalog_snapshot_admitted(e,"d9e249339663015f6d1c05752338a955bf64ad0b") ||
            !atm_chart_catalog_snapshot_admitted(e,"dc710d8e604c0fff713a3acfdb18ab47a1a902bb"))
            return fail(error, ATM_CHART_CATALOG_ERROR_SOURCE_POLICY, "GISTEMP catalog tuple differs from the reviewed adapter.");
        return TRUE;
    }
    if (e->adapter == ATM_CHART_CATALOG_ADAPTER_ENERGY_INSTITUTE_V2) {
        if (!IS(e->series_id,"energy.energy-institute.global-total-energy-supply") ||
            !IS(e->adapter_id,"energy-institute-tes-pinned-annual/1") ||
            !IS(e->series_profile_id,"atm-series/energy-institute-tes-complete-annual/1") ||
            !IS(e->series_profile_version,"1") ||
            !IS(e->admission_profile_id,"atm-energy-institute-pinned-admission/3") ||
            !IS(e->admission_profile_version,"3") ||
            !IS(e->reconstruction_profile_id,"atm-chart-reconstruct/energy-institute-tes-complete-annual/1") ||
            !IS(e->chart_spec_schema,"atm-chart-spec/2") ||
            !IS(e->history_schema_version,"5") ||
            !IS(e->repository_key,"ewd") ||
            !IS(e->repository_full_name,"LaurentiuStaicu/empirical-world3-dynamics") ||
            !IS(e->source_path,"science/data/processed/energy_institute_global_2026.csv") ||
            !IS(e->x_coordinate_semantics,"calendar_year") ||
            !IS(e->x_frequency,"annual") || !IS(e->x_unit,"year") ||
            !IS(e->x_dimension,"time") ||
            !IS(e->subject,"global_total_energy_supply") ||
            !IS(e->attribute,"total_energy_supply") ||
            !IS(e->y_unit,"exajoules/year") || !IS(e->y_dimension,"energy_flow_rate") ||
            !IS(e->series_semantics,"empirical_annual_observations") ||
            !IS(e->geography,"Total World") ||
            !IS(e->display_profile_id,"atm-display/energy-institute-tes-linear") ||
            !IS(e->display_profile_version,"1") ||
            !IS(e->publisher_name,"Energy Institute, Statistical Review of World Energy 2026") ||
            !IS(e->publisher_url,"https://www.energyinst.org/statistical-review/resources-and-data-downloads") ||
            e->x_kind != ATM_CHART_CATALOG_X_CALENDAR_YEAR ||
            e->missing_policy != ATM_CHART_CATALOG_MISSING_REJECT ||
            e->missing_coordinate != NULL || e->missing_reason != NULL ||
            e->snapshot_count != 2 ||
            !atm_chart_catalog_snapshot_admitted(e,"d9e249339663015f6d1c05752338a955bf64ad0b") ||
            !atm_chart_catalog_snapshot_admitted(e,"dc710d8e604c0fff713a3acfdb18ab47a1a902bb"))
            return fail(error, ATM_CHART_CATALOG_ERROR_SOURCE_POLICY, "Energy Institute catalog tuple differs from the reviewed adapter.");
        return TRUE;
    }
    if (e->adapter == ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3) {
        if (!IS(e->series_id,"banking.bnr.bls-c01.credit-standards-net-percentage") ||
            !IS(e->adapter_id,"bnr-bls-c01-pinned-quarterly/1") ||
            !IS(e->series_profile_id,"atm-series/bnr-bls-c01-calendar-quarter/1") ||
            !IS(e->series_profile_version,"1") ||
            !IS(e->admission_profile_id,"atm-bnr-bls-canonical-panel-admission/1") ||
            !IS(e->admission_profile_version,"1") ||
            !IS(e->reconstruction_profile_id,"atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1") ||
            !IS(e->chart_spec_schema,"atm-chart-spec/3") ||
            !IS(e->history_schema_version,"5") ||
            !IS(e->repository_key,"rmd") ||
            !IS(e->repository_full_name,"LaurentiuStaicu/romanian-monetary-dynamics") ||
            !IS(e->source_path,"data/processed/bnr_bls_realised_rounds.csv") ||
            !IS(e->x_coordinate_semantics,"calendar_quarter") ||
            !IS(e->x_frequency,"quarterly") || !IS(e->x_unit,"calendar_quarter") ||
            !IS(e->x_dimension,"time") ||
            !IS(e->subject,"non_financial_corporations") ||
            !IS(e->attribute,"nfc_credit_standards") ||
            !IS(e->y_unit,"percent") || !IS(e->y_dimension,"survey_net_percentage") ||
            !IS(e->series_semantics,"empirical_quarterly_observations") ||
            !IS(e->geography,"Romania") ||
            !IS(e->missing_coordinate,"2025-Q2") ||
            !IS(e->missing_reason,"source_round_not_retained") ||
            !IS(e->display_profile_id,"atm-display/bnr-bls-c01-quarterly-gap") ||
            !IS(e->display_profile_version,"1") ||
            !IS(e->publisher_name,"National Bank of Romania, Bank Lending Survey (BLS)") ||
            !IS(e->publisher_url,"https://www.bnr.ro/en/12067-bank-lending-survey") ||
            e->x_kind != ATM_CHART_CATALOG_X_CALENDAR_QUARTER ||
            e->missing_policy != ATM_CHART_CATALOG_MISSING_BOUNDED_PROFILE ||
            e->snapshot_count != 1 ||
            !atm_chart_catalog_snapshot_admitted(e,"e43eb4e899f123d3b1aa0f6e20b7c8ed35434170"))
            return fail(error, ATM_CHART_CATALOG_ERROR_SOURCE_POLICY, "BNR catalog tuple differs from the reviewed adapter.");
        return TRUE;
    }
#undef IS
    return fail (error, ATM_CHART_CATALOG_ERROR_PROFILE, "Catalog references an unknown adapter.");
}

gboolean
atm_chart_catalog_validate (GError **error)
{
    if (G_N_ELEMENTS (entries) != ATM_CHART_CATALOG_EXPECTED_ENTRIES)
        return fail (error, ATM_CHART_CATALOG_ERROR_SHAPE, "Catalog v1 must contain exactly the reviewed three entries.");

    for (gsize i = 0; i < G_N_ELEMENTS (entries); i++) {
        const AtmChartCatalogEntry *e = &entries[i];
        const char *required[] = {
            e->series_id, e->display_name, e->description, e->adapter_id,
            e->series_profile_id, e->series_profile_version,
            e->admission_profile_id, e->admission_profile_version,
            e->reconstruction_profile_id, e->chart_spec_schema,
            e->history_schema_version, e->repository_key, e->repository_full_name,
            e->source_path, e->x_coordinate_semantics, e->x_frequency, e->x_unit,
            e->x_dimension, e->subject, e->attribute, e->y_unit, e->y_dimension,
            e->series_semantics, e->observation_meaning, e->geography,
            e->display_profile_id, e->display_profile_version, e->publisher_name,
            e->publisher_url, e->pinned_artifact_description, e->reconstruction_scope
        };
        for (gsize j = 0; j < G_N_ELEMENTS (required); j++)
            if (required[j] == NULL || *required[j] == '\0' ||
                strlen(required[j]) > ATM_CHART_CATALOG_MAX_TEXT_BYTES ||
                !g_utf8_validate(required[j], -1, NULL))
                return fail (error, ATM_CHART_CATALOG_ERROR_SHAPE, "Catalog metadata is missing, invalid UTF-8 or over the text limit.");

        if (e->aliases == NULL || e->alias_count == 0 ||
            e->alias_count > ATM_CHART_CATALOG_MAX_ALIASES_PER_ENTRY ||
            e->snapshot_shas == NULL || e->snapshot_count == 0 ||
            e->snapshot_count > ATM_CHART_CATALOG_MAX_SNAPSHOTS_PER_ENTRY ||
            e->allowed_chart_kinds == NULL || e->allowed_chart_kind_count == 0)
            return fail (error, ATM_CHART_CATALOG_ERROR_LIMIT, "Catalog collection count is outside its bound.");

        if (e->source_path[0] == '/' || strstr(e->source_path, "..") != NULL ||
            !g_str_has_prefix(e->publisher_url, "https://"))
            return fail (error, ATM_CHART_CATALOG_ERROR_SHAPE, "Catalog contains unsafe source path or publisher URL.");

        for (gsize j = 0; j < e->snapshot_count; j++)
            if (!valid_sha(e->snapshot_shas[j]))
                return fail (error, ATM_CHART_CATALOG_ERROR_SOURCE_POLICY, "Catalog contains a non-canonical snapshot SHA.");

        for (gsize j = 0; j < i; j++)
            if (g_strcmp0 (e->series_id, entries[j].series_id) == 0)
                return fail (error, ATM_CHART_CATALOG_ERROR_DUPLICATE_ID, "Catalog repeats a stable series ID.");

        for (gsize j = 0; j < e->alias_count; j++) {
            char *a = normalize_alias(e->aliases[j]);
            if (a == NULL || strlen(a) > ATM_CHART_CATALOG_MAX_TEXT_BYTES) {
                g_free(a);
                return fail(error, ATM_CHART_CATALOG_ERROR_SHAPE, "Catalog contains an invalid or oversized alias.");
            }
            for (gsize k = 0; k < i + 1; k++) {
                const AtmChartCatalogEntry *other = &entries[k];
                gsize limit = k == i ? j : other->alias_count;
                for (gsize n = 0; n < limit; n++) {
                    char *b = normalize_alias(other->aliases[n]);
                    gboolean duplicate = b != NULL && g_strcmp0(a,b) == 0;
                    g_free(b);
                    if (duplicate) {
                        g_free(a);
                        return fail(error, ATM_CHART_CATALOG_ERROR_DUPLICATE_ALIAS, "Catalog alias resolves to more than one series.");
                    }
                }
            }
            g_free(a);
        }
        if (!policy_matches(e, error)) return FALSE;
    }
    return TRUE;
}
