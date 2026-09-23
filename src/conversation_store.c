#include "conversation_store.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#define ATM_CONVERSATION_STORE_SCHEMA_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v1.sql"

struct AtmConversationStore {
    sqlite3 *db;
    char *path;
    gint64 application_id;
    gint schema_version;
};

GQuark
atm_conversation_store_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-conversation-store-error"
    );
}

static gboolean
nonempty (const char *value)
{
    return value != NULL && value[0] != '\0';
}

static void
set_sqlite_error (
    sqlite3 *db,
    GError **error,
    AtmConversationStoreError code,
    const char *prefix
)
{
    g_set_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        code,
        "%s: %s",
        prefix,
        db != NULL
            ? sqlite3_errmsg (db)
            : "SQLite handle unavailable"
    );
}

static gboolean
exec_sql (
    sqlite3 *db,
    const char *sql,
    GError **error
)
{
    char *message = NULL;
    int rc = sqlite3_exec (
        db,
        sql,
        NULL,
        NULL,
        &message
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Conversation-store SQL failed: %s",
            message != NULL
                ? message
                : sqlite3_errmsg (db)
        );
        sqlite3_free (message);
        return FALSE;
    }

    sqlite3_free (message);
    return TRUE;
}

static gboolean
query_single_int64 (
    sqlite3 *db,
    const char *sql,
    gint64 *out_value,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (out_value == NULL ||
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare conversation-store integer query"
        );
        return FALSE;
    }

    int rc = sqlite3_step (statement);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Conversation-store integer query returned no row"
        );
        return FALSE;
    }

    *out_value = sqlite3_column_int64 (
        statement,
        0
    );

    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store scalar query returned multiple rows."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    return TRUE;
}

static gboolean
query_single_text (
    sqlite3 *db,
    const char *sql,
    char **out_value,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (out_value == NULL ||
        *out_value != NULL ||
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare conversation-store text query"
        );
        return FALSE;
    }

    int rc = sqlite3_step (statement);

    if (rc != SQLITE_ROW ||
        sqlite3_column_type (
            statement,
            0
        ) == SQLITE_NULL) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store text query returned no value."
        );
        return FALSE;
    }

    const unsigned char *value =
        sqlite3_column_text (
            statement,
            0
        );
    *out_value = g_strdup (
        (const char *) value
    );

    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_clear_pointer (
            out_value,
            g_free
        );
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store scalar query returned multiple rows."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    return TRUE;
}

static gboolean
load_schema_sql (
    char **out_sql,
    GError **error
)
{
    if (out_sql == NULL ||
        *out_sql != NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store schema loader received invalid arguments."
        );
        return FALSE;
    }

    GError *resource_error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        ATM_CONVERSATION_STORE_SCHEMA_RESOURCE,
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &resource_error
    );

    if (bytes == NULL) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Could not load conversation-store schema resource: %s",
            resource_error != NULL
                ? resource_error->message
                : "unknown resource error"
        );
        g_clear_error (&resource_error);
        return FALSE;
    }

    gsize size = 0;
    const char *data = g_bytes_get_data (
        bytes,
        &size
    );

    if (data == NULL || size == 0) {
        g_bytes_unref (bytes);
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store schema resource is empty."
        );
        return FALSE;
    }

    *out_sql = g_strndup (
        data,
        size
    );
    g_bytes_unref (bytes);
    return TRUE;
}

