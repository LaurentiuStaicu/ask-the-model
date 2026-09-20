#pragma once

#include <glib.h>

#include "retrieval_router.h"

G_BEGIN_DECLS

#define ATM_GROUNDING_MAX_SOURCES 32
#define ATM_GROUNDING_MIN_CONTEXT_BYTES 1024
#define ATM_GROUNDING_MAX_CONTEXT_BYTES (128 * 1024)
#define ATM_GROUNDING_MAX_EXCERPT_BYTES (4 * 1024)

typedef enum {
    ATM_GROUNDING_ERROR_ARGUMENT,
    ATM_GROUNDING_ERROR_PROVENANCE,
    ATM_GROUNDING_ERROR_BUDGET
} AtmGroundingError;

#define ATM_GROUNDING_ERROR (atm_grounding_error_quark ())

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
} AtmGroundingSource;

typedef struct {
    char *evidence_text;
    GPtrArray *sources;
    gboolean truncated;
    gsize evidence_bytes;
} AtmGroundingContext;

GQuark atm_grounding_error_quark (void);

const char *atm_grounding_system_instructions (void);

const char *atm_grounding_post_evidence_reminder (void);

gboolean atm_grounding_context_build (
    const AtmRetrievalResultSet *retrieval,
    guint max_sources,
    gsize max_context_bytes,
    AtmGroundingContext **out_context,
    GError **error
);

void atm_grounding_source_free (AtmGroundingSource *source);

void atm_grounding_context_free (AtmGroundingContext *context);

G_END_DECLS
