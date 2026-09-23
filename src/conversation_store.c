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

typedef struct {
    char *conversation_id;
    char *title;
    gint64 created_at_us;
    gint64 updated_at_us;
    gboolean archived;
} AtmConversationListEntry;

struct AtmConversationList {
    GPtrArray *entries;
};

typedef struct {
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
} AtmConversationSnapshotRepository;

typedef struct {
    gint64 ordinal;
    char *label;
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *logical_source_id;
    char *source_path;
    char *locator;
    char *title;
    char *excerpt;
    char *immutable_permalink;
} AtmConversationSnapshotCitation;

typedef struct {
    char *message_id;
    gint64 sequence_no;
    gint64 turn_no;
    char *role;
    char *provider_content;
    char *display_content;
    gboolean grounded;
    gint64 created_at_us;
    GPtrArray *citations;
} AtmConversationSnapshotMessage;

struct AtmConversationSnapshot {
    char *conversation_id;
    char *title;
    gint64 created_at_us;
    gint64 updated_at_us;
    char *model_name;
    char *model_digest;
    gint64 repository_generation_id;
    gboolean archived;
    GPtrArray *repositories;
    GPtrArray *messages;
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


gboolean
atm_conversation_store_update_title (
    AtmConversationStore *store,
    const char *conversation_id,
    const char *title,
    gint64 updated_at_us,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (conversation_id) ||
        !nonempty (title) ||
        updated_at_us < 0) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation title update received invalid arguments."
        );
        return FALSE;
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

    if (!prepare_statement (
            store->db,
            "UPDATE conversations "
            "SET title=?2,updated_at_us=?3 "
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
        updated_at_us
    );

    if (!step_done (
            store->db,
            statement,
            "Could not update conversation title",
            error
        )) {
        goto out;
    }

    if (sqlite3_changes (
            store->db
        ) != 1) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation title update did not match exactly one conversation."
        );
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

    return ok;
}

gboolean
atm_conversation_store_create_conversation_values (
    AtmConversationStore *store,
    const char *title,
    gint64 created_at_us,
    const char *model_name,
    const char *model_digest,
    gint64 repository_generation_id,
    const char * const *repository_ids,
    const char * const *repository_versions,
    const char * const *snapshot_shas,
    gsize repository_count,
    char **out_conversation_id,
    GError **error
)
{
    if (repository_count > 0 &&
        (repository_ids == NULL ||
         repository_versions == NULL ||
         snapshot_shas == NULL)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation repository arrays are incomplete."
        );
        return FALSE;
    }

    AtmConversationRepositoryInput *repositories = NULL;

    if (repository_count > 0) {
        repositories = g_new0 (
            AtmConversationRepositoryInput,
            repository_count
        );

        for (gsize i = 0;
             i < repository_count;
             i++) {
            repositories[i].repository_id =
                repository_ids[i];
            repositories[i].repository_version =
                repository_versions[i];
            repositories[i].snapshot_sha =
                snapshot_shas[i];
        }
    }

    gboolean ok =
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

    g_free (repositories);
    return ok;
}