static gboolean
configure_db_flag (
    sqlite3 *db,
    int option,
    int requested,
    const char *name,
    GError **error
)
{
    int effective = -1;
    int rc = sqlite3_db_config (
        db,
        option,
        requested,
        &effective
    );

    if (rc != SQLITE_OK ||
        effective != requested) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Could not enforce conversation-store SQLite setting %s=%d (rc=%d, effective=%d).",
            name,
            requested,
            rc,
            effective
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
configure_connection_security (
    sqlite3 *db,
    GError **error
)
{
    if (sqlite3_libversion_number () <
        3037000) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation store requires SQLite >= 3.37.0; runtime is %s.",
            sqlite3_libversion ()
        );
        return FALSE;
    }

    int main_readonly = sqlite3_db_readonly (
        db,
        "main"
    );

    if (main_readonly != 0) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_IO,
            "Conversation-store main database is not writable."
        );
        return FALSE;
    }

    if (sqlite3_busy_timeout (
            db,
            5000
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not configure conversation-store busy timeout"
        );
        return FALSE;
    }

    if (!configure_db_flag (
            db,
            SQLITE_DBCONFIG_DEFENSIVE,
            1,
            "defensive",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_TRUSTED_SCHEMA,
            0,
            "trusted_schema",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_DQS_DML,
            0,
            "dqs_dml",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_DQS_DDL,
            0,
            "dqs_ddl",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_ENABLE_TRIGGER,
            1,
            "enable_trigger",
            error
        )) {
        return FALSE;
    }

    if (!exec_sql (
            db,
            "PRAGMA foreign_keys=ON;",
            error
        ) ||
        !exec_sql (
            db,
            "PRAGMA synchronous=FULL;",
            error
        )) {
        return FALSE;
    }

    gint64 foreign_keys = 0;
    gint64 synchronous = 0;

    if (!query_single_int64 (
            db,
            "PRAGMA foreign_keys;",
            &foreign_keys,
            error
        ) ||
        !query_single_int64 (
            db,
            "PRAGMA synchronous;",
            &synchronous,
            error
        )) {
        return FALSE;
    }

    if (foreign_keys != 1 ||
        synchronous != 2) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store connection safety settings are not active."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
configure_store_journal (
    sqlite3 *db,
    GError **error
)
{
    if (!exec_sql (
            db,
            "PRAGMA journal_mode=WAL;",
            error
        )) {
        return FALSE;
    }

    char *journal_mode = NULL;

    if (!query_single_text (
            db,
            "PRAGMA journal_mode;",
            &journal_mode,
            error
        )) {
        return FALSE;
    }

    gboolean valid =
        g_ascii_strcasecmp (
            journal_mode,
            "wal"
        ) == 0;
    g_free (journal_mode);

    if (!valid) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store WAL journal mode is not active."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
database_has_user_objects (
    sqlite3 *db,
    gboolean *out_has_objects,
    GError **error
)
{
    gint64 count = 0;

    if (!query_single_int64 (
            db,
            "SELECT count(*) FROM sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%';",
            &count,
            error
        )) {
        return FALSE;
    }

    *out_has_objects = count > 0;
    return TRUE;
}

static gboolean
bootstrap_schema (
    sqlite3 *db,
    GError **error
)
{
    char *schema_sql = NULL;

    if (!load_schema_sql (
            &schema_sql,
            error
        )) {
        return FALSE;
    }

    char *bootstrap = g_strdup_printf (
        "BEGIN IMMEDIATE;"
        "%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=%d;"
        "COMMIT;",
        schema_sql,
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID,
        ATM_CONVERSATION_STORE_SCHEMA_VERSION
    );

    gboolean ok = exec_sql (
        db,
        bootstrap,
        error
    );

    if (!ok) {
        sqlite3_exec (
            db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    g_free (bootstrap);
    g_free (schema_sql);
    return ok;
}

static gboolean
required_table_exists (
    sqlite3 *db,
    const char *name,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "SELECT 1 FROM sqlite_master "
            "WHERE type='table' AND name=?1;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare conversation-store schema query"
        );
        return FALSE;
    }

    sqlite3_bind_text (
        statement,
        1,
        name,
        -1,
        SQLITE_STATIC
    );

    gboolean exists =
        sqlite3_step (statement) ==
        SQLITE_ROW;
    sqlite3_finalize (statement);

    if (!exists) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store required table '%s' is missing.",
            name
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_identity (
    AtmConversationStore *store,
    GError **error
)
{
    gint64 application_id = 0;
    gint64 user_version = 0;

    if (!query_single_int64 (
            store->db,
            "PRAGMA application_id;",
            &application_id,
            error
        ) ||
        !query_single_int64 (
            store->db,
            "PRAGMA user_version;",
            &user_version,
            error
        )) {
        return FALSE;
    }

    if (application_id !=
        (gint64) ATM_CONVERSATION_STORE_APPLICATION_ID) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_IDENTITY,
            "Conversation-store application_id is %" G_GINT64_FORMAT
            ", expected %u.",
            application_id,
            (guint) ATM_CONVERSATION_STORE_APPLICATION_ID
        );
        return FALSE;
    }

    if (user_version !=
        ATM_CONVERSATION_STORE_SCHEMA_VERSION) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store user_version is %" G_GINT64_FORMAT
            ", expected %d.",
            user_version,
            ATM_CONVERSATION_STORE_SCHEMA_VERSION
        );
        return FALSE;
    }

    store->application_id = application_id;
    store->schema_version =
        (gint) user_version;
    return TRUE;
}

