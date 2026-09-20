#pragma once

#include <glib.h>

#include "repository_sources.h"

G_BEGIN_DECLS

#define ATM_RETRIEVAL_MAX_EXACT_RESULTS 100
#define ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES 1024

typedef enum {
    ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
    ATM_RETRIEVAL_QUERY_ERROR_SQLITE
} AtmRetrievalQueryError;

#define ATM_RETRIEVAL_QUERY_ERROR \
    (atm_retrieval_query_error_quark ())

typedef struct {
    char *evidence_kind;
    gint64 evidence_id;
    char *logical_source_id;
    char *source_path;
    char *locator;
    char *title;
    char *body;
    guint source_roles;
} AtmEvidenceRecord;

GQuark atm_retrieval_query_error_quark (void);

gboolean atm_retrieval_lookup_exact (
    const char *index_path,
    const char *identifier,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);

void atm_evidence_record_free (AtmEvidenceRecord *record);

G_END_DECLS