gboolean
atm_conversation_store_commit_turn_values (
    AtmConversationStore *store,
    const char *conversation_id,
    const char *user_content,
    const char *assistant_provider_content,
    const char *assistant_display_content,
    gboolean grounded,
    gint64 created_at_us,
    const char * const *labels,
    const char * const *repository_ids,
    const char * const *repository_versions,
    const char * const *snapshot_shas,
    const char * const *logical_source_ids,
    const char * const *source_paths,
    const char * const *locators,
    const char * const *titles,
    const char * const *excerpts,
    gsize citation_count,
    gint64 *out_turn_no,
    GError **error
)
{
    if (citation_count > 0 &&
        (labels == NULL ||
         repository_ids == NULL ||
         repository_versions == NULL ||
         snapshot_shas == NULL ||
         logical_source_ids == NULL ||
         source_paths == NULL ||
         locators == NULL ||
         titles == NULL ||
         excerpts == NULL)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation citation arrays are incomplete."
        );
        return FALSE;
    }

    AtmConversationCitationInput *citations = NULL;

    if (citation_count > 0) {
        citations = g_new0 (
            AtmConversationCitationInput,
            citation_count
        );

        for (gsize i = 0;
             i < citation_count;
             i++) {
            citations[i].label = labels[i];
            citations[i].repository_id =
                repository_ids[i];
            citations[i].repository_version =
                repository_versions[i];
            citations[i].snapshot_sha =
                snapshot_shas[i];
            citations[i].logical_source_id =
                logical_source_ids[i];
            citations[i].source_path =
                source_paths[i];
            citations[i].locator =
                locators[i];
            citations[i].title =
                nonempty (titles[i])
                    ? titles[i]
                    : NULL;
            citations[i].excerpt =
                nonempty (excerpts[i])
                    ? excerpts[i]
                    : NULL;
            citations[i].immutable_permalink =
                NULL;
        }
    }

    gboolean ok =
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

    g_free (citations);
    return ok;
}


static char *
column_text_dup (
    sqlite3_stmt *statement,
    int column
)
{
    const unsigned char *value =
        sqlite3_column_text (
            statement,
            column
        );

    return value != NULL
        ? g_strdup ((const char *) value)
        : g_strdup ("");
}

static char *
column_text_dup_nullable (
    sqlite3_stmt *statement,
    int column
)
{
    if (sqlite3_column_type (
            statement,
            column
        ) == SQLITE_NULL) {
        return NULL;
    }

    return column_text_dup (
        statement,
        column
    );
}

static void
conversation_list_entry_free (
    gpointer data
)
{
    AtmConversationListEntry *entry =
        data;

    if (entry == NULL) {
        return;
    }

    g_free (entry->conversation_id);
    g_free (entry->title);
    g_free (entry);
}

void
atm_conversation_list_free (
    AtmConversationList *list
)
{
    if (list == NULL) {
        return;
    }

    g_clear_pointer (
        &list->entries,
        g_ptr_array_unref
    );
    g_free (list);
}

static void
snapshot_repository_free (
    gpointer data
)
{
    AtmConversationSnapshotRepository *repository =
        data;

    if (repository == NULL) {
        return;
    }

    g_free (repository->repository_id);
    g_free (repository->repository_version);
    g_free (repository->snapshot_sha);
    g_free (repository);
}

static void
snapshot_citation_free (
    gpointer data
)
{
    AtmConversationSnapshotCitation *citation =
        data;

    if (citation == NULL) {
        return;
    }

    g_free (citation->label);
    g_free (citation->repository_id);
    g_free (citation->repository_version);
    g_free (citation->snapshot_sha);
    g_free (citation->logical_source_id);
    g_free (citation->source_path);
    g_free (citation->locator);
    g_free (citation->title);
    g_free (citation->excerpt);
    g_free (citation->immutable_permalink);
    g_free (citation);
}

static void
snapshot_message_free (
    gpointer data
)
{
    AtmConversationSnapshotMessage *message =
        data;

    if (message == NULL) {
        return;
    }

    g_free (message->message_id);
    g_free (message->role);
    g_free (message->provider_content);
    g_free (message->display_content);
    g_clear_pointer (
        &message->citations,
        g_ptr_array_unref
    );
    g_free (message);
}

void
atm_conversation_snapshot_free (
    AtmConversationSnapshot *snapshot
)
{
    if (snapshot == NULL) {
        return;
    }

    g_free (snapshot->conversation_id);
    g_free (snapshot->title);
    g_free (snapshot->model_name);
    g_free (snapshot->model_digest);
    g_clear_pointer (
        &snapshot->repositories,
        g_ptr_array_unref
    );
    g_clear_pointer (
        &snapshot->messages,
        g_ptr_array_unref
    );
    g_free (snapshot);
}

