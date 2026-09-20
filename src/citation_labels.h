#pragma once

#include <glib.h>

#include "grounding_context.h"

G_BEGIN_DECLS

#define ATM_CITATION_MAX_LABEL_NUMBER 9999

typedef enum {
    ATM_CITATION_RESOLVE_ERROR_ARGUMENT,
    ATM_CITATION_RESOLVE_ERROR_PROVENANCE
} AtmCitationResolveError;

#define ATM_CITATION_RESOLVE_ERROR \
    (atm_citation_resolve_error_quark ())

typedef struct {
    char *label;
    char *evidence_kind;
    gint64 evidence_id;
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *logical_source_id;
    char *source_path;
    char *locator;
    char *title;
    char *excerpt;
    guint source_roles;
    AtmEvidenceMatch match_kind;
} AtmCitationReference;

typedef struct {
    GPtrArray *citations;
    GPtrArray *unknown_labels;
} AtmCitationResolution;

GQuark atm_citation_resolve_error_quark (void);

gboolean atm_citation_resolve_labels (
    const char *model_output,
    const AtmGroundingContext *context,
    AtmCitationResolution **out_resolution,
    GError **error
);

void atm_citation_reference_free (
    AtmCitationReference *citation
);

void atm_citation_resolution_free (
    AtmCitationResolution *resolution
);

guint atm_citation_resolution_count (
    const AtmCitationResolution *resolution
);

guint atm_citation_resolution_unknown_count (
    const AtmCitationResolution *resolution
);

const AtmCitationReference *atm_citation_resolution_get (
    const AtmCitationResolution *resolution,
    guint index
);

const char *atm_citation_resolution_unknown_get (
    const AtmCitationResolution *resolution,
    guint index
);

const char *atm_citation_reference_label (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_repository_id (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_repository_version (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_snapshot_sha (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_logical_source_id (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_source_path (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_locator (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_title (
    const AtmCitationReference *citation
);

const char *atm_citation_reference_excerpt (
    const AtmCitationReference *citation
);

G_END_DECLS
