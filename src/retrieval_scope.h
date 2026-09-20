#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_RETRIEVAL_SCOPE_MAX_QUERY_BYTES 4096

typedef enum {
    ATM_RETRIEVAL_SCOPE_ERROR_ARGUMENT
} AtmRetrievalScopeError;

#define ATM_RETRIEVAL_SCOPE_ERROR \
    (atm_retrieval_scope_error_quark ())

typedef struct {
    char *repository_id;
    char *index_path;
} AtmRetrievalRepositoryScope;

typedef struct {
    GPtrArray *repositories;
    gboolean explicit_scope;
    gboolean requested_outside_scope;
} AtmRetrievalScopeSelection;

GQuark atm_retrieval_scope_error_quark (void);

AtmRetrievalRepositoryScope *
atm_retrieval_repository_scope_new (
    const char *repository_id,
    const char *index_path
);

void atm_retrieval_repository_scope_free (
    AtmRetrievalRepositoryScope *scope
);

void atm_retrieval_scope_selection_free (
    AtmRetrievalScopeSelection *selection
);

gboolean atm_retrieval_scope_select (
    const char *query,
    const GPtrArray *active_repositories,
    AtmRetrievalScopeSelection **out_selection,
    GError **error
);

G_END_DECLS
