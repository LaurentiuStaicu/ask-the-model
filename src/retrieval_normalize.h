#pragma once

#include <glib.h>

#include "retrieval_rank.h"

G_BEGIN_DECLS

#define ATM_RETRIEVAL_ALIAS_VERSION 1
#define ATM_RETRIEVAL_NORMALIZE_MAX_QUERY_BYTES 4096

typedef enum {
    ATM_RETRIEVAL_NORMALIZE_ERROR_ARGUMENT
} AtmRetrievalNormalizeError;

#define ATM_RETRIEVAL_NORMALIZE_ERROR \
    (atm_retrieval_normalize_error_quark ())

typedef struct {
    char *expanded_text;
    guint intents;
} AtmNormalizedQuery;

GQuark atm_retrieval_normalize_error_quark (void);

gboolean atm_retrieval_normalize_query (
    const char *query,
    AtmNormalizedQuery **out_query,
    GError **error
);

void atm_normalized_query_free (AtmNormalizedQuery *query);

G_END_DECLS
