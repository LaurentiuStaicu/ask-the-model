#include "bnr_bls_sra.h"

#include "scientific_artifact.h"
#include "scientific_canonical.h"

#include <json-glib/json-glib.h>
#include <string.h>

#define BNR_REPOSITORY_ID "rmd"
#define BNR_PROFILE_VERSION "1"
#define BNR_SRA_OBLIGATION "atm-bnr-bls-c01-source-bound-reconstruction/1"
#define BNR_SEMANTIC_PROFILE ATM_BNR_BLS_PROFILE "@1"
#define BNR_NUMERIC_PROFILE "exact_decimal_coefficient_exponent/1"
#define BNR_SUBJECT "non_financial_corporations"
#define BNR_ATTRIBUTE "nfc_credit_standards"
#define BNR_UNIT "percent"
#define BNR_DIMENSION "survey_net_percentage"
#define BNR_SCENARIO "not_applicable"
#define BNR_EPISTEMIC_STATUS "empirical"

static const char *const source_roles[ATM_BNR_BLS_SOURCE_COUNT] = {
    "panel",
    "inventory",
    "canonical_panel_promotion_review",
    "cross_round_mapping_audit",
    "missing_round_semantic_mapping_audit"
};

static const char *const limitations[] = {
    "BNR BLS C01 net-percentage survey responses are not measured loan quantities or credit-growth data.",
    "The authorized 2025-Q2 absence has reason source_round_not_retained; no numeric observation or CSV row is manufactured.",
    "Blank reference_date cells in 2023-Q2 and 2024-Q2 are ancillary metadata and do not remove their observed numeric responses.",
    "No interpolation, forward-fill, synthetic quarter/date, parameter/model/lag selection, holdout opening, causal claim, or System Dynamics/behavioural activation is authorized.",
    "The SRA reconstructs exact pinned processed bytes; it does not reproduce the upstream BNR workbook extraction pipeline.",
    "This SRA is a native qualification artifact only; it grants no production route, SeriesContract, VerifiedSeries, chart, History, estimation, or release authority.",
    NULL
};

struct AtmBnrBlsSra {
    AtmBnrBlsPanel *panel;
    AtmSraResult *result;
};

GQuark
atm_bnr_bls_sra_error_quark (void)
{
    return g_quark_from_static_string ("atm-bnr-bls-sra-error-quark");
}

static gboolean
reject (GError **error, AtmBnrBlsSraError code, const char *message)
{
    g_set_error_literal (error, ATM_BNR_BLS_SRA_ERROR, code, message);
    return FALSE;
}

static void
add_string (JsonBuilder *builder, const char *key, const char *value)
{
    json_builder_set_member_name (builder, key);
    json_builder_add_string_value (builder, value);
}

static void
add_text (JsonBuilder *builder, const char *key, const char *value)
{
    json_builder_set_member_name (builder, key);
    if (value != NULL)
        json_builder_add_string_value (builder, value);
    else
        json_builder_add_null_value (builder);
}

static void
add_string_array (JsonBuilder *builder, const char *key, const char *const *values, guint count)
{
    json_builder_set_member_name (builder, key);
    json_builder_begin_array (builder);
    for (guint i = 0; i < count; i++)
        json_builder_add_string_value (builder, values[i]);
    json_builder_end_array (builder);
}

static char *
finish_payload (JsonBuilder *builder)
{
    json_builder_end_object (builder);
    JsonNode *root = json_builder_get_root (builder);
    char *payload = json_to_string (root, FALSE);
    json_node_free (root);
    g_object_unref (builder);
    return payload;
}

static char *
source_binding_payload (const AtmBnrBlsPanel *panel, guint index)
{
    JsonBuilder *builder = json_builder_new ();
    json_builder_begin_object (builder);
    add_string (builder, "schema", "atm-bnr-bls-source-binding/1");
    add_string (builder, "repository", ATM_BNR_BLS_UPSTREAM_REPOSITORY);
    add_string (builder, "snapshot_sha", atm_bnr_bls_panel_snapshot (panel));
    add_string (builder, "source_path", atm_bnr_bls_panel_source_path (panel, index));
    add_string (builder, "source_sha256", atm_bnr_bls_panel_source_sha256 (panel, index));
    add_string (builder, "role", source_roles[index]);
    return finish_payload (builder);
}

