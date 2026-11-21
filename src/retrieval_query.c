#include "retrieval_query.h"
#include "retrieval_fts_consistency.h"

#include <sqlite3.h>

#include <string.h>

static gboolean
validate_query_fts_consistency (sqlite3 *db, GError **error)
{
    GError *index_error = NULL;
    if (atm_retrieval_index_validate_fts_canonical_consistency (
            db, &index_error
        )) {
        return TRUE;
    }

    g_set_error (
        error,
        ATM_RETRIEVAL_QUERY_ERROR,
        ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
        "Retrieval index failed FTS consistency validation: %s",
        index_error != NULL ? index_error->message : "unknown integrity error"
    );
    g_clear_error (&index_error);
    return FALSE;
}

GQuark
atm_retrieval_query_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-query-error-quark"
    );
}

void
atm_evidence_record_free (AtmEvidenceRecord *record)
{
    if (record == NULL) {
        return;
    }

    g_free (record->evidence_kind);
    g_free (record->repository_id);
    g_free (record->repository_version);
    g_free (record->snapshot_sha);
    g_free (record->logical_source_id);
    g_free (record->entity_type);
    g_free (record->relation_type);
    g_free (record->native_id);
    g_free (record->from_logical_source_id);
    g_free (record->to_logical_source_id);
    g_free (record->dataset_logical_source_id);
    g_free (record->row_key);
    g_free (record->source_path);
    g_free (record->locator);
    g_free (record->title);
    g_free (record->body);
    g_free (record);
}

typedef struct {
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
} AtmSnapshotProvenance;

struct _AtmRetrievalQuerySession {
    sqlite3 *db;
    AtmSnapshotProvenance provenance;
};


static void
snapshot_provenance_clear (AtmSnapshotProvenance *provenance)
{
    if (provenance == NULL) {
        return;
    }

    g_clear_pointer (&provenance->repository_id, g_free);
    g_clear_pointer (&provenance->repository_version, g_free);
    g_clear_pointer (&provenance->snapshot_sha, g_free);
}

static gboolean
load_snapshot_provenance (
    sqlite3 *db,
    AtmSnapshotProvenance *out_provenance,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc;

    if (sqlite3_prepare_v2 (
            db,
            "SELECT repository_id, repository_version, snapshot_sha "
            "FROM snapshot_metadata WHERE id = 1;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare snapshot provenance query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    rc = sqlite3_step (statement);

    if (rc != SQLITE_ROW) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Retrieval index has no snapshot provenance: %s",
            sqlite3_errmsg (db)
        );
        sqlite3_finalize (statement);
        return FALSE;
    }

    out_provenance->repository_id = g_strdup (
        (const char *) sqlite3_column_text (statement, 0)
    );
    out_provenance->repository_version = g_strdup (
        (const char *) sqlite3_column_text (statement, 1)
    );
    out_provenance->snapshot_sha = g_strdup (
        (const char *) sqlite3_column_text (statement, 2)
    );

    sqlite3_finalize (statement);
    return TRUE;
}

static void
apply_snapshot_provenance (
    AtmEvidenceRecord *record,
    const AtmSnapshotProvenance *provenance
)
{
    record->repository_id = g_strdup (
        provenance->repository_id
    );
    record->repository_version = g_strdup (
        provenance->repository_version
    );
    record->snapshot_sha = g_strdup (
        provenance->snapshot_sha
    );
}

static char *
duplicate_optional_column (
    sqlite3_stmt *statement,
    int column
)
{
    if (sqlite3_column_type (statement, column) == SQLITE_NULL) {
        return NULL;
    }

    return g_strdup (
        (const char *) sqlite3_column_text (
            statement,
            column
        )
    );
}

static guint
source_role_bit_from_name (const char *role)
{
    if (g_strcmp0 (role, "canonical") == 0) {
        return ATM_SOURCE_ROLE_CANONICAL;
    }

    if (g_strcmp0 (role, "status") == 0) {
        return ATM_SOURCE_ROLE_STATUS;
    }

    if (g_strcmp0 (role, "structural") == 0) {
        return ATM_SOURCE_ROLE_STRUCTURAL;
    }

    if (g_strcmp0 (role, "evidence") == 0) {
        return ATM_SOURCE_ROLE_EVIDENCE;
    }

    if (g_strcmp0 (role, "tabular") == 0) {
        return ATM_SOURCE_ROLE_TABULAR;
    }

    if (g_strcmp0 (role, "implementation") == 0) {
        return ATM_SOURCE_ROLE_IMPLEMENTATION;
    }

    return 0;
}


