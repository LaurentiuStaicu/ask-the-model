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

    const AtmScientificArtifact *first = atm_series_evidence_at (evidence, 0);
    if (first == NULL)
        return reject (error, "Source evidence exposes no artifacts.");

    guint artifact_count = atm_series_evidence_count (evidence);
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

        /* Source decimal: coefficient followed by exponent, matching the form
         * the SRA fact value uses. The contract recomputes coefficient and
         * exponent from this token, so a mismatch is caught at add_point. No
         * binary64 conversion occurs anywhere. */
        char *source_decimal = g_strdup_printf ("%se%s", coefficient, exponent_text);
        gint64 exponent = g_ascii_strtoll (exponent_text, NULL, 10);

        gboolean added = atm_series_contract_add_point (series,
            year_text,
            ATM_SERIES_Y_NUMERIC,
            source_decimal,
            coefficient,
            exponent,
            FALSE,      /* negative_zero: the source spelling carries it */
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

gboolean
atm_series_adapter_validate (const AtmSeriesContract *contract,
                             const AtmSeriesEvidenceDescriptor *descriptor,
                             const AtmSeriesAdapterMetadata *metadata,
                             const AtmSeriesEvidence *evidence,
                             GError **error)
{
    if (contract == NULL || descriptor == NULL || metadata == NULL ||
        evidence == NULL)
        return reject (error, "Invalid series-adapter arguments.");

    AtmSeriesContract *rebuilt = NULL;
    if (!atm_series_adapter_from_evidence (descriptor, metadata, evidence,
                                           &rebuilt, error))
        return FALSE;

    /* Compare the reconstructed identities. A materialised contract whose
     * identity disagrees with its own reconstruction is rejected, which is the
     * guarantee the source-specific module provided. */
    gboolean matches =
        g_strcmp0 (atm_series_contract_scientific_id (contract),
                   atm_series_contract_scientific_id (rebuilt)) == 0 &&
        g_strcmp0 (atm_series_contract_qualified_id (contract),
                   atm_series_contract_qualified_id (rebuilt)) == 0;

    atm_series_contract_free (rebuilt);

    if (!matches)
        return reject (error, "Series contract disagrees with reconstructed source.");

    return TRUE;
}