static AtmConversationListEntry *
conversation_list_entry_at (
    const AtmConversationList *list,
    guint index
)
{
    if (list == NULL ||
        list->entries == NULL ||
        index >= list->entries->len) {
        return NULL;
    }

    return g_ptr_array_index (
        list->entries,
        index
    );
}

static AtmConversationSnapshotRepository *
snapshot_repository_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    if (snapshot == NULL ||
        snapshot->repositories == NULL ||
        index >= snapshot->repositories->len) {
        return NULL;
    }

    return g_ptr_array_index (
        snapshot->repositories,
        index
    );
}

static AtmConversationSnapshotMessage *
snapshot_message_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    if (snapshot == NULL ||
        snapshot->messages == NULL ||
        index >= snapshot->messages->len) {
        return NULL;
    }

    return g_ptr_array_index (
        snapshot->messages,
        index
    );
}

static AtmConversationSnapshotCitation *
snapshot_citation_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint citation_index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            message_index
        );

    if (message == NULL ||
        message->citations == NULL ||
        citation_index >= message->citations->len) {
        return NULL;
    }

    return g_ptr_array_index (
        message->citations,
        citation_index
    );
}

gboolean
atm_conversation_store_list_conversations (
    AtmConversationStore *store,
    AtmConversationList **out_list,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        out_list == NULL ||
        *out_list != NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation list read received invalid arguments."
        );
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN;",
            error
        )) {
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *statement = NULL;
    AtmConversationList *list =
        g_new0 (
            AtmConversationList,
            1
        );
    list->entries =
        g_ptr_array_new_with_free_func (
            conversation_list_entry_free
        );

    if (!prepare_statement (
            store->db,
            "SELECT "
            "conversation_id,title,created_at_us,updated_at_us,archived "
            "FROM conversations "
            "ORDER BY updated_at_us DESC,conversation_id ASC;",
            &statement,
            error
        )) {
        goto out;
    }

    int rc;

    while ((rc = sqlite3_step (
                statement
            )) == SQLITE_ROW) {
        AtmConversationListEntry *entry =
            g_new0 (
                AtmConversationListEntry,
                1
            );

        entry->conversation_id =
            column_text_dup (
                statement,
                0
            );
        entry->title =
            column_text_dup (
                statement,
                1
            );
        entry->created_at_us =
            sqlite3_column_int64 (
                statement,
                2
            );
        entry->updated_at_us =
            sqlite3_column_int64 (
                statement,
                3
            );
        entry->archived =
            sqlite3_column_int (
                statement,
                4
            ) != 0;

        g_ptr_array_add (
            list->entries,
            entry
        );
    }

    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation list"
        );
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

    *out_list =
        g_steal_pointer (
            &list
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

    atm_conversation_list_free (
        list
    );
    return ok;
}

static AtmConversationSnapshotMessage *
snapshot_message_for_sequence (
    AtmConversationSnapshot *snapshot,
    gint64 sequence_no
)
{
    if (snapshot == NULL ||
        snapshot->messages == NULL) {
        return NULL;
    }

    for (guint i = 0;
         i < snapshot->messages->len;
         i++) {
        AtmConversationSnapshotMessage *message =
            g_ptr_array_index (
                snapshot->messages,
                i
            );

        if (message->sequence_no ==
            sequence_no) {
            return message;
        }
    }

    return NULL;
}