AtmRetrievalQuerySession *
atm_retrieval_query_session_open (
    const char *index_path,
    GError **error
)
{
    AtmRetrievalQuerySession *session;
    int rc;

    g_return_val_if_fail (index_path != NULL, NULL);

    session = g_new0 (AtmRetrievalQuerySession, 1);
    rc = sqlite3_open_v2 (
        index_path,
        &session->db,
        SQLITE_OPEN_READONLY,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not open retrieval index read-only: %s",
            session->db != NULL
                ? sqlite3_errmsg (session->db)
                : "unknown SQLite error"
        );
        atm_retrieval_query_session_close (session);
        return NULL;
    }

    if (sqlite3_exec (
            session->db,
            "PRAGMA trusted_schema = OFF;"
            "PRAGMA query_only = ON;"
            "PRAGMA foreign_keys = ON;"
            "BEGIN;",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not configure read-only retrieval session: %s",
            sqlite3_errmsg (session->db)
        );
        atm_retrieval_query_session_close (session);
        return NULL;
    }

    if (!load_snapshot_provenance (
            session->db,
            &session->provenance,
            error
        ) ||
        !validate_query_fts_consistency (session->db, error)) {
        atm_retrieval_query_session_close (session);
        return NULL;
    }

    return session;
}

void
atm_retrieval_query_session_close (
    AtmRetrievalQuerySession *session
)
{
    if (session == NULL) {
        return;
    }

    if (session->db != NULL) {
        sqlite3_exec (session->db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close (session->db);
    }

    snapshot_provenance_clear (&session->provenance);
    g_free (session);
}

typedef struct {
    char *repository_id;
    AtmRetrievalQuerySession *session;
} AtmRetrievalQuerySessionCacheEntry;

struct _AtmRetrievalQuerySessionCache {
    GHashTable *sessions_by_index_path;
    guint open_count;
    guint reuse_count;
    guint invalidation_count;
};

static void
query_session_cache_entry_free (
    AtmRetrievalQuerySessionCacheEntry *entry
)
{
    if (entry == NULL) {
        return;
    }

    g_free (entry->repository_id);
    atm_retrieval_query_session_close (entry->session);
    g_free (entry);
}

AtmRetrievalQuerySessionCache *
atm_retrieval_query_session_cache_new (void)
{
    AtmRetrievalQuerySessionCache *cache = g_new0 (
        AtmRetrievalQuerySessionCache,
        1
    );

    cache->sessions_by_index_path = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify) query_session_cache_entry_free
    );

    return cache;
}

void
atm_retrieval_query_session_cache_free (
    AtmRetrievalQuerySessionCache *cache
)
{
    guint sessions = 0;
    guint opens = 0;
    guint reuses = 0;
    guint invalidations = 0;
    gint64 close_started_us = 0;

    if (cache == NULL) {
        return;
    }

    sessions = g_hash_table_size (cache->sessions_by_index_path);
    opens = cache->open_count;
    reuses = cache->reuse_count;
    invalidations = cache->invalidation_count;
    close_started_us = g_get_monotonic_time ();

    /*
     * Emit the close record only after the table's value destructors have
     * closed every SQLite handle owned by this conversation.
     */
    g_clear_pointer (
        &cache->sessions_by_index_path,
        g_hash_table_unref
    );

    if (g_getenv ("ATM_RETRIEVAL_GROUNDING_E4_TIMING") != NULL) {
        g_print (
            "ATM_E4_SESSION_CACHE_CLOSE sessions=%u opens=%u reuses=%u invalidations=%u elapsed_us=%" G_GINT64_FORMAT "\n",
            sessions,
            opens,
            reuses,
            invalidations,
            g_get_monotonic_time () - close_started_us
        );
    }

    g_free (cache);
}

AtmRetrievalQuerySession *
atm_retrieval_query_session_cache_get (
    AtmRetrievalQuerySessionCache *cache,
    const char *repository_id,
    const char *index_path,
    GError **error
)
{
    AtmRetrievalQuerySessionCacheEntry *entry = NULL;
    AtmRetrievalQuerySession *session = NULL;
    gint64 started_us;

    g_return_val_if_fail (cache != NULL, NULL);
    g_return_val_if_fail (repository_id != NULL, NULL);
    g_return_val_if_fail (index_path != NULL, NULL);

    if (repository_id[0] == '\0' ||
        index_path[0] == '\0' ||
        !g_utf8_validate (repository_id, -1, NULL) ||
        !g_utf8_validate (index_path, -1, NULL)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Query-session cache identity is invalid."
        );
        return NULL;
    }

    started_us = g_get_monotonic_time ();
    entry = g_hash_table_lookup (
        cache->sessions_by_index_path,
        index_path
    );

    if (entry != NULL) {
        if (g_strcmp0 (entry->repository_id, repository_id) != 0) {
            g_set_error_literal (
                error,
                ATM_RETRIEVAL_QUERY_ERROR,
                ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
                "One query-session cache index path was assigned to multiple repositories."
            );
            return NULL;
        }

        cache->reuse_count++;
        if (g_getenv ("ATM_RETRIEVAL_GROUNDING_E4_TIMING") != NULL) {
            g_print (
                "ATM_E4_SESSION repository=%s event=reuse elapsed_us=%" G_GINT64_FORMAT "\n",
                repository_id,
                g_get_monotonic_time () - started_us
            );
        }
        return entry->session;
    }

    session = atm_retrieval_query_session_open (index_path, error);
    if (session == NULL) {
        if (g_getenv ("ATM_RETRIEVAL_GROUNDING_E4_TIMING") != NULL) {
            g_print (
                "ATM_E4_SESSION repository=%s event=open_failed elapsed_us=%" G_GINT64_FORMAT "\n",
                repository_id,
                g_get_monotonic_time () - started_us
            );
        }
        return NULL;
    }

    entry = g_new0 (AtmRetrievalQuerySessionCacheEntry, 1);
    entry->repository_id = g_strdup (repository_id);
    entry->session = session;
    g_hash_table_insert (
        cache->sessions_by_index_path,
        g_strdup (index_path),
        entry
    );
    cache->open_count++;

    if (g_getenv ("ATM_RETRIEVAL_GROUNDING_E4_TIMING") != NULL) {
        g_print (
            "ATM_E4_SESSION repository=%s event=open elapsed_us=%" G_GINT64_FORMAT "\n",
            repository_id,
            g_get_monotonic_time () - started_us
        );
    }

    return session;
}

