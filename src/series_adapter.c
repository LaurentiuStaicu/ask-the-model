#include "series_adapter.h"

#include <json-glib/json-glib.h>
#include <string.h>

#include "series_sra.h"

/*
 * Source-bound adapter: admitted bundle -> evidence collection -> general
 * series contract.
 *
 * Every point's X is its admitted calendar year, Y retains the exact source
 * decimal with no binary64 round trip, and support is the descriptor's support
 * arity - the source bindings plus the point's own observation artifact. The
 * general contract validates that support against a finalized SRA, so the
 * caller must pass evidence whose artifacts are already qualified.
 *
 * Nothing here reads a caller-supplied point, identity or metric. The adapter
 * never names a source: the descriptor carries everything source-specific.
 */

static gboolean
reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SERIES_CONTRACT_ERROR,
                         ATM_SERIES_CONTRACT_ERROR_SHAPE, message);
    return FALSE;
}

static gboolean
metadata_valid (const AtmSeriesAdapterMetadata *m)
{
    if (m == NULL) return FALSE;
    return m->series_semantics != NULL && m->scenario != NULL &&
           m->time_scope != NULL && m->profile_id != NULL &&
           m->profile_version != NULL;
}

/* Read a required string member, failing closed when absent or non-string. */
static gboolean
time_scope_matches_points (const AtmSeriesContract *series)
{
    if (series == NULL || series->x_kind != ATM_SERIES_X_CALENDAR_YEAR ||
        series->points == NULL || series->points->len == 0)
        return FALSE;

    const AtmSeriesContractPoint *first =
        g_ptr_array_index (series->points, 0);
    const AtmSeriesContractPoint *last =
        g_ptr_array_index (series->points, series->points->len - 1);
    if (first == NULL || last == NULL || first->x == NULL || last->x == NULL)
        return FALSE;

    char *expected = g_strdup_printf ("%s-%s", first->x, last->x);
    gboolean matches = g_strcmp0 (series->time_scope, expected) == 0;
    g_free (expected);
    return matches;
}

static const char *
required_text (JsonObject *o, const char *name, GError **error)
{
    if (!json_object_has_member (o, name)) {
        g_set_error (error, ATM_SERIES_CONTRACT_ERROR,
                     ATM_SERIES_CONTRACT_ERROR_SHAPE,
                     "Observation payload is missing member '%s'.", name);
        return NULL;
    }
    JsonNode *node = json_object_get_member (o, name);
    if (!JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_STRING) {
        g_set_error (error, ATM_SERIES_CONTRACT_ERROR,
                     ATM_SERIES_CONTRACT_ERROR_SHAPE,
                     "Observation payload member '%s' is not a string.", name);
        return NULL;
    }
    return json_object_get_string_member (o, name);
}