static char *
point_payload (const AtmBnrBlsObservation *point)
{
    JsonBuilder *builder = json_builder_new ();
    char *source_row = g_strdup_printf ("%u", point->source_row);
    char *exponent = g_strdup_printf ("%" G_GINT64_FORMAT, point->exponent);
    json_builder_begin_object (builder);
    add_string (builder, "schema", "atm-bnr-bls-c01-observation/1");
    add_string (builder, "subject", BNR_SUBJECT);
    add_string (builder, "attribute", BNR_ATTRIBUTE);
    add_string (builder, "metric_label", "Net percentage");
    add_string (builder, "calendar_quarter", point->quarter);
    add_string (builder, "y_status", "NUMERIC");
    add_string (builder, "source_decimal", point->source_decimal);
    add_string (builder, "coefficient", point->coefficient);
    add_string (builder, "exponent", exponent);
    add_string (builder, "numeric_profile", BNR_NUMERIC_PROFILE);
    add_string (builder, "unit", BNR_UNIT);
    add_string (builder, "dimension", BNR_DIMENSION);
    add_string (builder, "scenario", BNR_SCENARIO);
    add_string (builder, "epistemic_status", BNR_EPISTEMIC_STATUS);
    add_string (builder, "source_row", source_row);
    add_string (builder, "source_id", point->source_id);
    add_string (builder, "source_kind", point->source_kind);
    add_text (builder, "reference_date", point->reference_date);
    add_text (builder, "missing_reason", NULL);
    json_builder_set_member_name (builder, "break_before");
    json_builder_add_boolean_value (builder, FALSE);
    add_text (builder, "break_reason", NULL);
    g_free (source_row);
    g_free (exponent);
    return finish_payload (builder);
}

static char *
missing_payload (const AtmBnrBlsPanel *panel)
{
    const char *paths[ATM_BNR_BLS_SOURCE_COUNT];
    const char *digests[ATM_BNR_BLS_SOURCE_COUNT];
    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        paths[i] = atm_bnr_bls_panel_source_path (panel, i);
        digests[i] = atm_bnr_bls_panel_source_sha256 (panel, i);
    }

    JsonBuilder *builder = json_builder_new ();
    json_builder_begin_object (builder);
    add_string (builder, "schema", "atm-bnr-bls-c01-missing-observation/1");
    add_string (builder, "subject", BNR_SUBJECT);
    add_string (builder, "attribute", BNR_ATTRIBUTE);
    add_string (builder, "metric_label", "Net percentage");
    add_string (builder, "calendar_quarter", ATM_BNR_BLS_MISSING_QUARTER);
    add_string (builder, "y_status", "MISSING");
    add_string (builder, "missing_reason", ATM_BNR_BLS_MISSING_REASON);
    add_string (builder, "unit", BNR_UNIT);
    add_string (builder, "dimension", BNR_DIMENSION);
    add_string (builder, "scenario", BNR_SCENARIO);
    add_string (builder, "epistemic_status", BNR_EPISTEMIC_STATUS);
    json_builder_set_member_name (builder, "break_before");
    json_builder_add_boolean_value (builder, FALSE);
    add_text (builder, "break_reason", NULL);
    add_string_array (builder, "authority_source_paths", paths, ATM_BNR_BLS_SOURCE_COUNT);
    add_string_array (builder, "authority_source_sha256", digests, ATM_BNR_BLS_SOURCE_COUNT);
    add_string (builder, "absence_contract", "authorized_absence_without_csv_row/1");
    return finish_payload (builder);
}

static AtmScientificArtifact *
make_artifact (
    const AtmBnrBlsPanel *panel,
    guint source_index,
    const char *semantic_type,
    const char *logical_source_id,
    const char *locator,
    char *payload,
    GError **error
)
{
    AtmScientificEvidenceAtom evidence = {0};
    AtmScientificArtifact *artifact = NULL;
    const char *snapshot = atm_bnr_bls_panel_snapshot (panel);
    char *version = g_strconcat ("snapshot:", snapshot, NULL);

    evidence.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    evidence.profile_id = (char *) ATM_BNR_BLS_PROFILE;
    evidence.profile_version = (char *) BNR_PROFILE_VERSION;
    evidence.repository_id = (char *) BNR_REPOSITORY_ID;
    evidence.repository_version = version;
    evidence.snapshot_sha = (char *) snapshot;
    evidence.source_path = (char *) atm_bnr_bls_panel_source_path (panel, source_index);
    evidence.locator = (char *) locator;
    evidence.logical_source_id = (char *) logical_source_id;
    evidence.semantic_type = (char *) semantic_type;
    evidence.raw_payload = payload;

    gboolean ok = atm_scientific_artifact_from_evidence (&evidence, &artifact, error);
    g_free (version);
    g_free (payload);
    return ok ? artifact : NULL;
}

static AtmScientificArtifact *
make_source_artifact (
    const AtmBnrBlsPanel *panel,
    guint index,
    GError **error
)
{
    char *logical_id = g_strdup_printf (
        "rmd:bnr-bls-source:%s",
        atm_bnr_bls_panel_source_path (panel, index)
    );
    AtmScientificArtifact *artifact = make_artifact (
        panel,
        index,
        "rmd.bnr_bls_source_binding",
        logical_id,
        "file:complete",
        source_binding_payload (panel, index),
        error
    );
    g_free (logical_id);
    return artifact;
}

