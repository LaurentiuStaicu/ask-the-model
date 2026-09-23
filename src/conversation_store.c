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


static gboolean
is_sha40 (
    const char *value
)
{
    if (value == NULL ||
        strlen (value) != 40) {
        return FALSE;
    }

    for (const char *cursor = value;
         *cursor != '\0';
         cursor++) {
        if (!((*cursor >= '0' && *cursor <= '9') ||
              (*cursor >= 'a' && *cursor <= 'f'))) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
prepare_statement (
    sqlite3 *db,
    const char *sql,
    sqlite3_stmt **out_statement,
    GError **error
)
{
    if (out_statement == NULL ||
        *out_statement != NULL ||
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            out_statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare conversation-store mutation"
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
step_done (
    sqlite3 *db,
    sqlite3_stmt *statement,
    const char *prefix,
    GError **error
)
{
    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            prefix
        );
        return FALSE;
    }

    return TRUE;
}

static void
rollback_best_effort (
    sqlite3 *db
)
{
    sqlite3_exec (
        db,
        "ROLLBACK;",
        NULL,
        NULL,
        NULL
    );
}

static gboolean
repository_input_is_valid (
    const AtmConversationRepositoryInput *repository
)
{
    return repository != NULL &&
        nonempty (repository->repository_id) &&
        nonempty (repository->repository_version) &&
        is_sha40 (repository->snapshot_sha);
}

static gboolean
citation_input_is_valid (
    const AtmConversationCitationInput *citation
)
{
    return citation != NULL &&
        nonempty (citation->label) &&
        nonempty (citation->repository_id) &&
        nonempty (citation->repository_version) &&
        is_sha40 (citation->snapshot_sha) &&
        nonempty (citation->logical_source_id) &&
        nonempty (citation->source_path) &&
        nonempty (citation->locator);
}

gboolean
atm_conversation_store_create_conversation (
    AtmConversationStore *store,
    const char *title,
    gint64 created_at_us,
    const char *model_name,
    const char *model_digest,
    gint64 repository_generation_id,
    const AtmConversationRepositoryInput *repositories,
    gsize repository_count,
    char **out_conversation_id,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (title) ||
        created_at_us < 0 ||
        !nonempty (model_name) ||
        (model_digest != NULL &&
         !nonempty (model_digest)) ||
        repository_generation_id < 0 ||
        (repository_count > 0 &&
         repositories == NULL) ||
        out_conversation_id == NULL ||
        *out_conversation_id != NULL ||
        (repository_generation_id == 0 &&
         repository_count != 0) ||
        (repository_generation_id > 0 &&
         repository_count == 0)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation creation received invalid arguments."
        );
        return FALSE;
    }

    for (gsize i = 0;
         i < repository_count;
         i++) {
        if (!repository_input_is_valid (
                &repositories[i]
            )) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
                "Conversation repository identity is invalid."
            );
            return FALSE;
        }
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *statement = NULL;
    char *conversation_id =
        g_uuid_string_random ();

    if (!prepare_statement (
            store->db,
            "INSERT INTO conversations("
            "conversation_id,title,created_at_us,updated_at_us,"
            "model_name,model_digest,repository_generation_id,archived"
            ") VALUES(?1,?2,?3,?3,?4,?5,?6,0);",
            &statement,
            error
        )) {
        goto out;
    }

    sqlite3_bind_text (
        statement,
        1,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        2,
        title,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int64 (
        statement,
        3,
        created_at_us
    );
    sqlite3_bind_text (
        statement,
        4,
        model_name,
        -1,
        SQLITE_TRANSIENT
    );

    if (model_digest != NULL) {
        sqlite3_bind_text (
            statement,
            5,
            model_digest,
            -1,
            SQLITE_TRANSIENT
        );
    } else {
        sqlite3_bind_null (
            statement,
            5
        );
    }

    sqlite3_bind_int64 (
        statement,
        6,
        repository_generation_id
    );

    if (!step_done (
            store->db,
            statement,
            "Could not create conversation",
            error
        )) {
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (repository_count > 0) {
        if (!prepare_statement (
                store->db,
                "INSERT INTO conversation_repositories("
                "conversation_id,repository_id,repository_version,snapshot_sha"
                ") VALUES(?1,?2,?3,?4);",
                &statement,
                error
            )) {
            goto out;
        }

        for (gsize i = 0;
             i < repository_count;
             i++) {
            sqlite3_reset (statement);
            sqlite3_clear_bindings (
                statement
            );

            sqlite3_bind_text (
                statement,
                1,
                conversation_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                2,
                repositories[i].repository_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                3,
                repositories[i].repository_version,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                4,
                repositories[i].snapshot_sha,
                -1,
                SQLITE_TRANSIENT
            );

            if (!step_done (
                    store->db,
                    statement,
                    "Could not pin conversation repository",
                    error
                )) {
                goto out;
            }
        }

        sqlite3_finalize (statement);
        statement = NULL;
    }

    if (!exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto out;
    }

    *out_conversation_id =
        g_steal_pointer (
            &conversation_id
        );
    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (
            statement
        );
    }

    if (!ok) {
        rollback_best_effort (
            store->db
        );
    }

    g_free (conversation_id);
    return ok;
}