gboolean
atm_series_adapter_from_evidence (const AtmSeriesEvidenceDescriptor *descriptor,
                                  const AtmSeriesAdapterMetadata *metadata,
                                  const AtmSeriesEvidence *evidence,
                                  AtmSeriesContract **out,
                                  GError **error)
{
    if (descriptor == NULL || evidence == NULL || out == NULL || *out != NULL)
        return reject (error, "Invalid series-adapter arguments.");
    if (!metadata_valid (metadata))
        return reject (error, "Invalid series-adapter metadata.");

    /* A descriptor is part of the source identity, not a hint. Refuse to
     * reinterpret an evidence collection built under another descriptor. */
    if (atm_series_evidence_descriptor (evidence) != descriptor)
        return reject (error, "Series descriptor does not match source evidence.");
    if (!atm_series_evidence_validate (evidence, error))
        return FALSE;

    const AtmScientificArtifact *first = atm_series_evidence_at (evidence, 0);
    if (first == NULL)
        return reject (error, "Source evidence exposes no artifacts.");

    guint artifact_count = atm_series_evidence_count (evidence);
    if (artifact_count != descriptor->evidence_count)
        return reject (error, "Source evidence artifact count does not match descriptor.");
    if (artifact_count <= descriptor->source_count)
        return reject (error, "Source evidence carries no observations.");

    /* The first observation artifact, not the whole-file binding at index 0,
     * carries the reviewed series metadata. */
    const AtmScientificArtifact *first_point =
        atm_series_evidence_at (evidence, descriptor->source_count);
    if (first_point == NULL)
        return reject (error, "Source evidence exposes no observations.");

    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, first_point->payload, -1, error)) {
        g_object_unref (parser);
        return FALSE;
    }
    JsonObject *o = json_node_get_object (json_parser_get_root (parser));

    const char *subject = required_text (o, "subject", error);
    const char *attribute = required_text (o, "attribute", error);
    const char *unit = required_text (o, "unit", error);
    const char *dimension = required_text (o, "dimension", error);
    if (subject == NULL || attribute == NULL || unit == NULL || dimension == NULL) {
        g_object_unref (parser);
        return FALSE;
    }

    /* Repository, version and snapshot come from the artifact the identity was
     * minted over, never from a caller argument. */
    AtmSeriesContract *series = atm_series_contract_new (
        ATM_SERIES_X_CALENDAR_YEAR,
        subject,
        attribute,
        "calendar_year",
        "time",
        unit,
        dimension,
        metadata->series_semantics,
        metadata->scenario,
        metadata->time_scope,
        "empirical",
        first->repository_id,
        first->repository_version,
        first->snapshot_sha,
        first->source_path,
        metadata->profile_id,
        metadata->profile_version,
        error);

    g_object_unref (parser);
    if (series == NULL) return FALSE;

    guint point_count = artifact_count - descriptor->source_count;
    for (guint i = 0; i < point_count; i++) {
        const AtmScientificArtifact *a =
            atm_series_evidence_at (evidence, i + descriptor->source_count);
        if (a == NULL) {
            atm_series_contract_free (series);
            return reject (error, "Source evidence exposes a missing observation.");
        }

        const char **support = g_new0 (const char *, descriptor->support_per_point);
        for (guint j = 0; j < descriptor->support_per_point; j++) {
            support[j] = atm_series_evidence_point_support (evidence, i, j);
            if (support[j] == NULL) {
                g_free (support);
                atm_series_contract_free (series);
                return reject (error, "Source evidence exposes a missing support id.");
            }
        }

        JsonParser *point_parser = json_parser_new ();
        if (!json_parser_load_from_data (point_parser, a->payload, -1, error)) {
            g_free (support);
            atm_series_contract_free (series);
            g_object_unref (point_parser);
            return FALSE;
        }
        JsonObject *po = json_node_get_object (json_parser_get_root (point_parser));

        const char *year_text = required_text (po, "calendar_year", error);
        const char *coefficient = required_text (po, "coefficient", error);
        const char *exponent_text = required_text (po, "exponent", error);

        if (year_text == NULL || coefficient == NULL || exponent_text == NULL) {
            g_free (support);
            atm_series_contract_free (series);
            g_object_unref (point_parser);
            return FALSE;
        }

        /* Keep the exact token from the rebuilt, admitted CSV. The Evidence
         * artifact intentionally retains its legacy coefficient/exponent shape
         * so source-equivalence identities remain stable; its owned admission
         * is the authoritative place to recover the original spelling. */
        const char *source_token =
            atm_series_evidence_point_source_decimal (evidence, i);
        if (source_token == NULL) {
            g_free (support);
            atm_series_contract_free (series);
            g_object_unref (point_parser);
            return reject (error, "Admitted source point has no original decimal token.");
        }
        char *source_decimal = g_strdup (source_token);
        gint64 exponent = g_ascii_strtoll (exponent_text, NULL, 10);
        gboolean negative_zero =
            g_strcmp0 (coefficient, "0") == 0 && exponent == 0 &&
            source_decimal[0] == '-';

        gboolean added = atm_series_contract_add_point (series,
            year_text,
            ATM_SERIES_Y_NUMERIC,
            source_decimal,
            coefficient,
            exponent,
            negative_zero,
            FALSE,      /* break_before */
            NULL,       /* missing_reason */
            NULL,       /* break_reason */
            support,
            descriptor->support_per_point,
            error);

        g_free (source_decimal);
        g_free (support);
        g_object_unref (point_parser);

        if (!added) {
            atm_series_contract_free (series);
            return FALSE;
        }
    }

    if (!time_scope_matches_points (series)) {
        atm_series_contract_free (series);
        return reject (error,
            "Declared time scope does not match admitted point coverage.");
    }

    *out = series;
    return TRUE;
}

gboolean
atm_series_adapter_admit (const AtmSeriesEvidenceDescriptor *descriptor,
                          const AtmSeriesAdapterMetadata *metadata,
                          gconstpointer admission,
                          AtmSeriesContract **out_contract,
                          GError **error)
{
    if (descriptor == NULL || metadata == NULL || admission == NULL ||
        out_contract == NULL || *out_contract != NULL)
        return reject (error, "Invalid series-adapter arguments.");

    AtmSeriesSra *sra = NULL;
    if (!atm_series_sra_new (descriptor, admission, &sra, error))
        return FALSE;

    AtmSeriesContract *contract = NULL;
    gboolean built = atm_series_adapter_from_evidence (descriptor, metadata,
        atm_series_sra_evidence (sra), &contract, error);

    if (built) {
        /* Qualification linkage is through the finalized SRA, never ID strings.
         * The contract refuses caller-supplied identities outright. */
        built = atm_series_contract_validate (contract,
            atm_series_sra_result (sra), error);
    }

    if (!built) {
        if (contract != NULL) atm_series_contract_free (contract);
        atm_series_sra_free (sra);
        return FALSE;
    }

    *out_contract = contract;
    atm_series_sra_free (sra);
    return TRUE;
}