void
atm_retrieval_query_session_cache_invalidate (
    AtmRetrievalQuerySessionCache *cache,
    const char *repository_id,
    const char *index_path
)
{
    AtmRetrievalQuerySessionCacheEntry *entry = NULL;

    g_return_if_fail (cache != NULL);
    g_return_if_fail (repository_id != NULL);
    g_return_if_fail (index_path != NULL);

    entry = g_hash_table_lookup (
        cache->sessions_by_index_path,
        index_path
    );
    if (entry == NULL ||
        g_strcmp0 (entry->repository_id, repository_id) != 0) {
        return;
    }

    g_hash_table_remove (
        cache->sessions_by_index_path,
        index_path
    );
    cache->invalidation_count++;

    if (g_getenv ("ATM_RETRIEVAL_GROUNDING_E4_TIMING") != NULL) {
        g_print (
            "ATM_E4_SESSION repository=%s event=invalidate elapsed_us=0\n",
            repository_id
        );
    }
}

guint
atm_retrieval_query_session_cache_open_count (
    const AtmRetrievalQuerySessionCache *cache
)
{
    return cache != NULL ? cache->open_count : 0;
}

guint
atm_retrieval_query_session_cache_reuse_count (
    const AtmRetrievalQuerySessionCache *cache
)
{
    return cache != NULL ? cache->reuse_count : 0;
}

guint
atm_retrieval_query_session_cache_invalidation_count (
    const AtmRetrievalQuerySessionCache *cache
)
{
    return cache != NULL ? cache->invalidation_count : 0;
}

guint
atm_retrieval_query_session_cache_size (
    const AtmRetrievalQuerySessionCache *cache
)
{
    return cache != NULL
        ? g_hash_table_size (cache->sessions_by_index_path)
        : 0;
}

static gboolean
load_source_roles (
    sqlite3 *db,
    sqlite3_stmt *role_statement,
    sqlite3_int64 source_id,
    guint *out_roles,
    GError **error
)
{
    guint roles = 0;
    int rc;

    sqlite3_reset (role_statement);
    sqlite3_clear_bindings (role_statement);
    sqlite3_bind_int64 (
        role_statement,
        1,
        source_id
    );

    while ((rc = sqlite3_step (role_statement)) == SQLITE_ROW) {
        const char *name = (const char *) sqlite3_column_text (
            role_statement,
            0
        );
        guint bit = source_role_bit_from_name (name);

        if (bit == 0) {
            g_set_error (
                error,
                ATM_RETRIEVAL_QUERY_ERROR,
                ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
                "Retrieval index contains unknown source role '%s'.",
                name != NULL ? name : "(null)"
            );
            return FALSE;
        }

        roles |= bit;
    }

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not read source roles: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    *out_roles = roles;
    return TRUE;
}

