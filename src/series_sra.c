#include "series_sra.h"

#include <json-glib/json-glib.h>

/*
 * Generic source-bound SRA construction.
 *
 * This is the extraction of gistemp_sra.c. Everything that file hard-coded is
 * either a descriptor field or a property of an established fact carrying full
 * source context, so nothing below needs to know which source it is serving.
 *
 * Facts are not reconstructed from a fixed point count. Each admitted point
 * already carries its full observation context in a validated artifact payload,
 * so the fact is built from that payload: subject, attribute, unit, dimension
 * and exact decimal identity all come from the same reviewed bytes that the
 * artifact identity was minted over. Adding a second source therefore adds no
 * branch here, only a descriptor.
 *
 * The numeric value is composed as "<coefficient>e<exponent>" from the
 * canonical decimal primitive. No binary64 conversion occurs anywhere.
 */

struct AtmSeriesSra {
    const AtmSeriesEvidenceDescriptor *descriptor;
    AtmSeriesEvidence *evidence;   /* owned */
    AtmSraResult *result;          /* owned, finalized */
};

static gboolean
reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY, message);
    return FALSE;
}

/* Read a required string member, failing closed when absent or non-string. */
static const char *
required_member (JsonObject *o, const char *name, GError **error)
{
    if (!json_object_has_member (o, name)) {
        g_set_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY,
                     "Observation payload is missing member '%s'.", name);
        return NULL;
    }
    JsonNode *node = json_object_get_member (o, name);
    if (!JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_STRING) {
        g_set_error (error, ATM_SRA_ERROR, ATM_SRA_ERROR_IDENTITY,
                     "Observation payload member '%s' is not a string.", name);
        return NULL;
    }
    return json_object_get_string_member (o, name);
}

/* Build one established fact from an admitted observation artifact.
 *
 * The artifact payload was minted by the evidence layer from rebuilt, validated
 * source bytes and its identity was minted over that payload. Re-reading the
 * same payload here means the fact and the artifact cannot describe different
 * observations. The fact inherits the source context it needs from the artifact
 * rather than from a caller argument. */
static AtmSraEstablishedFact *
fact_from_artifact (const AtmScientificArtifact *artifact,
                    const AtmSeriesEvidence *evidence,
                    guint point,
                    guint support_count,
                    GError **error)
{
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, artifact->payload, -1, error)) {
        g_object_unref (parser);
        return NULL;
    }
    JsonObject *o = json_node_get_object (json_parser_get_root (parser));

    const char *subject = required_member (o, "subject", error);
    const char *attribute = required_member (o, "attribute", error);
    const char *unit = required_member (o, "unit", error);
    const char *dimension = required_member (o, "dimension", error);
    const char *coefficient = required_member (o, "coefficient", error);
    const char *exponent = required_member (o, "exponent", error);

    if (subject == NULL || attribute == NULL || unit == NULL || dimension == NULL ||
        coefficient == NULL || exponent == NULL) {
        g_object_unref (parser);
        return NULL;
    }

    /* Scientific decimal stays textual; never converted via double. */
    char *value = g_strdup_printf ("%se%s", coefficient, exponent);

    AtmSraEstablishedFact *fact = atm_sra_established_fact_new (
        artifact->logical_source_id, artifact->artifact_class,
        subject, attribute, value, unit, dimension, artifact->payload);

    g_free (value);
    g_object_unref (parser);
    if (fact == NULL) {
        reject (error, "Cannot construct source observation fact.");
        return NULL;
    }

    /* Support is read through the evidence layer, so the arity is the
     * descriptor's, not a literal in this file. */
    for (guint j = 0; j < support_count; j++) {
        const char *id = atm_series_evidence_point_support (evidence, point, j);
        if (id == NULL || !atm_sra_fact_add_support (fact, id, error)) {
            atm_sra_established_fact_free (fact);
            return NULL;
        }
    }

    return fact;
}