gboolean
atm_conversation_store_load_snapshot (
    AtmConversationStore *store,
    const char *conversation_id,
    AtmConversationSnapshot **out_snapshot,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (conversation_id) ||
        out_snapshot == NULL ||
        *out_snapshot != NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation snapshot read received invalid arguments."
        );
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN;",
            error
        )) {
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *statement = NULL;
    AtmConversationSnapshot *snapshot =
        g_new0 (
            AtmConversationSnapshot,
            1
        );
    snapshot->repositories =
        g_ptr_array_new_with_free_func (
            snapshot_repository_free
        );
    snapshot->messages =
        g_ptr_array_new_with_free_func (
            snapshot_message_free
        );

    if (!prepare_statement (
            store->db,
            "SELECT "
            "conversation_id,title,created_at_us,updated_at_us,"
            "model_name,model_digest,repository_generation_id,archived "
            "FROM conversations WHERE conversation_id=?1;",
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

    int rc = sqlite3_step (
        statement
    );

    if (rc == SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_NOT_FOUND,
            "Conversation does not exist in the durable store."
        );
        goto out;
    }

    if (rc != SQLITE_ROW) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation identity"
        );
        goto out;
    }

    snapshot->conversation_id =
        column_text_dup (
            statement,
            0
        );
    snapshot->title =
        column_text_dup (
            statement,
            1
        );
    snapshot->created_at_us =
        sqlite3_column_int64 (
            statement,
            2
        );
    snapshot->updated_at_us =
        sqlite3_column_int64 (
            statement,
            3
        );
    snapshot->model_name =
        column_text_dup (
            statement,
            4
        );
    snapshot->model_digest =
        column_text_dup_nullable (
            statement,
            5
        );
    snapshot->repository_generation_id =
        sqlite3_column_int64 (
            statement,
            6
        );
    snapshot->archived =
        sqlite3_column_int (
            statement,
            7
        ) != 0;

    if (sqlite3_step (
            statement
        ) != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation snapshot identity query returned multiple rows."
        );
        goto out;
    }

    sqlite3_finalize (
        statement
    );
    statement = NULL;

    if (!prepare_statement (
            store->db,
            "SELECT repository_id,repository_version,snapshot_sha "
            "FROM conversation_repositories "
            "WHERE conversation_id=?1 "
            "ORDER BY CASE repository_id "
            "WHEN 'ewd' THEN 0 WHEN 'cbd' THEN 1 WHEN 'rmd' THEN 2 ELSE 99 END;",
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

    while ((rc = sqlite3_step (
                statement
            )) == SQLITE_ROW) {
        AtmConversationSnapshotRepository *repository =
            g_new0 (
                AtmConversationSnapshotRepository,
                1
            );

        repository->repository_id =
            column_text_dup (
                statement,
                0
            );
        repository->repository_version =
            column_text_dup (
                statement,
                1
            );
        repository->snapshot_sha =
            column_text_dup (
                statement,
                2
            );

        g_ptr_array_add (
            snapshot->repositories,
            repository
        );
    }

    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation repository pins"
        );
        goto out;
    }

    sqlite3_finalize (
        statement
    );
    statement = NULL;

    if (!prepare_statement (
            store->db,
            "SELECT "
            "message_id,sequence_no,turn_no,role,provider_content,"
            "display_content,grounded,created_at_us "
            "FROM messages "
            "WHERE conversation_id=?1 "
            "ORDER BY sequence_no ASC;",
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

    while ((rc = sqlite3_step (
                statement
            )) == SQLITE_ROW) {
        AtmConversationSnapshotMessage *message =
            g_new0 (
                AtmConversationSnapshotMessage,
                1
            );

        message->message_id =
            column_text_dup (
                statement,
                0
            );
        message->sequence_no =
            sqlite3_column_int64 (
                statement,
                1
            );
        message->turn_no =
            sqlite3_column_int64 (
                statement,
                2
            );
        message->role =
            column_text_dup (
                statement,
                3
            );
        message->provider_content =
            column_text_dup (
                statement,
                4
            );
        message->display_content =
            column_text_dup (
                statement,
                5
            );
        message->grounded =
            sqlite3_column_int (
                statement,
                6
            ) != 0;
        message->created_at_us =
            sqlite3_column_int64 (
                statement,
                7
            );
        message->citations =
            g_ptr_array_new_with_free_func (
                snapshot_citation_free
            );

        g_ptr_array_add (
            snapshot->messages,
            message
        );
    }

    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation messages"
        );
        goto out;
    }

    sqlite3_finalize (
        statement
    );
    statement = NULL;

    if (!prepare_statement (
            store->db,
            "SELECT "
            "m.sequence_no,c.ordinal,c.label,c.repository_id,"
            "c.repository_version,c.snapshot_sha,c.logical_source_id,"
            "c.source_path,c.locator,c.title,c.excerpt,c.immutable_permalink "
            "FROM citations c "
            "JOIN messages m ON m.message_id=c.message_id "
            "WHERE m.conversation_id=?1 "
            "ORDER BY m.sequence_no ASC,c.ordinal ASC;",
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

    while ((rc = sqlite3_step (
                statement
            )) == SQLITE_ROW) {
        gint64 sequence_no =
            sqlite3_column_int64 (
                statement,
                0
            );
        AtmConversationSnapshotMessage *message =
            snapshot_message_for_sequence (
                snapshot,
                sequence_no
            );

        if (message == NULL) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
                "Conversation citation refers to an unavailable snapshot message."
            );
            goto out;
        }

        AtmConversationSnapshotCitation *citation =
            g_new0 (
                AtmConversationSnapshotCitation,
                1
            );

        citation->ordinal =
            sqlite3_column_int64 (
                statement,
                1
            );
        citation->label =
            column_text_dup (
                statement,
                2
            );
        citation->repository_id =
            column_text_dup (
                statement,
                3
            );
        citation->repository_version =
            column_text_dup (
                statement,
                4
            );
        citation->snapshot_sha =
            column_text_dup (
                statement,
                5
            );
        citation->logical_source_id =
            column_text_dup (
                statement,
                6
            );
        citation->source_path =
            column_text_dup (
                statement,
                7
            );
        citation->locator =
            column_text_dup (
                statement,
                8
            );
        citation->title =
            column_text_dup_nullable (
                statement,
                9
            );
        citation->excerpt =
            column_text_dup_nullable (
                statement,
                10
            );
        citation->immutable_permalink =
            column_text_dup_nullable (
                statement,
                11
            );

        g_ptr_array_add (
            message->citations,
            citation
        );
    }

    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation citations"
        );
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

    *out_snapshot =
        g_steal_pointer (
            &snapshot
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

    atm_conversation_snapshot_free (
        snapshot
    );
    return ok;
}