static gboolean
validate_integrity (
    sqlite3 *db,
    GError **error
)
{
    char *integrity = NULL;

    if (!query_single_text (
            db,
            "PRAGMA integrity_check;",
            &integrity,
            error
        )) {
        return FALSE;
    }

    gboolean integrity_ok =
        g_strcmp0 (
            integrity,
            "ok"
        ) == 0;
    g_free (integrity);

    if (!integrity_ok) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation-store integrity_check did not return exactly 'ok'."
        );
        return FALSE;
    }

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA foreign_key_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare conversation-store foreign_key_check"
        );
        return FALSE;
    }

    int rc = sqlite3_step (statement);
    sqlite3_finalize (statement);

    if (rc != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation-store foreign_key_check reported a violation."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_semantics (
    sqlite3 *db,
    GError **error
)
{
    gint64 invalid_repository_scope = 0;
    gint64 incomplete_turns = 0;
    gint64 grounded_user_messages = 0;
    gint64 invalid_citations = 0;
    gint64 mismatched_citation_provenance = 0;

    if (!query_single_int64 (
            db,
            "SELECT count(*) FROM conversations c "
            "WHERE ("
            "c.repository_generation_id=0 AND EXISTS("
            "SELECT 1 FROM conversation_repositories r "
            "WHERE r.conversation_id=c.conversation_id"
            ")) OR ("
            "c.repository_generation_id>0 AND NOT EXISTS("
            "SELECT 1 FROM conversation_repositories r "
            "WHERE r.conversation_id=c.conversation_id"
            "));",
            &invalid_repository_scope,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) FROM ("
            "SELECT conversation_id, turn_no "
            "FROM messages "
            "GROUP BY conversation_id, turn_no "
            "HAVING count(*) != 2 "
            "OR sum(CASE WHEN role='user' THEN 1 ELSE 0 END) != 1 "
            "OR sum(CASE WHEN role='assistant' THEN 1 ELSE 0 END) != 1"
            ");",
            &incomplete_turns,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) FROM messages "
            "WHERE role='user' AND grounded != 0;",
            &grounded_user_messages,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) "
            "FROM citations c "
            "JOIN messages m ON m.message_id=c.message_id "
            "WHERE m.role!='assistant' OR m.grounded!=1;",
            &invalid_citations,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) "
            "FROM citations c "
            "JOIN messages m ON m.message_id=c.message_id "
            "LEFT JOIN conversation_repositories r "
            "ON r.conversation_id=m.conversation_id "
            "AND r.repository_id=c.repository_id "
            "AND r.repository_version=c.repository_version "
            "AND r.snapshot_sha=c.snapshot_sha "
            "WHERE r.repository_id IS NULL;",
            &mismatched_citation_provenance,
            error
        )) {
        return FALSE;
    }

    if (invalid_repository_scope != 0 ||
        incomplete_turns != 0 ||
        grounded_user_messages != 0 ||
        invalid_citations != 0 ||
        mismatched_citation_provenance != 0) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation-store semantic validation failed "
            "(scope=%" G_GINT64_FORMAT
            ", turns=%" G_GINT64_FORMAT
            ", grounded_users=%" G_GINT64_FORMAT
            ", citations=%" G_GINT64_FORMAT
            ", provenance=%" G_GINT64_FORMAT ").",
            invalid_repository_scope,
            incomplete_turns,
            grounded_user_messages,
            invalid_citations,
            mismatched_citation_provenance
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_conversation_store_validate (
    AtmConversationStore *store,
    GError **error
)
{
    static const char *required_tables[] = {
        "installation",
        "conversations",
        "conversation_repositories",
        "messages",
        "citations"
    };

    if (store == NULL ||
        store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store validation requires an open store."
        );
        return FALSE;
    }

    if (!validate_identity (
            store,
            error
        )) {
        return FALSE;
    }

    for (gsize i = 0;
         i < G_N_ELEMENTS (required_tables);
         i++) {
        if (!required_table_exists (
                store->db,
                required_tables[i],
                error
            )) {
            return FALSE;
        }
    }

    char *schema_id = NULL;

    if (!query_single_text (
            store->db,
            "SELECT schema_id FROM installation "
            "WHERE singleton_id=1;",
            &schema_id,
            error
        )) {
        return FALSE;
    }

    gboolean schema_id_ok =
        g_strcmp0 (
            schema_id,
            ATM_CONVERSATION_STORE_SCHEMA_ID
        ) == 0;
    g_free (schema_id);

    if (!schema_id_ok) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store schema identity is invalid."
        );
        return FALSE;
    }

    return validate_integrity (
            store->db,
            error
        ) &&
        validate_semantics (
            store->db,
            error
        );
}