/* Compare every identity-bearing field, not just cached identifiers.
 * AtmSeriesContract is a public mutable struct, so a caller could otherwise
 * mutate a point after validation while leaving its minted IDs unchanged. */
static gboolean
series_material_matches (const AtmSeriesContract *actual,
                         const AtmSeriesContract *expected)
{
    if (actual == NULL || expected == NULL ||
        actual->x_kind != expected->x_kind ||
        g_strcmp0 (actual->subject, expected->subject) != 0 ||
        g_strcmp0 (actual->attribute, expected->attribute) != 0 ||
        g_strcmp0 (actual->x_unit, expected->x_unit) != 0 ||
        g_strcmp0 (actual->x_dimension, expected->x_dimension) != 0 ||
        g_strcmp0 (actual->y_unit, expected->y_unit) != 0 ||
        g_strcmp0 (actual->y_dimension, expected->y_dimension) != 0 ||
        g_strcmp0 (actual->series_semantics, expected->series_semantics) != 0 ||
        g_strcmp0 (actual->scenario, expected->scenario) != 0 ||
        g_strcmp0 (actual->time_scope, expected->time_scope) != 0 ||
        g_strcmp0 (actual->epistemic_status, expected->epistemic_status) != 0 ||
        g_strcmp0 (actual->repository_id, expected->repository_id) != 0 ||
        g_strcmp0 (actual->repository_version, expected->repository_version) != 0 ||
        g_strcmp0 (actual->snapshot_sha, expected->snapshot_sha) != 0 ||
        g_strcmp0 (actual->source_path, expected->source_path) != 0 ||
        g_strcmp0 (actual->profile_id, expected->profile_id) != 0 ||
        g_strcmp0 (actual->profile_version, expected->profile_version) != 0 ||
        g_strcmp0 (actual->scientific_content_id, expected->scientific_content_id) != 0 ||
        g_strcmp0 (actual->qualified_series_id, expected->qualified_series_id) != 0 ||
        actual->points == NULL || expected->points == NULL ||
        actual->points->len != expected->points->len)
        return FALSE;

    for (guint i = 0; i < actual->points->len; i++) {
        const AtmSeriesContractPoint *a = g_ptr_array_index (actual->points, i);
        const AtmSeriesContractPoint *b = g_ptr_array_index (expected->points, i);
        if (a == NULL || b == NULL ||
            a->order != b->order ||
            g_strcmp0 (a->x, b->x) != 0 ||
            a->y_status != b->y_status ||
            g_strcmp0 (a->source_decimal, b->source_decimal) != 0 ||
            g_strcmp0 (a->coefficient, b->coefficient) != 0 ||
            a->exponent != b->exponent ||
            a->negative_zero != b->negative_zero ||
            a->break_before != b->break_before ||
            g_strcmp0 (a->missing_reason, b->missing_reason) != 0 ||
            g_strcmp0 (a->break_reason, b->break_reason) != 0 ||
            a->support == NULL || b->support == NULL ||
            a->support->len != b->support->len)
            return FALSE;

        for (guint j = 0; j < a->support->len; j++)
            if (g_strcmp0 (g_ptr_array_index (a->support, j),
                           g_ptr_array_index (b->support, j)) != 0)
                return FALSE;
    }
    return TRUE;
}

gboolean
atm_series_adapter_revalidate (const AtmSeriesContract *contract,
                               const AtmSeriesEvidenceDescriptor *descriptor,
                               const AtmSeriesAdapterMetadata *metadata,
                               gconstpointer admission,
                               GError **error)
{
    if (contract == NULL || descriptor == NULL || metadata == NULL ||
        admission == NULL)
        return reject (error, "Invalid series-adapter arguments.");

    /* Rebuild the entire chain from the same admission. The contract mints its
     * identities only when validated against a finalized SRA, so the SRA must
     * be part of the reconstruction - an evidence-only rebuild has no identity
     * to compare and would always read as disagreement. */
    AtmSeriesSra *sra = NULL;
    if (!atm_series_sra_new (descriptor, admission, &sra, error))
        return FALSE;

    AtmSeriesContract *rebuilt = NULL;
    gboolean built = atm_series_adapter_from_evidence (descriptor, metadata,
        atm_series_sra_evidence (sra), &rebuilt, error);

    if (built)
        built = atm_series_contract_validate (rebuilt,
            atm_series_sra_result (sra), error);

    if (!built) {
        if (rebuilt != NULL) atm_series_contract_free (rebuilt);
        atm_series_sra_free (sra);
        return FALSE;
    }

    /* Compare every field and both IDs. Cached IDs alone are insufficient
     * because public contract fields may have changed since validation. */
    gboolean matches = series_material_matches (contract, rebuilt);

    atm_series_contract_free (rebuilt);
    atm_series_sra_free (sra);

    if (!matches)
        return reject (error, "Series contract disagrees with reconstructed source.");

    return TRUE;
}
