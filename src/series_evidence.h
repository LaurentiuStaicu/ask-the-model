#pragma once

#include <glib.h>
#include "scientific_artifact.h"

G_BEGIN_DECLS

/* Ceilings are defensive allocation policy for the two admitted sources, not
 * claims about future coverage. GISTEMP pins 4 sources / 150 artifacts;
 * Energy Institute pins 4 sources / 65 artifacts. */
#define ATM_SERIES_EVIDENCE_MAX_SOURCES 8u
#define ATM_SERIES_EVIDENCE_MAX_ARTIFACTS 4096u

/*
 * Generic source-bound evidence layer.
 *
 * This replaces the two structurally identical source-specific modules
 * (gistemp_evidence, energy_institute_evidence). Everything those modules
 * hard-coded is a descriptor field here: the source count, the artifact count,
 * the support arity, the semantic profile and the payload shape.
 *
 * The layer performs no source interpretation. It rebuilds the admitted bundle
 * from owned bytes, asks the descriptor for each payload, and mints artifacts
 * through the existing Scientific Plane constructor. A caller cannot supply an
 * evidence atom, a point payload, a metadata field, an identity or a point.
 *
 * Source-specific point access uses typed descriptor callbacks. The generic
 * layer never casts one source's candidate to another source's C type; it can
 * also retain the original source decimal spelling without changing the
 * existing evidence-artifact payload schema.
 */

typedef struct {
    guint year;
    guint source_row;
    const char *source_decimal; /* borrowed from the owned admission */
    const char *coefficient;    /* borrowed canonical decimal coefficient */
    gint64 exponent;
} AtmSeriesEvidencePoint;

typedef struct {
    /* Source-specific, fixed at compile time. */
    const char *profile_id;            /* "atm-profile/ewd-gistemp-pinned/v1" */
    const char *profile_version;       /* always "1" for admitted profiles */
    const char *repository_id;         /* canonical, e.g. "ewd" */

    guint source_count;                /* files pinned by the admission bundle */
    guint evidence_count;              /* MUST equal source_count + point_count */
    guint support_per_point;           /* MUST equal source_count + 1 */

    /* roles[i] describes source file i; exactly source_count entries, in the
     * order the admission policy fixes (observations, provenance, manifest,
     * registry). */
    const char *const *roles;

    /* Logical-id stems. Source bindings become
     * "<repository_id>:<source_logical_stem>:<path>"; points become
     * "<point_logical_prefix><year>". */
    const char *source_logical_stem;   /* "gistemp-source" */
    const char *point_logical_prefix;  /* "ewd:gistemp-annual:" */

    /* Payload schemas and semantic types. */
    const char *source_payload_schema; /* "atm-gistemp-source-binding/1" */
    const char *source_semantic_type;  /* "ewd.gistemp_source_binding" */
    const char *point_payload_schema;  /* "atm-gistemp-annual-observation/1" */
    const char *point_semantic_type;   /* "ewd.gistemp_annual_observation" */

    /* Locator columns text, e.g. "year,temperature_anomaly_c_1951_1980". */
    const char *point_locator_columns;

    /*
     * Qualification vocabulary, consumed by the generic SRA layer. These are
     * scientific claims about the source, so they belong to the source and not
     * to the shared construction path.
     */

    /* Canonical reconstruction obligation for SRA qualification.
     * NULL is permitted for Evidence-only sources whose obligation has not been
     * approved; the generic SRA builder must then fail closed. */
    const char *canonical_obligation;

    /* Semantic profile bound into the SRA qualification envelope, including its
     * version suffix, e.g. "atm-profile/ewd-gistemp-pinned/v1@1". This is the
     * evidence/SRA profile namespace, not the downstream series profile. */
    const char *semantic_profile;

    /* Reviewed output-series metadata, separate from the evidence semantic_profile.
     * The generic adapter requires these fields and requires caller metadata to
     * match them exactly before minting a source-bound series contract. NULL means
     * this descriptor has no approved generic series/VerifiedSeries profile yet;
     * the adapter must fail closed. In particular, F0-19 permits EI SRA
     * reconstruction but does not approve an EI series profile. */
    const char *series_semantics;
    const char *series_scenario;
    const char *series_profile_id;
    const char *series_profile_version;

    /* Scientific limitations of this source, NULL-terminated. Rendered verbatim
     * into the finalized SRA; never inherited from another source. */
    const char *const *limitations;

    /* Bridges to the source-specific admission. Callbacks are implemented with
     * the concrete source type, so the generic layer does not type-pun candidate
     * objects that merely happen to have similar layouts. */

    /* Rebuild an owned admission copy into *out. */
    gboolean (*admission_rebuild) (gconstpointer admission, gpointer *out, GError **error);
    void     (*admission_free)    (gpointer admission);
    const char *(*admission_snapshot)      (gconstpointer admission);
    const char *(*admission_source_path)   (gconstpointer admission, guint index);
    const char *(*admission_source_digest) (gconstpointer admission, guint index);

    /* Point count and borrowed point view from the rebuilt admission. */
    guint    (*admission_point_count) (gconstpointer admission);
    gboolean (*admission_point) (gconstpointer admission, guint index,
                                 AtmSeriesEvidencePoint *point);

    /* Payload builders. Both return an owned string. */
    char *(*source_payload) (gconstpointer admission, guint index);
    char *(*point_payload)  (gconstpointer admission, guint index);
} AtmSeriesEvidenceDescriptor;

typedef struct AtmSeriesEvidence AtmSeriesEvidence;

/* *out must be NULL; failure leaves it unchanged. Owns all resulting data. */
gboolean atm_series_evidence_new (
    const AtmSeriesEvidenceDescriptor *descriptor,
    gconstpointer admission,
    AtmSeriesEvidence **out,
    GError **error);

/* Rebuilds from owned sources and compares every storage digest, in order. */
gboolean atm_series_evidence_validate (
    const AtmSeriesEvidence *evidence,
    GError **error);

void atm_series_evidence_free (AtmSeriesEvidence *evidence);

guint atm_series_evidence_count (const AtmSeriesEvidence *evidence);

/* Borrowed read-only artifact. Do not modify its fields or free it. */
const AtmScientificArtifact *atm_series_evidence_at (
    const AtmSeriesEvidence *evidence,
    guint index);

/* Original decimal token borrowed from the pinned/rebuilt source bytes. */
const char *atm_series_evidence_point_source_decimal (
    const AtmSeriesEvidence *evidence,
    guint point);

/* support_per_point borrowed ids per point: the source bindings first, then the
 * point's own observation artifact. Invalid point/support indices return NULL. */
const char *atm_series_evidence_point_support (
    const AtmSeriesEvidence *evidence,
    guint point,
    guint support_index);

/* Borrowed descriptor this collection was built under. */
const AtmSeriesEvidenceDescriptor *atm_series_evidence_descriptor (
    const AtmSeriesEvidence *evidence);

G_END_DECLS
