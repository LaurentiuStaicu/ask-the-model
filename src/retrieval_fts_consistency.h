#pragma once

#include <glib.h>
#include <sqlite3.h>

G_BEGIN_DECLS

gboolean atm_retrieval_index_validate_fts_canonical_consistency (
    sqlite3 *db,
    GError **error
);

G_END_DECLS
