#pragma once

#include <glib.h>

#include "retrieval_normalize.h"
#include "retrieval_scope.h"

G_BEGIN_DECLS

#define ATM_RETRIEVAL_ROUTER_MAX_QUERY_BYTES 4096
#define ATM_RETRIEVAL_ROUTER_MAX_CANDIDATES 16

typedef enum {
    ATM_RETRIEVAL_ROUTER_ERROR_ARGUMENT,
    ATM_RETRIEVAL_ROUTER_ERROR_PROVENANCE
} AtmRetrievalRouterError;

#define ATM_RETRIEVAL_ROUTER_ERROR \
    (atm_retrieval_router_error_quark ())

typedef struct {
    char *repository_id;
    GPtrArray *evidence;
} AtmRepositoryEvidenceSet;

typedef struct {
    GPtrArray *repositories;
    char *expanded_query;
    guint intents;
    gboolean explicit_scope;
    gboolean requested_outside_scope;
} AtmRetrievalResultSet;

GQuark atm_retrieval_router_error_quark (void);

void atm_repository_evidence_set_free (
    AtmRepositoryEvidenceSet *set
);

void atm_retrieval_result_set_free (
    AtmRetrievalResultSet *results
);

gboolean atm_retrieval_run (
    const char *query,
    const GPtrArray *active_repositories,
    guint max_results_per_repository,
    AtmRetrievalResultSet **out_results,
    GError **error
);

G_END_DECLS