/* Called only with source-reconstructed, validated evidence. */
static AtmSraResult *
build_result (const AtmSeriesEvidenceDescriptor *descriptor,
              const AtmSeriesEvidence *evidence,
              GError **error)
{
    if (!atm_series_evidence_validate (evidence, error)) return NULL;

    const AtmScientificArtifact *first = atm_series_evidence_at (evidence, 0);
    if (first == NULL) {
        reject (error, "Source evidence exposes no artifacts.");
        return NULL;
    }

    AtmSraResult *r = atm_sra_result_new (ATM_SRA_ANSWERED);

    char *snapshot = g_strdup_printf ("%s@%s#%s", first->repository_id,
                                      first->repository_version, first->snapshot_sha);
    gboolean qualified = atm_sra_qualification_add_repository_snapshot (r, snapshot, error);
    g_free (snapshot);

    /* The obligation string is the descriptor's; this file names no source. */
    if (!qualified ||
        !atm_sra_qualification_add_canonical_obligation (r, ATM_SRA_SCHEMA_ID, error) ||
        !atm_sra_qualification_add_canonical_obligation (r, descriptor->canonical_obligation, error) ||
        !atm_sra_qualification_add_semantic_profile (r, descriptor->semantic_profile, error) ||
        !atm_sra_qualification_set_numeric_profile (r, "exact_decimal_coefficient_exponent/1", error))
        goto invalid;

    for (guint i = 0; i < atm_series_evidence_count (evidence); i++) {
        const AtmScientificArtifact *a = atm_series_evidence_at (evidence, i);
        if (!atm_sra_qualification_add_evidence_artifact (r, a, error))
            goto invalid;
    }

    guint artifact_count = atm_series_evidence_count (evidence);
    guint point_count = artifact_count - descriptor->source_count;
    for (guint i = 0; i < point_count; i++) {
        const AtmScientificArtifact *a =
            atm_series_evidence_at (evidence, i + descriptor->source_count);
        if (a == NULL) {
            reject (error, "Source evidence exposes a missing observation artifact.");
            goto invalid;
        }

        AtmSraEstablishedFact *fact = fact_from_artifact (a, evidence, i,
            descriptor->support_per_point, error);
        if (fact == NULL) goto invalid;

        if (!atm_sra_result_add_fact (r, fact, error)) {
            atm_sra_established_fact_free (fact);
            goto invalid;
        }
    }

    /* Limitations are the descriptor's, because they are scientific claims
     * about the source, not generic properties of this layer. */
    for (guint i = 0; descriptor->limitations[i] != NULL; i++) {
        if (!atm_sra_result_add_limitation (r, descriptor->limitations[i], error))
            goto invalid;
    }

    if (!atm_sra_result_finalize (r, error)) goto invalid;
    return r;

invalid:
    atm_sra_result_free (r);
    return NULL;
}

void
atm_series_sra_free (AtmSeriesSra *s)
{
    if (s == NULL) return;
    atm_series_evidence_free (s->evidence);
    atm_sra_result_free (s->result);
    g_free (s);
}

gboolean
atm_series_sra_new (const AtmSeriesEvidenceDescriptor *descriptor,
                    gconstpointer admission,
                    AtmSeriesSra **out,
                    GError **error)
{
    if (descriptor == NULL || admission == NULL || out == NULL || *out != NULL)
        return reject (error, "Invalid source SRA arguments.");

    AtmSeriesSra *s = g_new0 (AtmSeriesSra, 1);
    s->descriptor = descriptor;

    if (!atm_series_evidence_new (descriptor, admission, &s->evidence, error))
        goto invalid;

    s->result = build_result (descriptor, s->evidence, error);
    if (s->result == NULL) goto invalid;

    *out = s;
    return TRUE;

invalid:
    atm_series_sra_free (s);
    return FALSE;
}

gboolean
atm_series_sra_validate (const AtmSeriesSra *s, GError **error)
{
    if (s == NULL || s->result == NULL || s->evidence == NULL)
        return reject (error, "Missing source SRA.");
    if (!atm_sra_result_validate (s->result, error)) return FALSE;

    /* Reconstruct from owned evidence and compare both identities. A changed
     * value, unit, support set, limitation, obligation or numeric profile is
     * rejected even after successful re-finalization with the generic API. */
    AtmSraResult *expected = build_result (s->descriptor, s->evidence, error);
    if (expected == NULL) return FALSE;

    gboolean matches =
        g_strcmp0 (s->result->qualification->scientific_content_id,
                   expected->qualification->scientific_content_id) == 0 &&
        g_strcmp0 (s->result->qualification->qualified_artifact_id,
                   expected->qualification->qualified_artifact_id) == 0;

    atm_sra_result_free (expected);
    return matches ||
        reject (error, "Finalized SRA disagrees with reconstructed source observations.");
}

const AtmSraResult *
atm_series_sra_result (const AtmSeriesSra *s)
{
    return s != NULL ? s->result : NULL;
}

const AtmSeriesEvidence *
atm_series_sra_evidence (const AtmSeriesSra *s)
{
    return s != NULL ? s->evidence : NULL;
}
