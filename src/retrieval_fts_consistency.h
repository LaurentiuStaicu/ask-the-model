#pragma once

#include <glib.h>
#include <sqlite3.h>

G_BEGIN_DECLS

gboolean atm_retrieval_index_validate_fts_canonical_consistency (
    sqlite3 *db,
    GError **error
);

#ifdef ATM_RETRIEVAL_VALIDATOR_TESTING
gboolean atm_retrieval_index_validate_fts_canonical_consistency_for_test (
    sqlite3 *db,
    gboolean use_original_predicate,
    GError **error
);
#endif

G_END_DECLS