static gboolean
load_conversation_write_state (
    sqlite3 *db,
    const char *conversation_id,
    gint64 *out_repository_generation_id,
    gint64 *out_turn_no,
    gint64 *out_sequence_no,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT "
            "c.repository_generation_id,"
            "COALESCE(MAX(m.turn_no)+1,0),"
            "COALESCE(MAX(m.sequence_no)+1,0) "
            "FROM conversations c "
            "LEFT JOIN messages m "
            "ON m.conversation_id=c.conversation_id "
            "WHERE c.conversation_id=?1 "
            "GROUP BY c.conversation_id,c.repository_generation_id;",
            &statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_text (
        statement,
        1,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );

    int rc = sqlite3_step (
        statement
    );

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (
            statement
        );
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation does not exist in the durable store."
        );
        return FALSE;
    }

    *out_repository_generation_id =
        sqlite3_column_int64 (
            statement,
            0
        );
    *out_turn_no =
        sqlite3_column_int64 (
            statement,
            1
        );
    *out_sequence_no =
        sqlite3_column_int64 (
            statement,
            2
        );

    sqlite3_finalize (
        statement
    );
    return TRUE;
}

static gboolean
citation_matches_conversation_scope (
    sqlite3 *db,
    const char *conversation_id,
    const AtmConversationCitationInput *citation,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT 1 "
            "FROM conversation_repositories "
            "WHERE conversation_id=?1 "
            "AND repository_id=?2 "
            "AND repository_version=?3 "
            "AND snapshot_sha=?4;",
            &statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_text (
        statement,
        1,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        2,
        citation->repository_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        3,
        citation->repository_version,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        4,
        citation->snapshot_sha,
        -1,
        SQLITE_TRANSIENT
    );

    gboolean matches =
        sqlite3_step (statement) ==
        SQLITE_ROW;
    sqlite3_finalize (
        statement
    );

    if (!matches) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Citation provenance does not match the conversation repository scope."
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_conversation_store_commit_turn (
    AtmConversationStore *store,
    const char *conversation_id,
    const char *user_content,
    const char *assistant_provider_content,
    const char *assistant_display_content,
    gboolean grounded,
    gint64 created_at_us,
    const AtmConversationCitationInput *citations,
    gsize citation_count,
    gint64 *out_turn_no,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (conversation_id) ||
        user_content == NULL ||
        assistant_provider_content == NULL ||
        assistant_display_content == NULL ||
        created_at_us < 0 ||
        (citation_count > 0 &&
         citations == NULL) ||
        (!grounded &&
         citation_count > 0) ||
        out_turn_no == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation turn commit received invalid arguments."
        );
        return FALSE;
    }

    *out_turn_no = -1;

    for (gsize i = 0;
         i < citation_count;
         i++) {
        if (!citation_input_is_valid (
                &citations[i]
            )) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
                "Conversation citation input is invalid."
            );
            return FALSE;
        }
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        return FALSE;
    }

    gboolean ok = FALSE;
    gint64 repository_generation_id = 0;
    gint64 turn_no = 0;
    gint64 sequence_no = 0;
    sqlite3_stmt *statement = NULL;
    char *user_message_id =
        g_uuid_string_random ();
    char *assistant_message_id =
        g_uuid_string_random ();

    if (!load_conversation_write_state (
            store->db,
            conversation_id,
            &repository_generation_id,
            &turn_no,
            &sequence_no,
            error
        )) {
        goto out;
    }

    if (grounded &&
        repository_generation_id <= 0) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "A grounded turn requires a repository-backed conversation."
        );
        goto out;
    }

    for (gsize i = 0;
         i < citation_count;
         i++) {
        if (!citation_matches_conversation_scope (
                store->db,
                conversation_id,
                &citations[i],
                error
            )) {
            goto out;
        }
    }

    if (!prepare_statement (
            store->db,
            "INSERT INTO messages("
            "message_id,conversation_id,sequence_no,turn_no,role,"
            "provider_content,display_content,grounded,created_at_us"
            ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9);",
            &statement,
            error
        )) {
        goto out;
    }

    sqlite3_bind_text (
        statement,
        1,
        user_message_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        2,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int64 (
        statement,
        3,
        sequence_no
    );
    sqlite3_bind_int64 (
        statement,
        4,
        turn_no
    );
    sqlite3_bind_text (
        statement,
        5,
        "user",
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        6,
        user_content,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        7,
        user_content,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int (
        statement,
        8,
        0
    );
    sqlite3_bind_int64 (
        statement,
        9,
        created_at_us
    );

    if (!step_done (
            store->db,
            statement,
            "Could not persist conversation user message",
            error
        )) {
        goto out;
    }

    sqlite3_reset (
        statement
    );
    sqlite3_clear_bindings (
        statement
    );

    sqlite3_bind_text (
        statement,
        1,
        assistant_message_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        2,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int64 (
        statement,
        3,
        sequence_no + 1
    );
    sqlite3_bind_int64 (
        statement,
        4,
        turn_no
    );
    sqlite3_bind_text (
        statement,
        5,
        "assistant",
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        6,
        assistant_provider_content,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text (
        statement,
        7,
        assistant_display_content,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int (
        statement,
        8,
        grounded ? 1 : 0
    );
    sqlite3_bind_int64 (
        statement,
        9,
        created_at_us
    );

    if (!step_done (
            store->db,
            statement,
            "Could not persist conversation assistant message",
            error
        )) {
        goto out;
    }

    sqlite3_finalize (
        statement
    );
    statement = NULL;

    if (citation_count > 0) {
        if (!prepare_statement (
                store->db,
                "INSERT INTO citations("
                "message_id,ordinal,label,repository_id,repository_version,"
                "snapshot_sha,logical_source_id,source_path,locator,title,"
                "excerpt,immutable_permalink"
                ") VALUES("
                "?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12"
                ");",
                &statement,
                error
            )) {
            goto out;
        }

        for (gsize i = 0;
             i < citation_count;
             i++) {
            sqlite3_reset (
                statement
            );
            sqlite3_clear_bindings (
                statement
            );

            sqlite3_bind_text (
                statement,
                1,
                assistant_message_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_int64 (
                statement,
                2,
                (gint64) i + 1
            );
            sqlite3_bind_text (
                statement,
                3,
                citations[i].label,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                4,
                citations[i].repository_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                5,
                citations[i].repository_version,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                6,
                citations[i].snapshot_sha,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                7,
                citations[i].logical_source_id,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                8,
                citations[i].source_path,
                -1,
                SQLITE_TRANSIENT
            );
            sqlite3_bind_text (
                statement,
                9,
                citations[i].locator,
                -1,
                SQLITE_TRANSIENT
            );

            if (citations[i].title != NULL) {
                sqlite3_bind_text (
                    statement,
                    10,
                    citations[i].title,
                    -1,
                    SQLITE_TRANSIENT
                );
            } else {
                sqlite3_bind_null (
                    statement,
                    10
                );
            }

            if (citations[i].excerpt != NULL) {
                sqlite3_bind_text (
                    statement,
                    11,
                    citations[i].excerpt,
                    -1,
                    SQLITE_TRANSIENT
                );
            } else {
                sqlite3_bind_null (
                    statement,
                    11
                );
            }

            if (citations[i].immutable_permalink != NULL) {
                sqlite3_bind_text (
                    statement,
                    12,
                    citations[i].immutable_permalink,
                    -1,
                    SQLITE_TRANSIENT
                );
            } else {
                sqlite3_bind_null (
                    statement,
                    12
                );
            }

            if (!step_done (
                    store->db,
                    statement,
                    "Could not persist conversation citation",
                    error
                )) {
                goto out;
            }
        }

        sqlite3_finalize (
            statement
        );
        statement = NULL;
    }

    if (!prepare_statement (
            store->db,
            "UPDATE conversations "
            "SET updated_at_us=?2 "
            "WHERE conversation_id=?1;",
            &statement,
            error
        )) {
        goto out;
    }

    sqlite3_bind_text (
        statement,
        1,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int64 (
        statement,
        2,
        created_at_us
    );

    if (!step_done (
            store->db,
            statement,
            "Could not update conversation commit metadata",
            error
        )) {
        goto out;
    }

    sqlite3_finalize (
        statement
    );
    statement = NULL;

    if (!exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto out;
    }

    *out_turn_no = turn_no;
    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (
            statement
        );
    }

    if (!ok) {
        rollback_best_effort (
            store->db
        );
    }

    g_free (user_message_id);
    g_free (assistant_message_id);
    return ok;
}


static const char *
nullable_array_value (
    const char *const *values,
    gsize index
)
{
    if (values == NULL ||
        values[index] == NULL ||
        values[index][0] == '\0') {
        return NULL;
    }

    return values[index];
}

gboolean
atm_conversation_store_create_conversation_values (
    const char *path,
    const char *title,
    gint64 created_at_us,
    const char *model_name,
    const char *model_digest,
    gint64 repository_generation_id,
    const char *const *repository_ids,
    const char *const *repository_versions,
    const char *const *repository_shas,
    gsize repository_count,
    char **out_conversation_id,
    GError **error
)
{
    if (!nonempty (path) ||
        (repository_count > 0 &&
         (repository_ids == NULL ||
          repository_versions == NULL ||
          repository_shas == NULL))) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store values create received invalid array arguments."
        );
        return FALSE;
    }

    AtmConversationRepositoryInput *repositories =
        repository_count > 0
            ? g_new0 (
                AtmConversationRepositoryInput,
                repository_count
              )
            : NULL;

    for (gsize i = 0;
         i < repository_count;
         i++) {
        repositories[i].repository_id =
            repository_ids[i];
        repositories[i].repository_version =
            repository_versions[i];
        repositories[i].snapshot_sha =
            repository_shas[i];
    }

    AtmConversationStore *store = NULL;
    gboolean ok = FALSE;

    if (!atm_conversation_store_open (
            path,
            &store,
            error
        )) {
        goto out;
    }

    ok =
        atm_conversation_store_create_conversation (
            store,
            title,
            created_at_us,
            model_name,
            model_digest,
            repository_generation_id,
            repositories,
            repository_count,
            out_conversation_id,
            error
        );

out:
    atm_conversation_store_close (store);
    g_free (repositories);
    return ok;
}

gboolean
atm_conversation_store_commit_turn_values (
    const char *path,
    const char *conversation_id,
    const char *user_content,
    const char *assistant_provider_content,
    const char *assistant_display_content,
    gboolean grounded,
    gint64 created_at_us,
    const char *const *citation_labels,
    const char *const *citation_repository_ids,
    const char *const *citation_repository_versions,
    const char *const *citation_snapshot_shas,
    const char *const *citation_logical_source_ids,
    const char *const *citation_source_paths,
    const char *const *citation_locators,
    const char *const *citation_titles,
    const char *const *citation_excerpts,
    gsize citation_count,
    gint64 *out_turn_no,
    GError **error
)
{
    if (!nonempty (path) ||
        (citation_count > 0 &&
         (citation_labels == NULL ||
          citation_repository_ids == NULL ||
          citation_repository_versions == NULL ||
          citation_snapshot_shas == NULL ||
          citation_logical_source_ids == NULL ||
          citation_source_paths == NULL ||
          citation_locators == NULL ||
          citation_titles == NULL ||
          citation_excerpts == NULL))) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store values commit received invalid citation arrays."
        );
        return FALSE;
    }

    AtmConversationCitationInput *citations =
        citation_count > 0
            ? g_new0 (
                AtmConversationCitationInput,
                citation_count
              )
            : NULL;

    for (gsize i = 0;
         i < citation_count;
         i++) {
        citations[i].label =
            citation_labels[i];
        citations[i].repository_id =
            citation_repository_ids[i];
        citations[i].repository_version =
            citation_repository_versions[i];
        citations[i].snapshot_sha =
            citation_snapshot_shas[i];
        citations[i].logical_source_id =
            citation_logical_source_ids[i];
        citations[i].source_path =
            citation_source_paths[i];
        citations[i].locator =
            citation_locators[i];
        citations[i].title =
            nullable_array_value (
                citation_titles,
                i
            );
        citations[i].excerpt =
            nullable_array_value (
                citation_excerpts,
                i
            );
        citations[i].immutable_permalink = NULL;
    }

    AtmConversationStore *store = NULL;
    gboolean ok = FALSE;

    if (!atm_conversation_store_open (
            path,
            &store,
            error
        )) {
        goto out;
    }

    ok =
        atm_conversation_store_commit_turn (
            store,
            conversation_id,
            user_content,
            assistant_provider_content,
            assistant_display_content,
            grounded,
            created_at_us,
            citations,
            citation_count,
            out_turn_no,
            error
        );

out:
    atm_conversation_store_close (store);
    g_free (citations);
    return ok;
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
