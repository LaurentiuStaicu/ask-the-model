#include "series_evidence.h"

#include <json-glib/json-glib.h>

/*
 * Generic source-bound evidence collection.
 *
 * This is the extraction of the two byte-identical source modules
 * (gistemp_evidence.c, energy_institute_evidence.c). Every literal that
 * differed between them is a descriptor field. The collection logic, the
 * payload plumbing and the reconstruction-comparison validation are shared.
 *
 * The point series is read through the existing AtmAnnualSeriesCandidate
 * abstraction, which both admitted sources already implement. No GISTEMP type
 * and no source-specific header appears in this file or in its header.
 */

struct AtmSeriesEvidence {
    const AtmSeriesEvidenceDescriptor *descriptor;
    gpointer admission;          /* owned, rebuilt copy of the admitted bundle */
    GPtrArray *artifacts;        /* AtmScientificArtifact, owned */
};

static gboolean
reject (GError **error, const char *message)
{
    g_set_error_literal (error, ATM_SCIENTIFIC_ARTIFACT_ERROR,
                         ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY, message);
    return FALSE;
}

/* Shape and arity of the descriptor itself. A malformed descriptor is a build
 * defect, not a runtime condition, so every requirement is checked here once
 * rather than being assumed at each use site. */
static gboolean
descriptor_valid (const AtmSeriesEvidenceDescriptor *d)
{
    if (d == NULL) return FALSE;

    if (d->profile_id == NULL || d->profile_version == NULL ||
        d->repository_id == NULL)
        return FALSE;

    if (d->source_count == 0 || d->source_count > ATM_SERIES_EVIDENCE_MAX_SOURCES)
        return FALSE;
    if (d->roles == NULL)
        return FALSE;
    for (guint i = 0; i < d->source_count; i++)
        if (d->roles[i] == NULL) return FALSE;

    /* Each point carries one support id per source binding plus its own
     * observation artifact. This is a contract, not a convention: the
     * source-specific modules encoded it implicitly as "point + 4". */
    if (d->support_per_point != d->source_count + 1u)
        return FALSE;

    /* The evidence count is fully determined by the other two; a descriptor
     * that disagrees with the arithmetic is rejected rather than trusted. */
    if (d->evidence_count <= d->source_count ||
        d->evidence_count > ATM_SERIES_EVIDENCE_MAX_ARTIFACTS)
        return FALSE;

    if (d->source_logical_stem == NULL || d->point_logical_prefix == NULL ||
        d->source_payload_schema == NULL || d->source_semantic_type == NULL ||
        d->point_payload_schema == NULL || d->point_semantic_type == NULL ||
        d->point_locator_columns == NULL || d->canonical_obligation == NULL ||
        d->semantic_profile == NULL || d->limitations == NULL ||
        d->limitations[0] == NULL)
        return FALSE;

    if (d->admission_rebuild == NULL || d->admission_free == NULL ||
        d->admission_snapshot == NULL || d->admission_source_path == NULL ||
        d->admission_source_digest == NULL || d->admission_candidate == NULL ||
        d->source_payload == NULL || d->point_payload == NULL)
        return FALSE;

    return TRUE;
}

static gboolean
append_artifact (AtmSeriesEvidence *e,
                 guint source_index,
                 const char *locator,
                 const char *logical_id,
                 const char *semantic_type,
                 char *payload,
                 GError **error)
{
    const AtmSeriesEvidenceDescriptor *d = e->descriptor;

    /* This status is minted only after the source-specific admission/rebuild.
     * No caller-supplied evidence atom, metadata or point payload is accepted. */
    AtmScientificEvidenceAtom atom = {0};
    atom.status = ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED;
    /* The atom declares these as non-const char *, matching the existing
     * source-specific modules. The descriptor's strings outlive the atom and
     * the constructor does not take ownership, so the cast is honest here. */
    atom.profile_id = (char *) d->profile_id;
    atom.profile_version = (char *) d->profile_version;
    atom.repository_id = (char *) d->repository_id;

    const char *snapshot = d->admission_snapshot (e->admission);
    char *version = g_strconcat ("snapshot:", snapshot, NULL);
    atom.repository_version = version;
    atom.snapshot_sha = (char *) snapshot;
    atom.source_path = (char *) d->admission_source_path (e->admission, source_index);
    atom.locator = (char *) locator;
    atom.logical_source_id = (char *) logical_id;
    atom.semantic_type = (char *) semantic_type;
    atom.raw_payload = payload;

    AtmScientificArtifact *artifact = NULL;
    gboolean ok = atm_scientific_artifact_from_evidence (&atom, &artifact, error);
    g_free (version);
    g_free (payload);
    if (!ok) return FALSE;
    g_ptr_array_add (e->artifacts, artifact);
    return TRUE;
}

/* The whole-file bindings. Each is scoped to the exact reviewed policy and
 * attests only that the retained bytes match it. */
static gboolean
append_source_bindings (AtmSeriesEvidence *e, GError **error)
{
    const AtmSeriesEvidenceDescriptor *d = e->descriptor;

    for (guint i = 0; i < d->source_count; i++) {
        char *logical = g_strdup_printf ("%s:%s:%s",
            d->repository_id, d->source_logical_stem,
            d->admission_source_path (e->admission, i));
        gboolean ok = append_artifact (e, i, "file:complete", logical,
            d->source_semantic_type,
            d->source_payload (e->admission, i), error);
        g_free (logical);
        if (!ok) return FALSE;
    }
    return TRUE;
}