static AtmScientificArtifact *
make_point_artifact (
    const AtmBnrBlsPanel *panel,
    const AtmBnrBlsObservation *point,
    GError **error
)
{
    char *logical_id = g_strdup_printf ("rmd:bnr-bls-c01:%s", point->quarter);
    char *locator = g_strdup_printf (
        "csv:row:%u:columns:quarter,reference_date,source_id,source_kind,nfc_credit_standards",
        point->source_row
    );
    AtmScientificArtifact *artifact = make_artifact (
        panel,
        ATM_BNR_BLS_SOURCE_PANEL,
        "rmd.bnr_bls_c01_observation",
        logical_id,
        locator,
        point_payload (point),
        error
    );
    g_free (logical_id);
    g_free (locator);
    return artifact;
}

static gboolean
add_support_to_fact (
    AtmSraEstablishedFact *fact,
    AtmScientificArtifact *const *sources,
    guint source_count,
    AtmScientificArtifact *point,
    GError **error
)
{
    for (guint i = 0; i < source_count; i++) {
        if (!atm_sra_fact_add_support (fact, sources[i]->qualified_artifact_id, error))
            return FALSE;
    }
    if (point != NULL &&
        !atm_sra_fact_add_support (fact, point->qualified_artifact_id, error))
        return FALSE;
    return TRUE;
}

static gboolean
add_qualification_metadata (
    AtmSraResult *result,
    const AtmBnrBlsPanel *panel,
    GError **error
)
{
    const char *snapshot_sha = atm_bnr_bls_panel_snapshot (panel);
    char *snapshot = g_strdup_printf (
        "%s@snapshot:%s#%s",
        BNR_REPOSITORY_ID,
        snapshot_sha,
        snapshot_sha
    );
    gboolean ok =
        atm_sra_qualification_add_repository_snapshot (result, snapshot, error) &&
        atm_sra_qualification_add_canonical_obligation (result, ATM_SRA_SCHEMA_ID, error) &&
        atm_sra_qualification_add_canonical_obligation (result, BNR_SRA_OBLIGATION, error) &&
        atm_sra_qualification_add_semantic_profile (result, BNR_SEMANTIC_PROFILE, error) &&
        atm_sra_qualification_set_numeric_profile (result, BNR_NUMERIC_PROFILE, error);
    g_free (snapshot);
    for (guint i = 0; ok && limitations[i] != NULL; i++)
        ok = atm_sra_result_add_limitation (result, limitations[i], error);
    return ok;
}

