#pragma once

#include <glib.h>
#include "annual_series_candidate.h"
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
 * The point series is reached through the existing AtmAnnualSeriesCandidate
 * abstraction, which both admitted sources already implement. No callback
 * table is needed for point access, and no GISTEMP type appears here.
 */

typedef struct {
    /* Source-specific, fixed at compile time. */
    const char *profile_id;
    const char *profile_version;
    const char *repository_id;

    guint source_count;
    guint evidence_count;
    guint support_per_point;

    const char *const *roles;

    const char *source_logical_stem;
    const char *point_logical_prefix;

    const char *source_payload_schema;
    const char *source_semantic_type;
    const char *point_payload_schema;
    const char *point_semantic_type;

    const char *point_locator_columns;

    const char *canonical_obligation;
    const char *semantic_profile;
    const char *const *limitations;

    gboolean (*admission_rebuild) (gconstpointer admission, gpointer *out, GError **error);
    void     (*admission_free)    (gpointer admission);
    const char *(*admission_snapshot)      (gconstpointer admission);
    const char *(*admission_source_path)   (gconstpointer admission, guint index);
    const char *(*admission_source_digest) (gconstpointer admission, guint index);

    const AtmAnnualSeriesCandidate *(*admission_candidate) (gconstpointer admission);

    char *(*source_payload) (gconstpointer admission, guint index);
    char *(*point_payload)  (gconstpointer admission, guint index);
} AtmSeriesEvidenceDescriptor;

typedef struct AtmSeriesEvidence AtmSeriesEvidence;

gboolean atm_series_evidence_new (
    const AtmSeriesEvidenceDescriptor *descriptor,
    gconstpointer admission,
    AtmSeriesEvidence **out,
    GError **error);

gboolean atm_series_evidence_validate (
    const AtmSeriesEvidence *evidence,
    GError **error);

void atm_series_evidence_free (AtmSeriesEvidence *evidence);

guint atm_series_evidence_count (const AtmSeriesEvidence *evidence);

const AtmScientificArtifact *atm_series_evidence_at (
    const AtmSeriesEvidence *evidence,
    guint index);

const char *atm_series_evidence_point_support (
    const AtmSeriesEvidence *evidence,
    guint point,
    guint support_index);

const AtmSeriesEvidenceDescriptor *atm_series_evidence_descriptor (
    const AtmSeriesEvidence *evidence);

G_END_DECLS