/* One observation artifact per admitted point, in admitted order. */
static gboolean
append_point_observations (AtmSeriesEvidence *e, GError **error)
{
    const AtmSeriesEvidenceDescriptor *d = e->descriptor;
    const AtmAnnualSeriesCandidate *c = d->admission_candidate (e->admission);
    if (c == NULL) return reject (error, "Admitted bundle exposes no point series.");

    guint points = atm_annual_series_candidate_count (c);
    if (points == 0 || points != d->evidence_count - d->source_count)
        return reject (error, "Unexpected source-evidence coverage.");

    for (guint i = 0; i < points; i++) {
        char *locator = g_strdup_printf ("csv:row:%u:columns:%s",
            atm_annual_series_candidate_source_row (c, i), d->point_locator_columns);
        char *logical = g_strdup_printf ("%s%u",
            d->point_logical_prefix, atm_annual_series_candidate_year (c, i));
        gboolean ok = append_artifact (e, 0, locator, logical,
            d->point_semantic_type,
            d->point_payload (e->admission, i), error);
        g_free (logical);
        g_free (locator);
        if (!ok) return FALSE;
    }
    return TRUE;
}

void
atm_series_evidence_free (AtmSeriesEvidence *e)
{
    if (e == NULL) return;
    if (e->admission != NULL && e->descriptor != NULL)
        e->descriptor->admission_free (e->admission);
    g_clear_pointer (&e->artifacts, g_ptr_array_unref);
    g_free (e);
}

gboolean
atm_series_evidence_new (const AtmSeriesEvidenceDescriptor *descriptor,
                         gconstpointer admission,
                         AtmSeriesEvidence **out,
                         GError **error)
{
    if (out == NULL || *out != NULL || admission == NULL)
        return reject (error, "Invalid source-evidence arguments.");
    if (!descriptor_valid (descriptor))
        return reject (error, "Invalid source-evidence descriptor.");

    AtmSeriesEvidence *e = g_new0 (AtmSeriesEvidence, 1);
    e->descriptor = descriptor;
    e->artifacts = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_scientific_artifact_free);

    /* Own the source bytes first; everything below reads the rebuilt copy. */
    if (!descriptor->admission_rebuild (admission, &e->admission, error))
        goto invalid;

    if (!append_source_bindings (e, error)) goto invalid;
    if (!append_point_observations (e, error)) goto invalid;

    if (e->artifacts->len != descriptor->evidence_count) {
        reject (error, "Unexpected source-evidence coverage.");
        goto invalid;
    }

    *out = e;
    return TRUE;

invalid:
    atm_series_evidence_free (e);
    return FALSE;
}

gboolean
atm_series_evidence_validate (const AtmSeriesEvidence *e, GError **error)
{
    if (e == NULL || e->descriptor == NULL || e->artifacts == NULL ||
        e->artifacts->len != e->descriptor->evidence_count)
        return reject (error, "Invalid source-evidence shape.");

    /* Reconstruct from owned sources and compare, in order. A reordered,
     * altered or dropped artifact is rejected even when its generic artifact
     * hash has been correctly recomputed. */
    AtmSeriesEvidence *expected = NULL;
    if (!atm_series_evidence_new (e->descriptor, e->admission, &expected, error))
        return FALSE;

    gboolean ok = TRUE;
    for (guint i = 0; i < e->artifacts->len; i++) {
        const AtmScientificArtifact *actual = g_ptr_array_index (e->artifacts, i);
        const AtmScientificArtifact *rebuilt = g_ptr_array_index (expected->artifacts, i);
        if (!atm_scientific_artifact_validate (actual, error)) { ok = FALSE; break; }
        if (g_strcmp0 (actual->storage_digest, rebuilt->storage_digest) != 0) {
            ok = reject (error,
                "Scientific artifact disagrees with admitted source reconstruction.");
            break;
        }
    }

    atm_series_evidence_free (expected);
    return ok;
}

guint
atm_series_evidence_count (const AtmSeriesEvidence *e)
{
    return e != NULL && e->artifacts != NULL ? e->artifacts->len : 0;
}

const AtmScientificArtifact *
atm_series_evidence_at (const AtmSeriesEvidence *e, guint i)
{
    return e != NULL && e->artifacts != NULL && i < e->artifacts->len
        ? g_ptr_array_index (e->artifacts, i) : NULL;
}

const char *
atm_series_evidence_point_support (const AtmSeriesEvidence *e, guint point, guint support)
{
    if (e == NULL || e->descriptor == NULL) return NULL;

    const AtmSeriesEvidenceDescriptor *d = e->descriptor;
    const AtmAnnualSeriesCandidate *c = d->admission_candidate (e->admission);
    if (c == NULL) return NULL;
    if (point >= atm_annual_series_candidate_count (c) || support >= d->support_per_point)
        return NULL;

    /* Source bindings occupy [0, source_count); each point's own observation
     * artifact follows the last binding, offset by point order. */
    const AtmScientificArtifact *a = atm_series_evidence_at (e,
        support < d->source_count ? support : point + d->source_count);
    return a != NULL ? a->qualified_artifact_id : NULL;
}

const AtmSeriesEvidenceDescriptor *
atm_series_evidence_descriptor (const AtmSeriesEvidence *e)
{
    return e != NULL ? e->descriptor : NULL;
}
