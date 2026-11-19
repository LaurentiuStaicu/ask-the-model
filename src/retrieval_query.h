#pragma once

#include <glib.h>

#include "repository_sources.h"

G_BEGIN_DECLS

typedef struct _AtmRetrievalQuerySession AtmRetrievalQuerySession;


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
    char *entity_type;
    char *relation_type;
    char *native_id;
    char *from_logical_source_id;
    char *to_logical_source_id;
    char *dataset_logical_source_id;
    char *row_key;
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


AtmRetrievalQuerySession *atm_retrieval_query_session_open (
    const char *index_path,
    GError **error
);
void atm_retrieval_query_session_close (
    AtmRetrievalQuerySession *session
);
/* Starts a fresh read transaction and repeats snapshot and FTS validation
 * on the existing read-only SQLite connection. */
gboolean atm_retrieval_query_session_refresh (
    AtmRetrievalQuerySession *session,
    GError **error
);
gboolean atm_retrieval_query_session_lookup_exact (
    AtmRetrievalQuerySession *session,
    const char *identifier,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);
gboolean atm_retrieval_query_session_search_fts (
    AtmRetrievalQuerySession *session,
    const char *query,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);
gboolean atm_retrieval_query_session_lookup_dataset_rows (
    AtmRetrievalQuerySession *session,
    const char *dataset_identifier,
    const char *row_key,
    guint max_results,
    GPtrArray **out_results,
    GError **error
);



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
