#pragma once

#include <glib.h>

#include "retrieval_query.h"

G_BEGIN_DECLS

typedef enum {
    ATM_RETRIEVAL_INTENT_GENERAL = 0,
    ATM_RETRIEVAL_INTENT_CURRENT_STATE = 1u << 0,
    ATM_RETRIEVAL_INTENT_STRUCTURE = 1u << 1,
    ATM_RETRIEVAL_INTENT_EVIDENCE = 1u << 2,
    ATM_RETRIEVAL_INTENT_NUMERIC = 1u << 3,
    ATM_RETRIEVAL_INTENT_IMPLEMENTATION = 1u << 4
} AtmRetrievalIntent;

guint atm_evidence_authority_rank (
    const AtmEvidenceRecord *record,
    guint intents
);

gboolean atm_retrieval_rank_and_deduplicate (
    GPtrArray *results,
    guint intents,
    guint max_results,
    GError **error
);

G_END_DECLS