gboolean
atm_conversation_store_open (
    const char *path,
    AtmConversationStore **out_store,
    GError **error
)
{
    if (!nonempty (path) ||
        out_store == NULL ||
        *out_store != NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store open received invalid arguments."
        );
        return FALSE;
    }

    char *parent = g_path_get_dirname (
        path
    );

    if (g_mkdir_with_parents (
            parent,
            0700
        ) != 0) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_IO,
            "Could not create conversation-store directory: %s.",
            g_strerror (errno)
        );
        g_free (parent);
        return FALSE;
    }

    g_free (parent);

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2 (
        path,
        &db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_NOFOLLOW,
        NULL
    );

    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not open conversation store"
        );
        if (db != NULL) {
            sqlite3_close (db);
        }
        return FALSE;
    }

    AtmConversationStore *store =
        g_new0 (
            AtmConversationStore,
            1
        );
    store->db = db;
    store->path = g_strdup (path);

    if (!configure_connection_security (
            db,
            error
        )) {
        atm_conversation_store_close (
            store
        );
        return FALSE;
    }

    gboolean has_objects = FALSE;

    if (!database_has_user_objects (
            db,
            &has_objects,
            error
        )) {
        atm_conversation_store_close (
            store
        );
        return FALSE;
    }

    gint64 application_id = 0;
    gint64 user_version = 0;

    if (!query_single_int64 (
            db,
            "PRAGMA application_id;",
            &application_id,
            error
        ) ||
        !query_single_int64 (
            db,
            "PRAGMA user_version;",
            &user_version,
            error
        )) {
        atm_conversation_store_close (
            store
        );
        return FALSE;
    }

    if (!has_objects &&
        application_id == 0 &&
        user_version == 0) {
        if (!bootstrap_schema (
                db,
                error
            )) {
            atm_conversation_store_close (
                store
            );
            return FALSE;
        }
    } else if (
        application_id !=
            (gint64) ATM_CONVERSATION_STORE_APPLICATION_ID
    ) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_IDENTITY,
            "Refusing SQLite file with application_id %" G_GINT64_FORMAT
            " as AtM conversation store.",
            application_id
        );
        atm_conversation_store_close (
            store
        );
        return FALSE;
    } else if (
        user_version !=
            ATM_CONVERSATION_STORE_SCHEMA_VERSION
    ) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Unsupported conversation-store user_version %" G_GINT64_FORMAT ".",
            user_version
        );
        atm_conversation_store_close (
            store
        );
        return FALSE;
    }

    if (!atm_conversation_store_validate (
            store,
            error
        )) {
        atm_conversation_store_close (
            store
        );
        return FALSE;
    }

    if (!configure_store_journal (
            db,
            error
        )) {
        atm_conversation_store_close (
            store
        );
        return FALSE;
    }

    *out_store = store;
    return TRUE;
}

void
atm_conversation_store_close (
    AtmConversationStore *store
)
{
    if (store == NULL) {
        return;
    }

    if (store->db != NULL) {
        sqlite3_close (store->db);
    }

    g_free (store->path);
    g_free (store);
}

const char *
atm_conversation_store_path (
    const AtmConversationStore *store
)
{
    return store != NULL
        ? store->path
        : NULL;
}

gint64
atm_conversation_store_application_id (
    const AtmConversationStore *store
)
{
    return store != NULL
        ? store->application_id
        : 0;
}

gint
atm_conversation_store_schema_version (
    const AtmConversationStore *store
)
{
    return store != NULL
        ? store->schema_version
        : 0;
}