static gboolean
append_exact_rows (
    sqlite3 *db,
    sqlite3_stmt *role_statement,
    const char *sql,
    const char *identifier,
    const char *evidence_kind,
    const AtmSnapshotProvenance *provenance,
    guint maximum_rows,
    GPtrArray *results,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc;

    if (maximum_rows == 0) {
        return TRUE;
    }

    rc = sqlite3_prepare_v2 (
        db,
        sql,
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare exact retrieval query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    sqlite3_bind_text (
        statement,
        1,
        identifier,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_int (
        statement,
        2,
        (int) maximum_rows
    );

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        AtmEvidenceRecord *record = g_new0 (
            AtmEvidenceRecord,
            1
        );
        sqlite3_int64 source_id = sqlite3_column_int64 (
            statement,
            6
        );

        record->evidence_kind = g_strdup (evidence_kind);
        record->match_kind = ATM_EVIDENCE_MATCH_EXACT;
        apply_snapshot_provenance (record, provenance);
        record->evidence_id = sqlite3_column_int64 (
            statement,
            0
        );
        record->logical_source_id = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                1
            )
        );
        record->source_path = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                2
            )
        );
        record->locator = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                3
            )
        );

        if (sqlite3_column_type (statement, 4) != SQLITE_NULL) {
            record->title = g_strdup (
                (const char *) sqlite3_column_text (
                    statement,
                    4
                )
            );
        }

        if (sqlite3_column_type (statement, 5) != SQLITE_NULL) {
            record->body = g_strdup (
                (const char *) sqlite3_column_text (
                    statement,
                    5
                )
            );
        }

        record->entity_type = duplicate_optional_column (
            statement,
            7
        );
        record->relation_type = duplicate_optional_column (
            statement,
            8
        );
        record->native_id = duplicate_optional_column (
            statement,
            9
        );
        record->from_logical_source_id = duplicate_optional_column (
            statement,
            10
        );
        record->to_logical_source_id = duplicate_optional_column (
            statement,
            11
        );
        record->dataset_logical_source_id = duplicate_optional_column (
            statement,
            12
        );
        record->row_key = duplicate_optional_column (
            statement,
            13
        );

        if (!load_source_roles (
                db,
                role_statement,
                source_id,
                &record->source_roles,
                error
            )) {
            atm_evidence_record_free (record);
            sqlite3_finalize (statement);
            return FALSE;
        }

        g_ptr_array_add (results, record);
    }

    sqlite3_finalize (statement);

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not finish exact retrieval query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_retrieval_query_session_lookup_exact (
    AtmRetrievalQuerySession *session,
    const char *identifier,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    static const char *entity_sql =
        "SELECT e.id, e.logical_source_id, s.path, e.locator, "
        "COALESCE(e.label, e.native_id), e.payload_json, s.id, "
        "e.entity_type, NULL, e.native_id, NULL, NULL, NULL, NULL "
        "FROM structured_entities e "
        "JOIN source_files s ON s.id = e.source_id "
        "WHERE e.native_id = ?1 COLLATE BINARY "
        "OR e.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, e.locator COLLATE BINARY "
        "LIMIT ?2;";

    static const char *relation_sql =
        "SELECT r.id, r.logical_source_id, s.path, r.locator, "
        "COALESCE(r.native_id, r.relation_type), r.payload_json, s.id, "
        "NULL, r.relation_type, r.native_id, "
        "r.from_logical_source_id, r.to_logical_source_id, NULL, NULL "
        "FROM structured_relations r "
        "JOIN source_files s ON s.id = r.source_id "
        "WHERE r.native_id = ?1 COLLATE BINARY "
        "OR r.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, r.locator COLLATE BINARY "
        "LIMIT ?2;";

    static const char *dataset_sql =
        "SELECT d.id, d.logical_source_id, s.path, d.locator, "
        "COALESCE(d.title, d.native_id, d.logical_source_id), "
        "d.metadata_json, s.id, "
        "NULL, NULL, d.native_id, NULL, NULL, d.logical_source_id, NULL "
        "FROM datasets d "
        "JOIN source_files s ON s.id = d.source_id "
        "WHERE d.native_id = ?1 COLLATE BINARY "
        "OR d.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, d.locator COLLATE BINARY "
        "LIMIT ?2;";

    static const char *section_sql =
        "SELECT d.id, d.logical_source_id, s.path, d.locator, "
        "COALESCE(d.title, d.heading_path, d.logical_source_id), "
        "d.body, s.id, "
        "NULL, NULL, NULL, NULL, NULL, NULL, NULL "
        "FROM document_sections d "
        "JOIN source_files s ON s.id = d.source_id "
        "WHERE d.logical_source_id = ?1 COLLATE BINARY "
        "ORDER BY s.path COLLATE BINARY, d.locator COLLATE BINARY "
        "LIMIT ?2;";

    sqlite3_stmt *role_statement = NULL;
    GPtrArray *results = NULL;
    gboolean ok = FALSE;
    int rc;

    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (identifier != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    gsize identifier_length = strlen (identifier);

    if (identifier_length == 0 ||
        identifier_length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
        max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS ||
        !g_utf8_validate (identifier, identifier_length, NULL)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Exact retrieval arguments are invalid."
        );
        return FALSE;
    }

    sqlite3 *db = session->db;
    const AtmSnapshotProvenance *provenance = &session->provenance;


    if (sqlite3_prepare_v2 (
            db,
            "SELECT role FROM source_roles "
            "WHERE source_id = ?1 ORDER BY role COLLATE BINARY;",
            -1,
            &role_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare source-role retrieval query: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    results = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_evidence_record_free
    );

    const struct {
        const char *sql;
        const char *kind;
    } queries[] = {
        { entity_sql, "entity" },
        { relation_sql, "relation" },
        { dataset_sql, "dataset" },
        { section_sql, "section" }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (queries); i++) {
        guint remaining = max_results - results->len;

        if (remaining == 0) {
            break;
        }

        if (!append_exact_rows (
                db,
                role_statement,
                queries[i].sql,
                identifier,
                queries[i].kind,
                provenance,
                remaining,
                results,
                error
            )) {
            goto out;
        }
    }

    *out_results = g_steal_pointer (&results);
    ok = TRUE;

out:
    g_clear_pointer (&results, g_ptr_array_unref);
    

    if (role_statement != NULL) {
        sqlite3_finalize (role_statement);
    }


    return ok;
}


#define ATM_RETRIEVAL_MAX_FTS_TERMS 16
#define ATM_RETRIEVAL_MAX_FTS_QUERY_BYTES 4096

static gboolean
fts_term_is_stopword (const char *folded)
{
    static const char *stopwords[] = {
        "a", "an", "and", "are", "as", "at",
        "be", "by", "does", "for", "from",
        "how", "in", "is", "of", "on", "or",
        "the", "to", "what", "which", "with",
        "și", "si", "în", "in", "de", "din",
        "este", "sunt", "ce", "care", "cu",
        "la", "pe", "pentru", "sau", "un",
        "o", "ale", "al", "a"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (stopwords); i++) {
        if (g_strcmp0 (folded, stopwords[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
flush_fts_term (
    GString *token,
    GHashTable *seen,
    GPtrArray *terms
)
{
    if (token->len == 0 ||
        terms->len >= ATM_RETRIEVAL_MAX_FTS_TERMS) {
        g_string_set_size (token, 0);
        return;
    }

    char *folded = g_utf8_casefold (
        token->str,
        token->len
    );

    if (fts_term_is_stopword (folded)) {
        g_free (folded);
        g_string_set_size (token, 0);
        return;
    }

    if (!g_hash_table_contains (seen, folded)) {
        g_hash_table_add (seen, folded);
        g_ptr_array_add (
            terms,
            g_strdup (token->str)
        );
    } else {
        g_free (folded);
    }

    g_string_set_size (token, 0);
}

static char *
safe_fts_query (
    const char *query,
    GError **error
)
{
    gsize length = strlen (query);

    if (length == 0 ||
        length > ATM_RETRIEVAL_MAX_FTS_QUERY_BYTES ||
        !g_utf8_validate (query, length, NULL)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "FTS retrieval query is invalid."
        );
        return NULL;
    }

    GPtrArray *terms = g_ptr_array_new_with_free_func (g_free);
    GHashTable *seen = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );
    GString *token = g_string_new (NULL);

    for (const char *cursor = query;
         *cursor != '\0';) {
        gunichar character = g_utf8_get_char (cursor);

        if (g_unichar_isalnum (character) ||
            character == '_') {
            gchar encoded[7] = { 0 };
            gint count = g_unichar_to_utf8 (
                character,
                encoded
            );
            g_string_append_len (
                token,
                encoded,
                count
            );
        } else {
            flush_fts_term (
                token,
                seen,
                terms
            );

            if (terms->len >= ATM_RETRIEVAL_MAX_FTS_TERMS) {
                break;
            }
        }

        cursor = g_utf8_next_char (cursor);
    }

    flush_fts_term (token, seen, terms);
    g_string_free (token, TRUE);
    g_hash_table_unref (seen);

    if (terms->len == 0) {
        g_ptr_array_unref (terms);
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "FTS retrieval query contains no searchable terms."
        );
        return NULL;
    }

    GString *fts = g_string_new (NULL);

    for (guint i = 0; i < terms->len; i++) {
        if (i > 0) {
            g_string_append (fts, " OR ");
        }

        g_string_append_c (fts, '"');
        g_string_append (
            fts,
            g_ptr_array_index (terms, i)
        );
        g_string_append_c (fts, '"');
    }

    g_ptr_array_unref (terms);
    return g_string_free (fts, FALSE);
}

static gboolean
prepare_provenance_statement (
    sqlite3 *db,
    const char *sql,
    sqlite3_stmt **out_statement,
    GError **error
)
{
    if (sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            out_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare evidence provenance query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
load_fts_provenance (
    sqlite3_stmt *section_statement,
    sqlite3_stmt *entity_statement,
    sqlite3_stmt *relation_statement,
    sqlite3_stmt *row_statement,
    const char *kind,
    sqlite3_int64 evidence_id,
    AtmEvidenceRecord *record,
    sqlite3_int64 *out_source_id,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (g_strcmp0 (kind, "section") == 0) {
        statement = section_statement;
    } else if (g_strcmp0 (kind, "entity") == 0) {
        statement = entity_statement;
    } else if (g_strcmp0 (kind, "relation") == 0) {
        statement = relation_statement;
    } else if (g_strcmp0 (kind, "dataset_row") == 0) {
        statement = row_statement;
    } else {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "FTS row has unsupported evidence kind '%s'.",
            kind != NULL ? kind : "(null)"
        );
        return FALSE;
    }

    sqlite3_reset (statement);
    sqlite3_clear_bindings (statement);
    sqlite3_bind_int64 (
        statement,
        1,
        evidence_id
    );

    if (sqlite3_step (statement) != SQLITE_ROW) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "FTS evidence row '%s:%" G_GINT64_FORMAT "' has no canonical provenance.",
            kind,
            (gint64) evidence_id
        );
        return FALSE;
    }

    record->source_path = g_strdup (
        (const char *) sqlite3_column_text (
            statement,
            0
        )
    );
    record->locator = g_strdup (
        (const char *) sqlite3_column_text (
            statement,
            1
        )
    );
    *out_source_id = sqlite3_column_int64 (
        statement,
        2
    );
    record->entity_type = duplicate_optional_column (
        statement,
        3
    );
    record->relation_type = duplicate_optional_column (
        statement,
        4
    );
    record->native_id = duplicate_optional_column (
        statement,
        5
    );
    record->from_logical_source_id = duplicate_optional_column (
        statement,
        6
    );
    record->to_logical_source_id = duplicate_optional_column (
        statement,
        7
    );
    record->dataset_logical_source_id = duplicate_optional_column (
        statement,
        8
    );
    record->row_key = duplicate_optional_column (
        statement,
        9
    );

    return TRUE;
}

gboolean
atm_retrieval_query_session_search_fts (
    AtmRetrievalQuerySession *session,
    const char *query,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    sqlite3_stmt *search_statement = NULL;
    sqlite3_stmt *role_statement = NULL;
    sqlite3_stmt *section_statement = NULL;
    sqlite3_stmt *entity_statement = NULL;
    sqlite3_stmt *relation_statement = NULL;
    sqlite3_stmt *row_statement = NULL;
    GPtrArray *results = NULL;
    char *fts_query = NULL;
    gboolean ok = FALSE;
    int rc;

    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    if (max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "FTS result limit is invalid."
        );
        return FALSE;
    }

    fts_query = safe_fts_query (
        query,
        error
    );

    if (fts_query == NULL) {
        return FALSE;
    }

    sqlite3 *db = session->db;
    const AtmSnapshotProvenance *provenance = &session->provenance;


    if (sqlite3_prepare_v2 (
            db,
            "SELECT evidence_kind, evidence_id, logical_source_id, "
            "title, body, bm25(search_fts, 0.0, 0.0, 0.0, 4.0, 1.0) "
            "FROM search_fts "
            "WHERE search_fts MATCH ?1 "
            "ORDER BY bm25(search_fts, 0.0, 0.0, 0.0, 4.0, 1.0), rowid "
            "LIMIT ?2;",
            -1,
            &search_statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "SELECT role FROM source_roles "
            "WHERE source_id = ?1 ORDER BY role COLLATE BINARY;",
            -1,
            &role_statement,
            NULL
        ) != SQLITE_OK ||
        !prepare_provenance_statement (
            db,
            "SELECT s.path, d.locator, s.id, "
            "NULL, NULL, NULL, NULL, NULL, NULL, NULL "
            "FROM document_sections d "
            "JOIN source_files s ON s.id = d.source_id "
            "WHERE d.id = ?1;",
            &section_statement,
            error
        ) ||
        !prepare_provenance_statement (
            db,
            "SELECT s.path, e.locator, s.id, "
            "e.entity_type, NULL, e.native_id, NULL, NULL, NULL, NULL "
            "FROM structured_entities e "
            "JOIN source_files s ON s.id = e.source_id "
            "WHERE e.id = ?1;",
            &entity_statement,
            error
        ) ||
        !prepare_provenance_statement (
            db,
            "SELECT s.path, r.locator, s.id, "
            "NULL, r.relation_type, r.native_id, "
            "r.from_logical_source_id, r.to_logical_source_id, NULL, NULL "
            "FROM structured_relations r "
            "JOIN source_files s ON s.id = r.source_id "
            "WHERE r.id = ?1;",
            &relation_statement,
            error
        ) ||
        !prepare_provenance_statement (
            db,
            "SELECT s.path, r.locator, s.id, "
            "NULL, NULL, NULL, NULL, NULL, d.logical_source_id, r.row_key "
            "FROM dataset_rows r "
            "JOIN datasets d ON d.id = r.dataset_id "
            "JOIN source_files s ON s.id = d.source_id "
            "WHERE r.id = ?1;",
            &row_statement,
            error
        )) {
        if (error != NULL && *error == NULL) {
            g_set_error (
                error,
                ATM_RETRIEVAL_QUERY_ERROR,
                ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
                "Could not prepare FTS retrieval query: %s",
                sqlite3_errmsg (db)
            );
        }
        goto out;
    }

    sqlite3_bind_text (
        search_statement,
        1,
        fts_query,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_int (
        search_statement,
        2,
        (int) max_results
    );

    results = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_evidence_record_free
    );

    while ((rc = sqlite3_step (search_statement)) == SQLITE_ROW) {
        const char *kind =
            (const char *) sqlite3_column_text (
                search_statement,
                0
            );
        sqlite3_int64 evidence_id = sqlite3_column_int64 (
            search_statement,
            1
        );
        AtmEvidenceRecord *record = g_new0 (
            AtmEvidenceRecord,
            1
        );
        sqlite3_int64 source_id = 0;

        record->evidence_kind = g_strdup (kind);
        record->match_kind = ATM_EVIDENCE_MATCH_LEXICAL;
        apply_snapshot_provenance (record, provenance);
        record->evidence_id = evidence_id;
        record->logical_source_id = g_strdup (
            (const char *) sqlite3_column_text (
                search_statement,
                2
            )
        );

        if (sqlite3_column_type (
                search_statement,
                3
            ) != SQLITE_NULL) {
            record->title = g_strdup (
                (const char *) sqlite3_column_text (
                    search_statement,
                    3
                )
            );
        }

        if (sqlite3_column_type (
                search_statement,
                4
            ) != SQLITE_NULL) {
            record->body = g_strdup (
                (const char *) sqlite3_column_text (
                    search_statement,
                    4
                )
            );
        }

        record->has_lexical_score = TRUE;
        record->lexical_score = sqlite3_column_double (
            search_statement,
            5
        );

        if (!load_fts_provenance (
                section_statement,
                entity_statement,
                relation_statement,
                row_statement,
                kind,
                evidence_id,
                record,
                &source_id,
                error
            ) ||
            !load_source_roles (
                db,
                role_statement,
                source_id,
                &record->source_roles,
                error
            )) {
            atm_evidence_record_free (record);
            goto out;
        }

        g_ptr_array_add (results, record);
    }

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not finish FTS retrieval query: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    *out_results = g_steal_pointer (&results);
    ok = TRUE;

out:
    g_clear_pointer (&results, g_ptr_array_unref);
    g_clear_pointer (&fts_query, g_free);
    

    if (row_statement != NULL) {
        sqlite3_finalize (row_statement);
    }

    if (relation_statement != NULL) {
        sqlite3_finalize (relation_statement);
    }

    if (entity_statement != NULL) {
        sqlite3_finalize (entity_statement);
    }

    if (section_statement != NULL) {
        sqlite3_finalize (section_statement);
    }

    if (role_statement != NULL) {
        sqlite3_finalize (role_statement);
    }

    if (search_statement != NULL) {
        sqlite3_finalize (search_statement);
    }


    return ok;
}


gboolean
atm_retrieval_query_session_lookup_dataset_rows (
    AtmRetrievalQuerySession *session,
    const char *dataset_identifier,
    const char *row_key,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    sqlite3_stmt *role_statement = NULL;
    GPtrArray *results = NULL;
    gboolean ok = FALSE;
    int rc;

    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (row_key != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    gsize row_key_length = strlen (row_key);

    if (row_key_length == 0 ||
        row_key_length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
        !g_utf8_validate (row_key, row_key_length, NULL) ||
        max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Tabular row lookup arguments are invalid."
        );
        return FALSE;
    }

    if (dataset_identifier != NULL) {
        gsize dataset_length = strlen (dataset_identifier);

        if (dataset_length == 0 ||
            dataset_length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
            !g_utf8_validate (
                dataset_identifier,
                dataset_length,
                NULL
            )) {
            g_set_error_literal (
                error,
                ATM_RETRIEVAL_QUERY_ERROR,
                ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
                "Dataset identifier is invalid."
            );
            return FALSE;
        }
    }

    sqlite3 *db = session->db;
    const AtmSnapshotProvenance *provenance = &session->provenance;


    if (sqlite3_prepare_v2 (
            db,
            "SELECT r.id, m.repository_id, s.path, r.ordinal, "
            "r.locator, r.row_key, r.payload_json, s.id, "
            "d.logical_source_id "
            "FROM dataset_rows r "
            "JOIN datasets d ON d.id = r.dataset_id "
            "JOIN source_files s ON s.id = d.source_id "
            "JOIN snapshot_metadata m ON m.id = 1 "
            "WHERE r.row_key = ?1 COLLATE BINARY "
            "AND (?2 IS NULL "
                 "OR d.native_id = ?2 COLLATE BINARY "
                 "OR d.logical_source_id = ?2 COLLATE BINARY "
                 "OR s.path = ?2 COLLATE BINARY "
                 "OR (length(s.path) > length(?2) "
                     "AND substr(s.path, -length(?2)) = ?2 COLLATE BINARY "
                     "AND substr("
                         "s.path, "
                         "length(s.path) - length(?2), "
                         "1"
                     ") = '/')) "
            "ORDER BY s.path COLLATE BINARY, "
            "d.logical_source_id COLLATE BINARY, r.ordinal "
            "LIMIT ?3;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK ||
        sqlite3_prepare_v2 (
            db,
            "SELECT role FROM source_roles "
            "WHERE source_id = ?1 ORDER BY role COLLATE BINARY;",
            -1,
            &role_statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not prepare tabular row lookup: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    sqlite3_bind_text (
        statement,
        1,
        row_key,
        -1,
        SQLITE_STATIC
    );

    if (dataset_identifier != NULL) {
        sqlite3_bind_text (
            statement,
            2,
            dataset_identifier,
            -1,
            SQLITE_STATIC
        );
    } else {
        sqlite3_bind_null (statement, 2);
    }

    sqlite3_bind_int (
        statement,
        3,
        (int) max_results
    );

    results = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_evidence_record_free
    );

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        const char *repository_id =
            (const char *) sqlite3_column_text (
                statement,
                1
            );
        const char *source_path =
            (const char *) sqlite3_column_text (
                statement,
                2
            );
        guint ordinal = (guint) sqlite3_column_int (
            statement,
            3
        );
        sqlite3_int64 source_id = sqlite3_column_int64 (
            statement,
            7
        );
        AtmEvidenceRecord *record = g_new0 (
            AtmEvidenceRecord,
            1
        );

        record->evidence_kind = g_strdup ("dataset_row");
        record->match_kind = ATM_EVIDENCE_MATCH_TABULAR;
        apply_snapshot_provenance (record, provenance);
        record->evidence_id = sqlite3_column_int64 (
            statement,
            0
        );
        record->logical_source_id = g_strdup_printf (
            "%s:dataset-row:%s:%u",
            repository_id,
            source_path,
            ordinal
        );
        record->source_path = g_strdup (source_path);
        record->locator = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                4
            )
        );
        record->title = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                5
            )
        );
        record->body = g_strdup (
            (const char *) sqlite3_column_text (
                statement,
                6
            )
        );
        record->dataset_logical_source_id = duplicate_optional_column (
            statement,
            8
        );
        record->row_key = duplicate_optional_column (
            statement,
            5
        );

        if (!load_source_roles (
                db,
                role_statement,
                source_id,
                &record->source_roles,
                error
            )) {
            atm_evidence_record_free (record);
            goto out;
        }

        g_ptr_array_add (results, record);
    }

    if (rc != SQLITE_DONE) {
        g_set_error (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_SQLITE,
            "Could not finish tabular row lookup: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    *out_results = g_steal_pointer (&results);
    ok = TRUE;

out:
    g_clear_pointer (&results, g_ptr_array_unref);
    

    if (role_statement != NULL) {
        sqlite3_finalize (role_statement);
    }

    if (statement != NULL) {
        sqlite3_finalize (statement);
    }


    return ok;
}


gboolean
atm_retrieval_lookup_exact (
    const char *index_path,
    const char *identifier,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    AtmRetrievalQuerySession *session;
    gboolean ok;
    gsize length;

    g_return_val_if_fail (index_path != NULL, FALSE);
    g_return_val_if_fail (identifier != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    length = strlen (identifier);
    if (length == 0 ||
        length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
        max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS ||
        !g_utf8_validate (identifier, length, NULL)) {
        g_set_error_literal (error, ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Exact retrieval arguments are invalid.");
        return FALSE;
    }

    session = atm_retrieval_query_session_open (index_path, error);
    if (session == NULL) return FALSE;
    ok = atm_retrieval_query_session_lookup_exact (
        session, identifier, max_results, out_results, error
    );
    atm_retrieval_query_session_close (session);
    return ok;
}

gboolean
atm_retrieval_search_fts (
    const char *index_path,
    const char *query,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    AtmRetrievalQuerySession *session;
    char *validated_query;
    gboolean ok;

    g_return_val_if_fail (index_path != NULL, FALSE);
    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    if (max_results == 0 || max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (error, ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT, "FTS result limit is invalid.");
        return FALSE;
    }

    validated_query = safe_fts_query (query, error);
    if (validated_query == NULL) return FALSE;
    g_free (validated_query);

    session = atm_retrieval_query_session_open (index_path, error);
    if (session == NULL) return FALSE;
    ok = atm_retrieval_query_session_search_fts (
        session, query, max_results, out_results, error
    );
    atm_retrieval_query_session_close (session);
    return ok;
}

gboolean
atm_retrieval_lookup_dataset_rows (
    const char *index_path,
    const char *dataset_identifier,
    const char *row_key,
    guint max_results,
    GPtrArray **out_results,
    GError **error
)
{
    AtmRetrievalQuerySession *session;
    gsize length;

    g_return_val_if_fail (index_path != NULL, FALSE);
    g_return_val_if_fail (row_key != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    length = strlen (row_key);
    if (length == 0 ||
        length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
        !g_utf8_validate (row_key, length, NULL) ||
        max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (error, ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Tabular row lookup arguments are invalid.");
        return FALSE;
    }

    if (dataset_identifier != NULL) {
        gsize dataset_length = strlen (dataset_identifier);
        if (dataset_length == 0 ||
            dataset_length > ATM_RETRIEVAL_MAX_IDENTIFIER_BYTES ||
            !g_utf8_validate (dataset_identifier, dataset_length, NULL)) {
            g_set_error_literal (error, ATM_RETRIEVAL_QUERY_ERROR,
                ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
                "Dataset identifier is invalid.");
            return FALSE;
        }
    }

    session = atm_retrieval_query_session_open (index_path, error);
    if (session == NULL) return FALSE;
    {
        gboolean ok = atm_retrieval_query_session_lookup_dataset_rows (
            session, dataset_identifier, row_key, max_results, out_results, error
        );
        atm_retrieval_query_session_close (session);
        return ok;
    }
}