static gboolean
build_result (
    const AtmBnrBlsPanel *panel,
    AtmSraResult **out_result,
    GError **error
)
{
    AtmSraResult *result = NULL;
    GPtrArray *owned_artifacts = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_scientific_artifact_free
    );
    AtmScientificArtifact *source_artifacts[ATM_BNR_BLS_SOURCE_COUNT] = { NULL };
    AtmScientificArtifact *point_artifacts[11] = { NULL };
    gboolean ok = FALSE;

    if (out_result == NULL || *out_result != NULL) {
        reject (error, ATM_BNR_BLS_SRA_ERROR_ARGUMENT, "Invalid SRA result output.");
        goto out;
    }
    if (!atm_bnr_bls_panel_validate (panel, error))
        goto out;

    result = atm_sra_result_new (ATM_SRA_PARTIAL);
    if (result == NULL || !add_qualification_metadata (result, panel, error))
        goto out;

    for (guint i = 0; i < ATM_BNR_BLS_SOURCE_COUNT; i++) {
        source_artifacts[i] = make_source_artifact (panel, i, error);
        if (source_artifacts[i] == NULL)
            goto out;
        g_ptr_array_add (owned_artifacts, source_artifacts[i]);
        if (!atm_sra_qualification_add_evidence_artifact (result, source_artifacts[i], error))
            goto out;
    }

    if (atm_bnr_bls_panel_observation_count (panel) != G_N_ELEMENTS (point_artifacts)) {
        reject (error, ATM_BNR_BLS_SRA_ERROR_ADMISSION, "Admitted BNR panel has unexpected observation count.");
        goto out;
    }

    for (guint i = 0; i < G_N_ELEMENTS (point_artifacts); i++) {
        AtmBnrBlsObservation point = {0};
        if (!atm_bnr_bls_panel_observation (panel, i, &point)) {
            reject (error, ATM_BNR_BLS_SRA_ERROR_ADMISSION, "Admitted BNR panel point is unavailable.");
            goto out;
        }
        point_artifacts[i] = make_point_artifact (panel, &point, error);
        if (point_artifacts[i] == NULL)
            goto out;
        g_ptr_array_add (owned_artifacts, point_artifacts[i]);
        if (!atm_sra_qualification_add_evidence_artifact (result, point_artifacts[i], error))
            goto out;

        char *value = g_strdup_printf (
            "%se%" G_GINT64_FORMAT,
            point.coefficient,
            point.exponent
        );
        AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
            point_artifacts[i]->logical_source_id,
            "rmd.bnr_bls_c01_observation",
            BNR_SUBJECT,
            BNR_ATTRIBUTE,
            value,
            BNR_UNIT,
            BNR_DIMENSION,
            point_artifacts[i]->payload
        );
        g_free (value);
        if (fact == NULL) {
            reject (error, ATM_BNR_BLS_SRA_ERROR_IDENTITY, "Cannot construct an admitted BNR numeric fact.");
            goto out;
        }
        if (!add_support_to_fact (fact, source_artifacts, ATM_BNR_BLS_SOURCE_COUNT, point_artifacts[i], error) ||
            !atm_sra_result_add_fact (result, fact, error)) {
            atm_sra_established_fact_free (fact);
            goto out;
        }
    }

    /* The missing quarter is an SRA fact backed by the five pinned authority
     * artifacts. It intentionally has no observation artifact or CSV row. */
    {
        char *payload = missing_payload (panel);
        AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
            "rmd:bnr-bls-c01:2025-Q2",
            "rmd.bnr_bls_c01_missing_observation",
            BNR_SUBJECT,
            BNR_ATTRIBUTE,
            "MISSING",
            BNR_UNIT,
            BNR_DIMENSION,
            payload
        );
        g_free (payload);
        if (fact == NULL) {
            reject (error, ATM_BNR_BLS_SRA_ERROR_IDENTITY, "Cannot construct the authorized BNR missing fact.");
            goto out;
        }
        if (!add_support_to_fact (fact, source_artifacts, ATM_BNR_BLS_SOURCE_COUNT, NULL, error) ||
            !atm_sra_result_add_fact (result, fact, error)) {
            atm_sra_established_fact_free (fact);
            goto out;
        }
    }

    if (!atm_sra_result_finalize (result, error))
        goto out;

    *out_result = result;
    result = NULL;
    ok = TRUE;

out:
    g_clear_pointer (&result, atm_sra_result_free);
    g_ptr_array_unref (owned_artifacts);
    return ok;
}

void
atm_bnr_bls_sra_free (AtmBnrBlsSra *sra)
{
    if (sra == NULL)
        return;
    g_clear_pointer (&sra->panel, atm_bnr_bls_panel_free);
    g_clear_pointer (&sra->result, atm_sra_result_free);
    g_free (sra);
}

gboolean
atm_bnr_bls_sra_new (
    const AtmBnrBlsPanel *panel,
    AtmBnrBlsSra **out_sra,
    GError **error
)
{
    if (panel == NULL || out_sra == NULL || *out_sra != NULL)
        return reject (error, ATM_BNR_BLS_SRA_ERROR_ARGUMENT, "Invalid BNR SRA arguments.");

    AtmBnrBlsSra *sra = g_new0 (AtmBnrBlsSra, 1);
    if (!atm_bnr_bls_panel_rebuild (panel, &sra->panel, error) ||
        !build_result (sra->panel, &sra->result, error)) {
        atm_bnr_bls_sra_free (sra);
        return FALSE;
    }

    *out_sra = sra;
    return TRUE;
}

gboolean
atm_bnr_bls_sra_validate (
    const AtmBnrBlsSra *sra,
    GError **error
)
{
    if (sra == NULL || sra->panel == NULL || sra->result == NULL)
        return reject (error, ATM_BNR_BLS_SRA_ERROR_ARGUMENT, "Missing BNR SRA state.");

    if (!atm_bnr_bls_panel_validate (sra->panel, error) ||
        !atm_sra_result_validate (sra->result, error))
        return FALSE;

    AtmSraResult *expected = NULL;
    if (!build_result (sra->panel, &expected, error))
        return FALSE;

    gboolean matches =
        g_strcmp0 (sra->result->qualification->scientific_content_id,
                   expected->qualification->scientific_content_id) == 0 &&
        g_strcmp0 (sra->result->qualification->qualified_artifact_id,
                   expected->qualification->qualified_artifact_id) == 0;
    atm_sra_result_free (expected);
    return matches ||
        reject (error, ATM_BNR_BLS_SRA_ERROR_IDENTITY,
                "Finalized BNR SRA disagrees with its reconstructed five-file source contract.");
}

const AtmSraResult *
atm_bnr_bls_sra_result (const AtmBnrBlsSra *sra)
{
    return sra != NULL ? sra->result : NULL;
}