guint
atm_conversation_list_count (
    const AtmConversationList *list
)
{
    return list != NULL &&
        list->entries != NULL
            ? list->entries->len
            : 0;
}

const char *
atm_conversation_list_id_at (
    const AtmConversationList *list,
    guint index
)
{
    AtmConversationListEntry *entry =
        conversation_list_entry_at (
            list,
            index
        );

    return entry != NULL
        ? entry->conversation_id
        : NULL;
}

const char *
atm_conversation_list_title_at (
    const AtmConversationList *list,
    guint index
)
{
    AtmConversationListEntry *entry =
        conversation_list_entry_at (
            list,
            index
        );

    return entry != NULL
        ? entry->title
        : NULL;
}

gint64
atm_conversation_list_created_at_us_at (
    const AtmConversationList *list,
    guint index
)
{
    AtmConversationListEntry *entry =
        conversation_list_entry_at (
            list,
            index
        );

    return entry != NULL
        ? entry->created_at_us
        : -1;
}

gint64
atm_conversation_list_updated_at_us_at (
    const AtmConversationList *list,
    guint index
)
{
    AtmConversationListEntry *entry =
        conversation_list_entry_at (
            list,
            index
        );

    return entry != NULL
        ? entry->updated_at_us
        : -1;
}

gboolean
atm_conversation_list_archived_at (
    const AtmConversationList *list,
    guint index
)
{
    AtmConversationListEntry *entry =
        conversation_list_entry_at (
            list,
            index
        );

    return entry != NULL &&
        entry->archived;
}

