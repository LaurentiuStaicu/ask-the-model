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

typedef enum {
    ATM_EVIDENCE_MATCH_EXACT,
    ATM_EVIDENCE_MATCH_TABULAR,
    ATM_EVIDENCE_MATCH_LEXICAL
} AtmEvidenceMatch;

typedef struct {
    char *evidence_kind;
    gint64 evidence_id;
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *logical_source_id;
    char *source_path;
    char *locator;
    char *title;
    char *body;
    guint source_roles;
    AtmEvidenceMatch match_kind;
    gboolean has_lexical_score;
    double lexical_score;
} AtmEvidenceRecord;

GQuark atm_retrieval_query_error_quark (void);

gboolean atm_retrieval_lookup_exact (
    const char *index_path,
    const char *identifier,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);

gboolean atm_retrieval_search_fts (
    const char *index_path,
    const char *query,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);

gboolean atm_retrieval_lookup_dataset_rows (
    const char *index_path,
    const char *dataset_identifier,
    const char *row_key,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);

void atm_evidence_record_free (AtmEvidenceRecord *record);

G_END_DECLS