const char *
atm_conversation_snapshot_id (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->conversation_id
        : NULL;
}

const char *
atm_conversation_snapshot_title (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->title
        : NULL;
}

gint64
atm_conversation_snapshot_created_at_us (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->created_at_us
        : -1;
}

gint64
atm_conversation_snapshot_updated_at_us (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->updated_at_us
        : -1;
}

const char *
atm_conversation_snapshot_model_name (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->model_name
        : NULL;
}

const char *
atm_conversation_snapshot_model_digest (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->model_digest
        : NULL;
}

gint64
atm_conversation_snapshot_repository_generation_id (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL
        ? snapshot->repository_generation_id
        : -1;
}

gboolean
atm_conversation_snapshot_archived (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL &&
        snapshot->archived;
}

guint
atm_conversation_snapshot_repository_count (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL &&
        snapshot->repositories != NULL
            ? snapshot->repositories->len
            : 0;
}

const char *
atm_conversation_snapshot_repository_id_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotRepository *repository =
        snapshot_repository_at (
            snapshot,
            index
        );

    return repository != NULL
        ? repository->repository_id
        : NULL;
}

const char *
atm_conversation_snapshot_repository_version_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotRepository *repository =
        snapshot_repository_at (
            snapshot,
            index
        );

    return repository != NULL
        ? repository->repository_version
        : NULL;
}

const char *
atm_conversation_snapshot_repository_sha_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotRepository *repository =
        snapshot_repository_at (
            snapshot,
            index
        );

    return repository != NULL
        ? repository->snapshot_sha
        : NULL;
}

guint
atm_conversation_snapshot_message_count (
    const AtmConversationSnapshot *snapshot
)
{
    return snapshot != NULL &&
        snapshot->messages != NULL
            ? snapshot->messages->len
            : 0;
}

gint64
atm_conversation_snapshot_message_sequence_no_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL
        ? message->sequence_no
        : -1;
}

gint64
atm_conversation_snapshot_message_turn_no_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL
        ? message->turn_no
        : -1;
}

const char *
atm_conversation_snapshot_message_role_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL
        ? message->role
        : NULL;
}

const char *
atm_conversation_snapshot_message_provider_content_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL
        ? message->provider_content
        : NULL;
}

const char *
atm_conversation_snapshot_message_display_content_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL
        ? message->display_content
        : NULL;
}

gboolean
atm_conversation_snapshot_message_grounded_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL &&
        message->grounded;
}

gint64
atm_conversation_snapshot_message_created_at_us_at (
    const AtmConversationSnapshot *snapshot,
    guint index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            index
        );

    return message != NULL
        ? message->created_at_us
        : -1;
}

guint
atm_conversation_snapshot_message_citation_count_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (
            snapshot,
            message_index
        );

    return message != NULL &&
        message->citations != NULL
            ? message->citations->len
            : 0;
}

#define SNAPSHOT_CITATION_TEXT_ACCESSOR(name, field) \
const char * \
name ( \
    const AtmConversationSnapshot *snapshot, \
    guint message_index, \
    guint citation_index \
) \
{ \
    AtmConversationSnapshotCitation *citation = \
        snapshot_citation_at ( \
            snapshot, \
            message_index, \
            citation_index \
        ); \
    return citation != NULL \
        ? citation->field \
        : NULL; \
}

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_label_at,
    label
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_repository_id_at,
    repository_id
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_repository_version_at,
    repository_version
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_snapshot_sha_at,
    snapshot_sha
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_logical_source_id_at,
    logical_source_id
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_source_path_at,
    source_path
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_locator_at,
    locator
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_title_at,
    title
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_excerpt_at,
    excerpt
)

SNAPSHOT_CITATION_TEXT_ACCESSOR (
    atm_conversation_snapshot_citation_immutable_permalink_at,
    immutable_permalink
)

#undef SNAPSHOT_CITATION_TEXT_ACCESSOR

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
