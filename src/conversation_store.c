#include "conversation_store.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#define ATM_CONVERSATION_STORE_SCHEMA_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v5.sql"
#define ATM_CONVERSATION_STORE_MIGRATION_V3_V4_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v3-to-v4.sql"
#define ATM_CONVERSATION_STORE_MIGRATION_V4_V5_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v4-to-v5.sql"
#define ATM_CONVERSATION_STORE_MIGRATION_V5_V6_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v5-to-v6.sql"
#define ATM_CONVERSATION_STORE_SCHEMA_V6_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v6.sql"
#define ATM_CONVERSATION_STORE_SCHEMA_V4_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v4.sql"
#define ATM_CONVERSATION_STORE_MIGRATION_V1_V2_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v1-to-v2.sql"
#define ATM_CONVERSATION_STORE_MIGRATION_V2_V3_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v2-to-v3.sql"
#define ATM_CONVERSATION_STORE_SCHEMA_V1_ID "atm-conversation-store/1"
#define ATM_CONVERSATION_STORE_SCHEMA_V1_VERSION 1
#define ATM_CONVERSATION_STORE_SCHEMA_V2_ID "atm-conversation-store/2"
#define ATM_CONVERSATION_STORE_SCHEMA_V2_VERSION 2
#define ATM_CONVERSATION_STORE_SCHEMA_V3_ID "atm-conversation-store/3"
#define ATM_CONVERSATION_STORE_SCHEMA_V3_VERSION 3
#define ATM_CONVERSATION_STORE_SCHEMA_V4_ID "atm-conversation-store/4"
#define ATM_CONVERSATION_STORE_SCHEMA_V4_VERSION 4
#define ATM_CONVERSATION_STORE_SCHEMA_V5_ID "atm-conversation-store/5"
#define ATM_CONVERSATION_STORE_SCHEMA_V5_VERSION 5
#define ATM_CONVERSATION_STORE_SCHEMA_V6_ID "atm-conversation-store/6"
#define ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION 6

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
    gboolean open_on_startup;
} AtmConversationListEntry;

struct AtmConversationList {
    GPtrArray *entries;
};

static gboolean validate_v6_for_migration (sqlite3 *db, GError **error);
static gboolean validate_v6_presentation_semantics (
    sqlite3 *db,
    GError **error
);
#ifdef ATM_CONVERSATION_STORE_TESTING
static guint fail_v5_v6_after_statement;
static gboolean fail_v5_v6_after_sql_resource;
static void (*v5_v6_lock_hook) (gpointer user_data);
static gpointer v5_v6_lock_hook_data;

void
atm_conversation_store_test_fail_v5_v6_after_statement (
    guint statement_number
)
{
    fail_v5_v6_after_statement = statement_number;
}

void
atm_conversation_store_test_fail_v5_v6_after_sql_resource (void)
{
    fail_v5_v6_after_sql_resource = TRUE;
}

void
atm_conversation_store_test_set_v5_v6_lock_hook (
    void (*hook) (gpointer user_data),
    gpointer user_data
)
{
    v5_v6_lock_hook = hook;
    v5_v6_lock_hook_data = user_data;
}
#endif

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
    guint series_ordinal;
    char *series_profile;
    char *admission_profile;
    char *scientific_id;
    char *qualified_id;
    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *source_path;
} AtmConversationSnapshotChartSeries;

typedef struct {
    gint64 ordinal;
    char *chart_schema;
    char *chart_spec_id;
    char *chart_kind;
    char *reconstruction_profile;
    GPtrArray *series;
} AtmConversationSnapshotChart;

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
    GPtrArray *charts;
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
load_sql_resource (
    const char *resource_path,
    const char *label,
    char **out_sql,
    GError **error
)
{
    if (!nonempty (resource_path) ||
        !nonempty (label) ||
        out_sql == NULL ||
        *out_sql != NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store SQL resource loader received invalid arguments."
        );
        return FALSE;
    }

    GError *resource_error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        resource_path,
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &resource_error
    );

    if (bytes == NULL) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Could not load conversation-store %s resource: %s",
            label,
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
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store %s resource is empty.",
            label
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
load_schema_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_SCHEMA_RESOURCE,
        "schema",
        out_sql,
        error
    );
}

static gboolean
load_migration_v1_v2_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_MIGRATION_V1_V2_RESOURCE,
        "v1-to-v2 migration",
        out_sql,
        error
    );
}

static gboolean
load_migration_v2_v3_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_MIGRATION_V2_V3_RESOURCE,
        "v2-to-v3 migration",
        out_sql,
        error
    );
}

static gboolean
load_migration_v3_v4_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_MIGRATION_V3_V4_RESOURCE,
        "v3-to-v4 migration",
        out_sql,
        error
    );
}

static gboolean
load_migration_v4_v5_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_MIGRATION_V4_V5_RESOURCE,
        "v4-to-v5 migration",
        out_sql,
        error
    );
}

static gboolean
load_migration_v5_v6_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_MIGRATION_V5_V6_RESOURCE,
        "v5-to-v6 migration",
        out_sql,
        error
    );
}

static gboolean
load_schema_v4_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_SCHEMA_V4_RESOURCE,
        "v4 schema",
        out_sql,
        error
    );
}

static gboolean
load_schema_v6_sql (
    char **out_sql,
    GError **error
)
{
    return load_sql_resource (
        ATM_CONVERSATION_STORE_SCHEMA_V6_RESOURCE,
        "v6 schema",
        out_sql,
        error
    );
}

static void
append_fingerprint_value (
    GString *fingerprint,
    sqlite3_stmt *statement,
    int column
)
{
    int type = sqlite3_column_type (statement, column);
    g_string_append_printf (fingerprint, "%d:", type);
    if (type == SQLITE_INTEGER) {
        gint64 value = (gint64) sqlite3_column_int64 (statement, column);
        g_string_append_printf (
            fingerprint,
            "%" G_GINT64_FORMAT,
            value
        );
    } else if (type == SQLITE_FLOAT) {
        g_string_append_printf (
            fingerprint,
            "%.17g",
            sqlite3_column_double (statement, column)
        );
    } else if (type == SQLITE_TEXT || type == SQLITE_BLOB) {
        const void *value = type == SQLITE_TEXT
            ? (const void *) sqlite3_column_text (statement, column)
            : sqlite3_column_blob (statement, column);
        int length = sqlite3_column_bytes (statement, column);
        g_string_append_printf (fingerprint, "%d:", length);
        if (length > 0)
            g_string_append_len (fingerprint, value, length);
    }
    g_string_append_c (fingerprint, ';');
}

static gboolean
append_query_fingerprint (
    sqlite3 *db,
    const char *sql,
    GString *fingerprint,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2 (db, sql, -1, &statement, NULL);
    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare schema fingerprint query"
        );
        return FALSE;
    }

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        g_string_append_c (fingerprint, '[');
        for (int column = 0;
             column < sqlite3_column_count (statement);
             column++)
            append_fingerprint_value (fingerprint, statement, column);
        g_string_append_c (fingerprint, ']');
    }
    sqlite3_finalize (statement);
    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read schema fingerprint query"
        );
        return FALSE;
    }
    return TRUE;
}

static char *
normalize_schema_sql (
    const char *sql
)
{
    GString *normalized = g_string_new (NULL);
    gsize length = strlen (sql);
    gboolean pending_space = FALSE;
    for (gsize i = 0; i < length;) {
        char ch = sql[i];
        if (g_ascii_isspace (ch)) {
            pending_space = TRUE;
            i++;
            continue;
        }
        if (ch == '-' && i + 1 < length && sql[i + 1] == '-') {
            pending_space = TRUE;
            i += 2;
            while (i < length && sql[i] != '\n')
                i++;
            continue;
        }
        if (ch == '/' && i + 1 < length && sql[i + 1] == '*') {
            pending_space = TRUE;
            i += 2;
            while (i + 1 < length &&
                   !(sql[i] == '*' && sql[i + 1] == '/'))
                i++;
            i = MIN (i + 2, length);
            continue;
        }
        char previous = normalized->len > 0
            ? normalized->str[normalized->len - 1]
            : '\0';
        if (pending_space && normalized->len > 0 &&
            previous != '(' && previous != ',' &&
            ch != ',' && ch != ')')
            g_string_append_c (normalized, ' ');
        pending_space = FALSE;
        if (ch == '\'' || ch == '"' || ch == '`' || ch == '[') {
            char close = ch == '[' ? ']' : ch;
            g_string_append_c (normalized, ch);
            i++;
            while (i < length) {
                char quoted = sql[i++];
                g_string_append_c (normalized, quoted);
                if (quoted == close) {
                    if (i < length && sql[i] == close &&
                        close != ']') {
                        g_string_append_c (normalized, sql[i++]);
                    } else {
                        break;
                    }
                }
            }
            continue;
        }
        g_string_append_c (normalized, ch);
        i++;
    }
    char *result = g_string_free (normalized, FALSE);
    return result;
}

static gboolean
append_normalized_schema_sql_fingerprint (
    sqlite3 *db,
    GString *fingerprint,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2 (
        db,
        "SELECT type,name,tbl_name,sql FROM main.sqlite_schema "
        "WHERE sql IS NOT NULL ORDER BY type,name,tbl_name;",
        -1,
        &statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not prepare normalized schema SQL fingerprint query"
        );
        return FALSE;
    }
    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        for (int column = 0; column < 3; column++)
            append_fingerprint_value (fingerprint, statement, column);
        const char *sql = (const char *) sqlite3_column_text (statement, 3);
        char *normalized = normalize_schema_sql (sql != NULL ? sql : "");
        g_string_append_printf (
            fingerprint,
            "sql:%" G_GSIZE_FORMAT ":",
            strlen (normalized)
        );
        g_string_append (fingerprint, normalized);
        g_string_append_c (fingerprint, ';');
        g_free (normalized);
    }
    sqlite3_finalize (statement);
    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read normalized schema SQL fingerprint"
        );
        return FALSE;
    }
    return TRUE;
}

static gboolean
append_schema_fingerprint (
    sqlite3 *db,
    gint version,
    GString *fingerprint,
    GError **error
)
{
    static const char *v5_tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series"
    };
    static const char *v6_tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series",
        "chart_presentations_v4"
    };
    const char **tables = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
        ? v6_tables
        : v5_tables;
    gsize table_count = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
        ? G_N_ELEMENTS (v6_tables)
        : G_N_ELEMENTS (v5_tables);

    if (!append_normalized_schema_sql_fingerprint (db, fingerprint, error) ||
        !append_query_fingerprint (
            db,
            "SELECT schema,name,type,ncol,wr,strict "
            "FROM pragma_table_list ORDER BY schema,name;",
            fingerprint,
            error
        ) ||
        !append_query_fingerprint (
            db,
            "PRAGMA encoding;",
            fingerprint,
            error
        ))
        return FALSE;

    for (gsize i = 0; i < table_count; i++) {
        char *quoted_table = sqlite3_mprintf ("%Q", tables[i]);
        char *sql = sqlite3_mprintf (
            "PRAGMA main.table_xinfo(%s);",
            quoted_table
        );
        sqlite3_free (quoted_table);
        gboolean ok = append_query_fingerprint (db, sql, fingerprint, error);
        sqlite3_free (sql);
        if (!ok)
            return FALSE;

        quoted_table = sqlite3_mprintf ("%Q", tables[i]);
        sql = sqlite3_mprintf (
            "PRAGMA main.foreign_key_list(%s);",
            quoted_table
        );
        sqlite3_free (quoted_table);
        ok = append_query_fingerprint (db, sql, fingerprint, error);
        sqlite3_free (sql);
        if (!ok)
            return FALSE;

        quoted_table = sqlite3_mprintf ("%Q", tables[i]);
        sql = sqlite3_mprintf (
            "SELECT seq,name,"
            "\"unique\",origin,partial FROM pragma_index_list(%s) ORDER BY seq;",
            quoted_table
        );
        sqlite3_free (quoted_table);
        sqlite3_stmt *indexes = NULL;
        int rc = sqlite3_prepare_v2 (db, sql, -1, &indexes, NULL);
        sqlite3_free (sql);
        if (rc != SQLITE_OK) {
            set_sqlite_error (
                db,
                error,
                ATM_CONVERSATION_STORE_ERROR_SQLITE,
                "Could not prepare schema index fingerprint query"
            );
            return FALSE;
        }
        while ((rc = sqlite3_step (indexes)) == SQLITE_ROW) {
            const char *index_name = (const char *) sqlite3_column_text (
                indexes,
                1
            );
            gboolean generated = sqlite3_column_text (indexes, 3) != NULL &&
                (!g_str_equal (
                    (const char *) sqlite3_column_text (indexes, 3),
                    "c"
                ));
            append_fingerprint_value (fingerprint, indexes, 0);
            if (generated) {
                g_string_append (fingerprint, "constraint-autoindex;");
            } else {
                append_fingerprint_value (fingerprint, indexes, 1);
            }
            for (int column = 2;
                 column < sqlite3_column_count (indexes);
                 column++)
                append_fingerprint_value (fingerprint, indexes, column);

            char *quoted_index = sqlite3_mprintf ("%Q", index_name);
            char *index_sql = sqlite3_mprintf (
                "SELECT seqno,cid,name,\"desc\",coll,\"key\" "
                "FROM pragma_index_xinfo(%s) ORDER BY seqno;",
                quoted_index
            );
            sqlite3_free (quoted_index);
            gboolean index_ok = append_query_fingerprint (
                db,
                index_sql,
                fingerprint,
                error
            );
            sqlite3_free (index_sql);
            if (!index_ok) {
                sqlite3_finalize (indexes);
                return FALSE;
            }
        }
        sqlite3_finalize (indexes);
        if (rc != SQLITE_DONE) {
            set_sqlite_error (
                db,
                error,
                ATM_CONVERSATION_STORE_ERROR_SQLITE,
                "Could not read schema index fingerprint query"
            );
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
create_canonical_schema_db (
    gint version,
    gboolean migrate_from_v4,
    gboolean bootstrap_direct_v6,
    sqlite3 **out_db,
    GError **error
)
{
    *out_db = NULL;
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2 (":memory:", &db, SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY, NULL);
    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not create in-memory canonical schema"
        );
        if (db != NULL)
            sqlite3_close (db);
        return FALSE;
    }

    gboolean ok = FALSE;
    char *schema_sql = NULL;
    char *migration_sql = NULL;
    if (!exec_sql (db, "PRAGMA foreign_keys=ON;", error))
        goto out;

    if (bootstrap_direct_v6) {
        if (!load_schema_v6_sql (&schema_sql, error) ||
            !exec_sql (db, schema_sql, error))
            goto out;
    } else if (migrate_from_v4) {
        if (!load_schema_v4_sql (&schema_sql, error) ||
            !load_migration_v4_v5_sql (&migration_sql, error) ||
            !exec_sql (db, schema_sql, error) ||
            !exec_sql (db, migration_sql, error))
            goto out;
        g_free (migration_sql);
        migration_sql = NULL;
    } else {
        if (!load_schema_sql (&schema_sql, error) ||
            !exec_sql (db, schema_sql, error))
            goto out;
    }
    g_free (schema_sql);
    schema_sql = NULL;

    if (version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION &&
        !bootstrap_direct_v6) {
        if (!load_migration_v5_v6_sql (&migration_sql, error) ||
            !exec_sql (db, migration_sql, error))
            goto out;
    }
    char *identity_sql = g_strdup_printf (
        "PRAGMA application_id=%u;",
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    gboolean identity_ok = exec_sql (
        db,
        version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
            ? "PRAGMA user_version=6;"
            : "PRAGMA user_version=5;",
        error
    ) && exec_sql (db, identity_sql, error);
    g_free (identity_sql);
    if (!identity_ok)
        goto out;

    *out_db = db;
    db = NULL;
    ok = TRUE;

out:
    g_free (schema_sql);
    g_free (migration_sql);
    if (db != NULL)
        sqlite3_close (db);
    return ok;
}

static gboolean
create_canonical_schema_fingerprint (
    gint version,
    gboolean migrate_from_v4,
    gboolean bootstrap_direct_v6,
    char **out_fingerprint,
    GError **error
)
{
    sqlite3 *db = NULL;
    if (!create_canonical_schema_db (
            version,
            migrate_from_v4,
            bootstrap_direct_v6,
            &db,
            error
        ))
        return FALSE;
    GString *fingerprint = g_string_new (NULL);
    gboolean ok = append_schema_fingerprint (
        db,
        version,
        fingerprint,
        error
    );
    if (ok)
        *out_fingerprint = g_string_free (fingerprint, FALSE);
    else
        g_string_free (fingerprint, TRUE);
    sqlite3_close (db);
    return ok;
}

static GMutex canonical_fingerprint_mutex;
static GHashTable *canonical_fingerprint_cache;

static gboolean
get_canonical_fingerprint (
    gint version,
    gboolean migrate_from_v4,
    gboolean bootstrap_direct_v6,
    const char **out_fingerprint,
    GError **error
)
{
    char *key = g_strdup_printf (
        "%d:%d:%d",
        version,
        migrate_from_v4 ? 1 : 0,
        bootstrap_direct_v6 ? 1 : 0
    );
    g_mutex_lock (&canonical_fingerprint_mutex);
    if (canonical_fingerprint_cache == NULL)
        canonical_fingerprint_cache = g_hash_table_new_full (
            g_str_hash,
            g_str_equal,
            g_free,
            g_free
        );
    char *fingerprint = g_hash_table_lookup (
        canonical_fingerprint_cache,
        key
    );
    if (fingerprint == NULL) {
        if (!create_canonical_schema_fingerprint (
                version,
                migrate_from_v4,
                bootstrap_direct_v6,
                &fingerprint,
                error
            )) {
            g_mutex_unlock (&canonical_fingerprint_mutex);
            g_free (key);
            return FALSE;
        }
        g_hash_table_insert (
            canonical_fingerprint_cache,
            g_strdup (key),
            fingerprint
        );
    }
    *out_fingerprint = fingerprint;
    g_mutex_unlock (&canonical_fingerprint_mutex);
    g_free (key);
    return TRUE;
}

static gboolean
canonical_object_name (
    gint version,
    const char *type,
    const char *name
)
{
    static const char *table_names[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series",
        "chart_presentations_v4"
    };
    static const char *index_names[] = {
        "idx_messages_conversation_sequence",
        "idx_citations_repository_snapshot",
        "idx_chart_series_repository_snapshot",
        "idx_chart_presentations_series_id"
    };
    static const char *trigger_names[] = {
        "chart_series_tuple_insert",
        "chart_series_tuple_update",
        "message_charts_tuple_update",
        "chart_presentations_v4_tuple_insert",
        "chart_presentations_v4_tuple_update",
        "chart_presentations_v4_delete_guard",
        "message_charts_presentation_update"
    };
    if (g_str_equal (type, "table")) {
        gsize count = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
            ? G_N_ELEMENTS (table_names)
            : G_N_ELEMENTS (table_names) - 1;
        for (gsize i = 0; i < count; i++)
            if (g_str_equal (name, table_names[i]))
                return TRUE;
    } else if (g_str_equal (type, "index")) {
        gsize count = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
            ? G_N_ELEMENTS (index_names)
            : G_N_ELEMENTS (index_names) - 1;
        for (gsize i = 0; i < count; i++)
            if (g_str_equal (name, index_names[i]))
                return TRUE;
    } else if (g_str_equal (type, "trigger")) {
        gsize count = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
            ? G_N_ELEMENTS (trigger_names)
            : 3;
        for (gsize i = 0; i < count; i++)
            if (g_str_equal (name, trigger_names[i]))
                return TRUE;
    }
    return FALSE;
}

static const char *
canonical_object_table (
    const char *name
)
{
    if (g_str_equal (name, "idx_messages_conversation_sequence"))
        return "messages";
    if (g_str_equal (name, "idx_citations_repository_snapshot"))
        return "citations";
    if (g_str_equal (name, "idx_chart_series_repository_snapshot"))
        return "chart_series";
    if (g_str_equal (name, "idx_chart_presentations_series_id"))
        return "chart_presentations_v4";
    if (g_str_has_prefix (name, "chart_series_tuple_"))
        return "chart_series";
    if (g_str_equal (name, "message_charts_tuple_update") ||
        g_str_equal (name, "message_charts_presentation_update"))
        return "message_charts";
    if (g_str_has_prefix (name, "chart_presentations_v4_"))
        return "chart_presentations_v4";
    return name;
}

static gboolean
report_schema_inventory_mismatch (
    sqlite3 *db,
    gint version,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2 (
        db,
        "SELECT type,name,tbl_name,sql FROM main.sqlite_schema "
        "ORDER BY type,name;",
        -1,
        &statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not inspect conversation-store schema objects"
        );
        return FALSE;
    }

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        const char *type = (const char *) sqlite3_column_text (statement, 0);
        const char *name = (const char *) sqlite3_column_text (statement, 1);
        const char *table = (const char *) sqlite3_column_text (statement, 2);
        gboolean generated_autoindex =
            type != NULL &&
            name != NULL &&
            g_str_equal (type, "index") &&
            sqlite3_column_type (statement, 3) == SQLITE_NULL &&
            g_str_has_prefix (name, "sqlite_autoindex_");
        if (generated_autoindex)
            continue;
        if (!canonical_object_name (version, type, name)) {
            g_set_error (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_SCHEMA,
                "Conversation-store migration refused: object_type=%s; "
                "object_name=%s; table_name=%s; reason=noncanonical-object.",
                type != NULL ? type : "unknown",
                name != NULL ? name : "unknown",
                table != NULL ? table : "unknown"
            );
            sqlite3_finalize (statement);
            return FALSE;
        }
    }
    sqlite3_finalize (statement);
    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not enumerate conversation-store schema objects"
        );
        return FALSE;
    }

    typedef struct {
        const char *type;
        const char *name;
    } SchemaObject;
    static const SchemaObject required_v5[] = {
        { "table", "installation" },
        { "table", "conversations" },
        { "table", "conversation_repositories" },
        { "table", "messages" },
        { "table", "citations" },
        { "table", "message_charts" },
        { "table", "chart_series" },
        { "index", "idx_messages_conversation_sequence" },
        { "index", "idx_citations_repository_snapshot" },
        { "index", "idx_chart_series_repository_snapshot" },
        { "trigger", "chart_series_tuple_insert" },
        { "trigger", "chart_series_tuple_update" },
        { "trigger", "message_charts_tuple_update" }
    };
    static const SchemaObject required_v6[] = {
        { "table", "installation" },
        { "table", "conversations" },
        { "table", "conversation_repositories" },
        { "table", "messages" },
        { "table", "citations" },
        { "table", "message_charts" },
        { "table", "chart_series" },
        { "table", "chart_presentations_v4" },
        { "index", "idx_messages_conversation_sequence" },
        { "index", "idx_citations_repository_snapshot" },
        { "index", "idx_chart_series_repository_snapshot" },
        { "index", "idx_chart_presentations_series_id" },
        { "trigger", "chart_series_tuple_insert" },
        { "trigger", "chart_series_tuple_update" },
        { "trigger", "message_charts_tuple_update" },
        { "trigger", "chart_presentations_v4_tuple_insert" },
        { "trigger", "chart_presentations_v4_tuple_update" },
        { "trigger", "chart_presentations_v4_delete_guard" },
        { "trigger", "message_charts_presentation_update" }
    };
    gsize required_count = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
        ? G_N_ELEMENTS (required_v6)
        : G_N_ELEMENTS (required_v5);
    for (gsize i = 0; i < required_count; i++) {
        const SchemaObject *item =
            version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
                ? &required_v6[i]
                : &required_v5[i];
        const char *type = item->type;
        const char *name = item->name;
        sqlite3_stmt *lookup = NULL;
        rc = sqlite3_prepare_v2 (
            db,
            "SELECT 1 FROM main.sqlite_schema WHERE type=?1 AND name=?2;",
            -1,
            &lookup,
            NULL
        );
        if (rc == SQLITE_OK) {
            sqlite3_bind_text (lookup, 1, type, -1, SQLITE_STATIC);
            sqlite3_bind_text (lookup, 2, name, -1, SQLITE_STATIC);
            rc = sqlite3_step (lookup);
        }
        gboolean exists = rc == SQLITE_ROW;
        sqlite3_finalize (lookup);
        if (!exists) {
            g_set_error (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_SCHEMA,
                "Conversation-store migration refused: object_type=%s; "
                "object_name=%s; table_name=%s; reason=missing-canonical-object.",
                type,
                name,
                canonical_object_table (name)
            );
            return FALSE;
        }
    }

    sqlite3 *canonical[3] = { NULL, NULL, NULL };
    guint canonical_count =
        version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION ? 3 : 2;
    for (guint variant = 0; variant < canonical_count; variant++) {
        if (!create_canonical_schema_db (
                version,
                variant == 1,
                variant == 2,
                &canonical[variant],
                error
            )) {
            for (guint i = 0; i < canonical_count; i++)
                if (canonical[i] != NULL)
                    sqlite3_close (canonical[i]);
            return FALSE;
        }
    }

    rc = sqlite3_prepare_v2 (
        db,
        "SELECT type,name,tbl_name,sql FROM main.sqlite_schema "
        "WHERE sql IS NOT NULL ORDER BY type,name;",
        -1,
        &statement,
        NULL
    );
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
            const char *type = (const char *) sqlite3_column_text (statement, 0);
            const char *name = (const char *) sqlite3_column_text (statement, 1);
            const char *table = (const char *) sqlite3_column_text (statement, 2);
            const char *sql = (const char *) sqlite3_column_text (statement, 3);
            gboolean matches_variant = FALSE;
            for (guint i = 0; i < canonical_count; i++) {
                sqlite3_stmt *expected = NULL;
                int expected_rc = sqlite3_prepare_v2 (
                    canonical[i],
                    "SELECT tbl_name,sql FROM main.sqlite_schema "
                    "WHERE type=?1 AND name=?2;",
                    -1,
                    &expected,
                    NULL
                );
                if (expected_rc == SQLITE_OK) {
                    sqlite3_bind_text (expected, 1, type, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text (expected, 2, name, -1, SQLITE_TRANSIENT);
                    expected_rc = sqlite3_step (expected);
                }
                if (expected_rc == SQLITE_ROW) {
                    const char *expected_table =
                        (const char *) sqlite3_column_text (expected, 0);
                    const char *expected_sql =
                        (const char *) sqlite3_column_text (expected, 1);
                    char *normalized_sql = normalize_schema_sql (
                        sql != NULL ? sql : ""
                    );
                    char *normalized_expected_sql = normalize_schema_sql (
                        expected_sql != NULL ? expected_sql : ""
                    );
                    matches_variant =
                        g_strcmp0 (table, expected_table) == 0 &&
                        g_strcmp0 (
                            normalized_sql,
                            normalized_expected_sql
                        ) == 0;
                    g_free (normalized_sql);
                    g_free (normalized_expected_sql);
                }
                sqlite3_finalize (expected);
                if (matches_variant)
                    break;
            }
            if (!matches_variant) {
                g_set_error (
                    error,
                    ATM_CONVERSATION_STORE_ERROR,
                    ATM_CONVERSATION_STORE_ERROR_SCHEMA,
                    "Conversation-store migration refused: object_type=%s; "
                    "object_name=%s; table_name=%s; "
                    "reason=canonical-definition-mismatch.",
                    type != NULL ? type : "unknown",
                    name != NULL ? name : "unknown",
                    table != NULL ? table : "unknown"
                );
                sqlite3_finalize (statement);
                for (guint i = 0; i < canonical_count; i++)
                    sqlite3_close (canonical[i]);
                return FALSE;
            }
        }
    }
    sqlite3_finalize (statement);
    for (guint i = 0; i < canonical_count; i++)
        sqlite3_close (canonical[i]);
    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not compare canonical conversation-store definitions"
        );
        return FALSE;
    }

    g_set_error_literal (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_SCHEMA,
        "Conversation-store migration refused: object_type=database; "
        "object_name=main; table_name=unknown; "
        "reason=canonical-pragma-or-encoding-mismatch."
    );
    return FALSE;
}

static gboolean
validate_schema_inventory (
    sqlite3 *db,
    gint version,
    GError **error
)
{
    GString *actual = g_string_new (NULL);
    if (!append_schema_fingerprint (db, version, actual, error)) {
        g_string_free (actual, TRUE);
        return FALSE;
    }

    gboolean matches = FALSE;
    guint variant_count = version == ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION
        ? 3
        : 2;
    for (guint variant = 0; variant < variant_count; variant++) {
        const char *expected = NULL;
        if (!get_canonical_fingerprint (
                version,
                variant == 1,
                variant == 2,
                &expected,
                error
            )) {
            g_string_free (actual, TRUE);
            return FALSE;
        }
        if (g_str_equal (actual->str, expected)) {
            matches = TRUE;
            break;
        }
    }
    g_string_free (actual, TRUE);
    if (matches)
        return TRUE;
    return report_schema_inventory_mismatch (db, version, error);
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
configure_readonly_connection_security (
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

    if (sqlite3_db_readonly (
            db,
            "main"
        ) != 1) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_IO,
            "Conversation-store root query did not open the database read-only."
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
            "Could not configure read-only conversation-store busy timeout"
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
            "PRAGMA query_only=ON;",
            error
        )) {
        return FALSE;
    }

    gint64 query_only = 0;

    if (!query_single_int64 (
            db,
            "PRAGMA query_only;",
            &query_only,
            error
        )) {
        return FALSE;
    }

    if (query_only != 1) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store read-only root query could not enforce query_only."
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
    char *migration_sql = NULL;

    if (!load_schema_sql (&schema_sql, error) ||
        !load_migration_v5_v6_sql (&migration_sql, error)) {
        g_free (schema_sql);
        g_free (migration_sql);
        return FALSE;
    }

    gboolean ok = exec_sql (db, "BEGIN IMMEDIATE;", error) &&
        exec_sql (db, schema_sql, error);
    char *identity_sql = g_strdup_printf (
        "PRAGMA application_id=%u;PRAGMA user_version=%d;",
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID,
        ATM_CONVERSATION_STORE_SCHEMA_V5_VERSION
    );
    if (ok)
        ok = exec_sql (db, identity_sql, error) &&
            exec_sql (db, migration_sql, error);
    g_free (identity_sql);
    if (ok)
        ok = exec_sql (
                db,
                "PRAGMA user_version=6;",
                error
            ) &&
            validate_v6_for_migration (db, error);
    if (ok)
        ok = exec_sql (db, "COMMIT;", error);
    if (!ok)
        sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);

    g_free (schema_sql);
    g_free (migration_sql);
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
validate_base_semantics (
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
validate_chart_semantics (
    sqlite3 *db,
    GError **error
)
{
    gint64 invalid_chart_message = 0;
    gint64 invalid_chart_tuple = 0;
    gint64 invalid_chart_series_shape = 0;
    gint64 invalid_chart_provenance = 0;

    if (!query_single_int64 (
            db,
            "SELECT count(*) "
            "FROM message_charts c "
            "JOIN messages m ON m.message_id=c.message_id "
            "WHERE m.role!='assistant' OR m.grounded!=1;",
            &invalid_chart_message,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) FROM message_charts c "
            "WHERE NOT ("
            "(c.chart_schema='atm-chart-spec/1' AND "
            " c.reconstruction_profile='atm-chart-reconstruct/gistemp-complete-annual/1') OR "
            "(c.chart_schema='atm-chart-spec/2' AND "
            " c.reconstruction_profile='atm-chart-reconstruct/energy-institute-tes-complete-annual/1') OR "
            "(c.chart_schema='atm-chart-spec/3' AND "
            " c.reconstruction_profile='atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1')"
            ");",
            &invalid_chart_tuple,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) FROM ("
            "SELECT c.message_id, c.ordinal, "
            "count(s.series_ordinal) AS series_count, "
            "min(s.series_ordinal) AS first_ordinal, "
            "max(s.series_ordinal) AS last_ordinal "
            "FROM message_charts c "
            "LEFT JOIN chart_series s "
            "ON s.message_id=c.message_id "
            "AND s.chart_ordinal=c.ordinal "
            "GROUP BY c.message_id, c.ordinal "
            "HAVING series_count!=1 "
            "OR first_ordinal!=0 OR last_ordinal!=0"
            ");",
            &invalid_chart_series_shape,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) "
            "FROM chart_series s "
            "JOIN messages m ON m.message_id=s.message_id "
            "JOIN message_charts c ON c.message_id=s.message_id "
            "AND c.ordinal=s.chart_ordinal "
            "LEFT JOIN conversation_repositories r "
            "ON r.conversation_id=m.conversation_id "
            "AND r.repository_id=s.repository_id "
            "AND r.snapshot_sha=s.snapshot_sha "
            "AND ("
            " (c.chart_schema='atm-chart-spec/1' AND r.repository_version=s.repository_version) OR "
            " (c.chart_schema='atm-chart-spec/2' AND s.repository_version='snapshot:' || s.snapshot_sha) OR "
            " (c.chart_schema='atm-chart-spec/3' AND s.repository_version='snapshot:' || s.snapshot_sha)"
            ") "
            "WHERE r.repository_id IS NULL "
            "OR s.repository_id NOT IN ('ewd','rmd') "
            "OR NOT ("
            "(c.chart_schema='atm-chart-spec/1' AND "
            " c.reconstruction_profile='atm-chart-reconstruct/gistemp-complete-annual/1' AND "
            " s.series_profile='atm-series/gistemp-complete-annual/1' AND "
            " s.admission_profile='atm-gistemp-pinned-admission/1' AND "
            " s.source_path='science/data/processed/nasa_gistemp_global_2026.csv') OR "
            "(c.chart_schema='atm-chart-spec/2' AND "
            " c.reconstruction_profile='atm-chart-reconstruct/energy-institute-tes-complete-annual/1' AND "
            " s.series_profile='atm-series/energy-institute-tes-complete-annual/1' AND "
            " s.admission_profile='atm-energy-institute-pinned-admission/3' AND "
            " s.source_path='science/data/processed/energy_institute_global_2026.csv' AND "
            " ((s.snapshot_sha='d9e249339663015f6d1c05752338a955bf64ad0b' AND "
            "   s.repository_version='snapshot:d9e249339663015f6d1c05752338a955bf64ad0b') OR "
            "  (s.snapshot_sha='dc710d8e604c0fff713a3acfdb18ab47a1a902bb' AND "
            "   s.repository_version='snapshot:dc710d8e604c0fff713a3acfdb18ab47a1a902bb')))"
            " OR (c.chart_schema='atm-chart-spec/3' AND "
            " c.reconstruction_profile='atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1' AND "
            " s.series_profile='atm-series/bnr-bls-c01-calendar-quarter/1' AND "
            " s.admission_profile='atm-bnr-bls-canonical-panel-admission/1' AND "
            " s.repository_id='rmd' AND "
            " s.source_path='data/processed/bnr_bls_realised_rounds.csv' AND "
            " s.snapshot_sha='e43eb4e899f123d3b1aa0f6e20b7c8ed35434170' AND "
            " s.repository_version='snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170')"
            ");",
            &invalid_chart_provenance,
            error
        )) {
        return FALSE;
    }

    if (invalid_chart_message != 0 ||
        invalid_chart_tuple != 0 ||
        invalid_chart_series_shape != 0 ||
        invalid_chart_provenance != 0) {
        g_set_error (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation-store chart semantic validation failed "
            "(message=%" G_GINT64_FORMAT
            ", tuple=%" G_GINT64_FORMAT
            ", series=%" G_GINT64_FORMAT
            ", provenance=%" G_GINT64_FORMAT ").",
            invalid_chart_message,
            invalid_chart_tuple,
            invalid_chart_series_shape,
            invalid_chart_provenance
        );
        return FALSE;
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

static gboolean
is_hex64 (
    const char *value
)
{
    if (value == NULL || strlen (value) != 64) return FALSE;
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (!((*cursor >= '0' && *cursor <= '9') ||
              (*cursor >= 'a' && *cursor <= 'f'))) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
chart_series_input_is_valid (
    const AtmConversationChartSeriesInput *series,
    const char *chart_schema
)
{
    if (series == NULL ||
        !is_hex64 (series->scientific_id) ||
        !is_hex64 (series->qualified_id) ||
        (g_strcmp0 (series->repository_id, "ewd") != 0 &&
         g_strcmp0 (series->repository_id, "rmd") != 0) ||
        !is_sha40 (series->snapshot_sha))
        return FALSE;

    if (g_strcmp0 (chart_schema, "atm-chart-spec/1") == 0) {
        return
            g_strcmp0 (series->repository_id, "ewd") == 0 &&
            g_strcmp0 (series->series_profile,
                "atm-series/gistemp-complete-annual/1") == 0 &&
            g_strcmp0 (series->admission_profile,
                "atm-gistemp-pinned-admission/1") == 0 &&
            nonempty (series->repository_version) &&
            g_strcmp0 (series->source_path,
                "science/data/processed/nasa_gistemp_global_2026.csv") == 0;
    }

    if (g_strcmp0 (chart_schema, "atm-chart-spec/2") == 0) {
        const char *snapshot = series->snapshot_sha;
        gboolean admitted_snapshot =
            g_strcmp0 (snapshot, "d9e249339663015f6d1c05752338a955bf64ad0b") == 0 ||
            g_strcmp0 (snapshot, "dc710d8e604c0fff713a3acfdb18ab47a1a902bb") == 0;
        char *expected_version = g_strconcat ("snapshot:", snapshot, NULL);
        gboolean valid =
            admitted_snapshot &&
            g_strcmp0 (series->repository_id, "ewd") == 0 &&
            g_strcmp0 (series->series_profile,
                "atm-series/energy-institute-tes-complete-annual/1") == 0 &&
            g_strcmp0 (series->admission_profile,
                "atm-energy-institute-pinned-admission/3") == 0 &&
            g_strcmp0 (series->repository_version, expected_version) == 0 &&
            g_strcmp0 (series->source_path,
                "science/data/processed/energy_institute_global_2026.csv") == 0;
        g_free (expected_version);
        return valid;
    }

    if (g_strcmp0 (chart_schema, "atm-chart-spec/3") == 0) {
        return
            g_strcmp0 (series->repository_id, "rmd") == 0 &&
            g_strcmp0 (series->series_profile,
                "atm-series/bnr-bls-c01-calendar-quarter/1") == 0 &&
            g_strcmp0 (series->admission_profile,
                "atm-bnr-bls-canonical-panel-admission/1") == 0 &&
            g_strcmp0 (series->repository_version,
                "snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170") == 0 &&
            g_strcmp0 (series->snapshot_sha,
                "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170") == 0 &&
            g_strcmp0 (series->source_path,
                "data/processed/bnr_bls_realised_rounds.csv") == 0;
    }

    return FALSE;
}

static gboolean
chart_input_is_valid (
    const AtmConversationChartInput *chart
)
{
    if (chart == NULL ||
        chart->ordinal < 1 || chart->ordinal > 4 ||
        !is_hex64 (chart->chart_spec_id) ||
        (g_strcmp0 (chart->chart_kind, "line") != 0 &&
         g_strcmp0 (chart->chart_kind, "scatter") != 0) ||
        chart->series == NULL ||
        chart->series_count != 1 ||
        !chart_series_input_is_valid (&chart->series[0], chart->chart_schema))
        return FALSE;

    if (g_strcmp0 (chart->chart_schema, "atm-chart-spec/1") == 0)
        return g_strcmp0 (chart->reconstruction_profile,
            "atm-chart-reconstruct/gistemp-complete-annual/1") == 0;

    if (g_strcmp0 (chart->chart_schema, "atm-chart-spec/2") == 0)
        return g_strcmp0 (chart->reconstruction_profile,
            "atm-chart-reconstruct/energy-institute-tes-complete-annual/1") == 0;

    if (g_strcmp0 (chart->chart_schema, "atm-chart-spec/3") == 0)
        return g_strcmp0 (chart->reconstruction_profile,
            "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1") == 0;

    return FALSE;
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
        "citations",
        "message_charts",
        "chart_series",
        "chart_presentations_v4"
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
        validate_base_semantics (
            store->db,
            error
        ) &&
        validate_chart_semantics (
            store->db,
            error
        ) &&
        validate_v6_presentation_semantics (
            store->db,
            error
        ) &&
        validate_schema_inventory (
            store->db,
            ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION,
            error
        );
}

static gboolean
open_existing_conversation_store_readonly (
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
            "Read-only conversation-root query received invalid open arguments."
        );
        return FALSE;
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2 (
        path,
        &db,
        SQLITE_OPEN_READONLY |
        SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_NOFOLLOW,
        NULL
    );

    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONVERSATION_STORE_ERROR_IO,
            "Could not open existing conversation store read-only"
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

    if (!configure_readonly_connection_security (
            db,
            error
        ) ||
        !atm_conversation_store_validate (
            store,
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


static gboolean
validate_v1_for_migration (
    sqlite3 *db,
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

    for (gsize i = 0;
         i < G_N_ELEMENTS (required_tables);
         i++) {
        if (!required_table_exists (
                db,
                required_tables[i],
                error
            )) {
            return FALSE;
        }
    }

    char *schema_id = NULL;

    if (!query_single_text (
            db,
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
            ATM_CONVERSATION_STORE_SCHEMA_V1_ID
        ) == 0;
    g_free (schema_id);

    if (!schema_id_ok) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v1 schema identity is invalid."
        );
        return FALSE;
    }

    return validate_integrity (
            db,
            error
        ) &&
        validate_base_semantics (
            db,
            error
        );
}

static gboolean
validate_v2_for_migration (
    sqlite3 *db,
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

    for (gsize i = 0; i < G_N_ELEMENTS (required_tables); i++) {
        if (!required_table_exists (db, required_tables[i], error)) {
            return FALSE;
        }
    }

    char *schema_id = NULL;
    if (!query_single_text (
            db,
            "SELECT schema_id FROM installation WHERE singleton_id=1;",
            &schema_id,
            error
        )) {
        return FALSE;
    }

    gboolean schema_id_ok =
        g_strcmp0 (schema_id, ATM_CONVERSATION_STORE_SCHEMA_V2_ID) == 0;
    g_free (schema_id);

    if (!schema_id_ok) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v2 schema identity is invalid."
        );
        return FALSE;
    }

    return validate_integrity (db, error) &&
        validate_base_semantics (db, error);
}

static gboolean
validate_v3_for_migration (
    sqlite3 *db,
    GError **error
)
{
    static const char *required_tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series"
    };
    gint64 application_id = 0;
    gint64 user_version = 0;
    for (gsize i = 0; i < G_N_ELEMENTS (required_tables); i++) {
        if (!required_table_exists (db, required_tables[i], error))
            return FALSE;
    }
    if (!query_single_int64 (db, "PRAGMA application_id;", &application_id, error) ||
        !query_single_int64 (db, "PRAGMA user_version;", &user_version, error))
        return FALSE;
    if (application_id != (gint64) ATM_CONVERSATION_STORE_APPLICATION_ID ||
        user_version != ATM_CONVERSATION_STORE_SCHEMA_V3_VERSION) {
        g_set_error_literal (error, ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v3 identity/version is invalid for migration.");
        return FALSE;
    }
    char *schema_id = NULL;
    if (!query_single_text (db,
            "SELECT schema_id FROM installation WHERE singleton_id=1;",
            &schema_id, error))
        return FALSE;
    gboolean schema_ok =
        g_strcmp0 (schema_id, ATM_CONVERSATION_STORE_SCHEMA_V3_ID) == 0;
    g_free (schema_id);
    if (!schema_ok) {
        g_set_error_literal (error, ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v3 schema identity is invalid for migration.");
        return FALSE;
    }
    return validate_integrity (db, error) &&
        validate_base_semantics (db, error) &&
        validate_chart_semantics (db, error);
}

static gboolean
validate_v4_for_migration (
    sqlite3 *db,
    GError **error
)
{
    static const char *required_tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series"
    };
    gint64 application_id = 0;
    gint64 user_version = 0;
    for (gsize i = 0; i < G_N_ELEMENTS (required_tables); i++) {
        if (!required_table_exists (db, required_tables[i], error))
            return FALSE;
    }
    if (!query_single_int64 (db, "PRAGMA application_id;", &application_id, error) ||
        !query_single_int64 (db, "PRAGMA user_version;", &user_version, error))
        return FALSE;
    if (application_id != (gint64) ATM_CONVERSATION_STORE_APPLICATION_ID ||
        user_version != ATM_CONVERSATION_STORE_SCHEMA_V4_VERSION) {
        g_set_error_literal (error, ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v4 identity/version is invalid for migration.");
        return FALSE;
    }
    char *schema_id = NULL;
    if (!query_single_text (db,
            "SELECT schema_id FROM installation WHERE singleton_id=1;",
            &schema_id, error))
        return FALSE;
    gboolean schema_ok =
        g_strcmp0 (schema_id, ATM_CONVERSATION_STORE_SCHEMA_V4_ID) == 0;
    g_free (schema_id);
    if (!schema_ok) {
        g_set_error_literal (error, ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v4 schema identity is invalid for migration.");
        return FALSE;
    }
    return validate_integrity (db, error) &&
        validate_base_semantics (db, error) &&
        validate_chart_semantics (db, error);
}

static gboolean
validate_v5_for_migration (
    sqlite3 *db,
    GError **error
)
{
    static const char *required_tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series"
    };
    gint64 application_id = 0;
    gint64 user_version = 0;
    for (gsize i = 0; i < G_N_ELEMENTS (required_tables); i++) {
        if (!required_table_exists (db, required_tables[i], error))
            return FALSE;
    }
    if (!query_single_int64 (db, "PRAGMA application_id;", &application_id, error) ||
        !query_single_int64 (db, "PRAGMA user_version;", &user_version, error))
        return FALSE;
    if (application_id != (gint64) ATM_CONVERSATION_STORE_APPLICATION_ID ||
        user_version != ATM_CONVERSATION_STORE_SCHEMA_V5_VERSION) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v5 identity/version is invalid for migration."
        );
        return FALSE;
    }

    char *schema_id = NULL;
    if (!query_single_text (
            db,
            "SELECT schema_id FROM installation WHERE singleton_id=1;",
            &schema_id,
            error
        ))
        return FALSE;
    gboolean schema_ok = g_strcmp0 (
        schema_id,
        ATM_CONVERSATION_STORE_SCHEMA_V5_ID
    ) == 0;
    g_free (schema_id);
    if (!schema_ok) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v5 schema identity is invalid for migration."
        );
        return FALSE;
    }
    return validate_integrity (db, error) &&
        validate_base_semantics (db, error) &&
        validate_chart_semantics (db, error) &&
        validate_schema_inventory (
            db,
            ATM_CONVERSATION_STORE_SCHEMA_V5_VERSION,
            error
        );
}

static gboolean
validate_v6_presentation_semantics (
    sqlite3 *db,
    GError **error
)
{
    gint64 invalid_presentations = 0;
    if (!query_single_int64 (
            db,
            "SELECT count(*) FROM message_charts c "
            "LEFT JOIN chart_presentations_v4 p "
            "ON p.message_id=c.message_id AND p.chart_ordinal=c.ordinal "
            "WHERE (c.presentation_schema IS NULL AND p.message_id IS NOT NULL) "
            "OR (c.presentation_schema='atm-chart-spec/4' AND p.message_id IS NULL) "
            "OR (p.message_id IS NOT NULL AND NOT EXISTS ("
            "SELECT 1 FROM chart_series s "
            "WHERE s.message_id=c.message_id AND s.chart_ordinal=c.ordinal "
            "AND s.series_ordinal=0 "
            "AND p.presentation_schema=c.presentation_schema "
            "AND p.source_chart_schema=c.chart_schema "
            "AND p.source_chart_spec_id=c.chart_spec_id "
            "AND p.reconstruction_profile_id=c.reconstruction_profile "
            "AND p.series_profile_id=s.series_profile "
            "AND p.admission_profile_id=s.admission_profile"
            "));",
            &invalid_presentations,
            error
        ))
        return FALSE;
    if (invalid_presentations != 0) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Conversation-store v4 presentation sidecar does not match its source chart."
        );
        return FALSE;
    }
    return TRUE;
}

static gboolean
validate_v6_for_migration (
    sqlite3 *db,
    GError **error
)
{
    static const char *required_tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series",
        "chart_presentations_v4"
    };
    gint64 application_id = 0;
    gint64 user_version = 0;
    for (gsize i = 0; i < G_N_ELEMENTS (required_tables); i++) {
        if (!required_table_exists (db, required_tables[i], error))
            return FALSE;
    }
    if (!query_single_int64 (db, "PRAGMA application_id;", &application_id, error) ||
        !query_single_int64 (db, "PRAGMA user_version;", &user_version, error))
        return FALSE;
    if (application_id != (gint64) ATM_CONVERSATION_STORE_APPLICATION_ID ||
        user_version != ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v6 identity/version is invalid."
        );
        return FALSE;
    }

    char *schema_id = NULL;
    if (!query_single_text (
            db,
            "SELECT schema_id FROM installation WHERE singleton_id=1;",
            &schema_id,
            error
        ))
        return FALSE;
    gboolean schema_ok = g_strcmp0 (
        schema_id,
        ATM_CONVERSATION_STORE_SCHEMA_V6_ID
    ) == 0;
    g_free (schema_id);
    if (!schema_ok) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SCHEMA,
            "Conversation-store v6 schema identity is invalid."
        );
        return FALSE;
    }
    return validate_integrity (db, error) &&
        validate_base_semantics (db, error) &&
        validate_chart_semantics (db, error) &&
        validate_v6_presentation_semantics (db, error) &&
        validate_schema_inventory (
            db,
            ATM_CONVERSATION_STORE_SCHEMA_V6_VERSION,
            error
        );
}


static gboolean
migrate_v1_to_v3 (
    AtmConversationStore *store,
    GError **error
)
{
    if (store == NULL || store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store schema migration requires an open store."
        );
        return FALSE;
    }

    if (!validate_v1_for_migration (store->db, error)) {
        return FALSE;
    }

    char *migration_v1_v2_sql = NULL;
    char *migration_v2_v3_sql = NULL;

    if (!load_migration_v1_v2_sql (&migration_v1_v2_sql, error) ||
        !load_migration_v2_v3_sql (&migration_v2_v3_sql, error)) {
        g_free (migration_v1_v2_sql);
        g_free (migration_v2_v3_sql);
        return FALSE;
    }

    if (!exec_sql (store->db, "BEGIN IMMEDIATE;", error)) {
        g_free (migration_v1_v2_sql);
        g_free (migration_v2_v3_sql);
        return FALSE;
    }

    gboolean ok = FALSE;

    if (!exec_sql (store->db, migration_v1_v2_sql, error) ||
        !validate_v2_for_migration (store->db, error) ||
        !exec_sql (store->db, migration_v2_v3_sql, error)) {
        goto rollback;
    }

    char *version_sql = g_strdup_printf (
        "PRAGMA user_version=%d;",
        ATM_CONVERSATION_STORE_SCHEMA_V3_VERSION
    );
    if (!exec_sql (store->db, version_sql, error)) {
        g_free (version_sql);
        goto rollback;
    }
    g_free (version_sql);

    if (!validate_v3_for_migration (store->db, error) ||
        !exec_sql (store->db, "COMMIT;", error)) {
        goto rollback;
    }

    ok = TRUE;

rollback:
    if (!ok) {
        sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
    }
    g_free (migration_v1_v2_sql);
    g_free (migration_v2_v3_sql);
    return ok;
}

static gboolean
migrate_v2_to_v3 (
    AtmConversationStore *store,
    GError **error
)
{
    if (store == NULL || store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store schema migration requires an open store."
        );
        return FALSE;
    }

    if (!validate_v2_for_migration (store->db, error)) {
        return FALSE;
    }

    char *migration_sql = NULL;
    if (!load_migration_v2_v3_sql (&migration_sql, error)) {
        return FALSE;
    }

    if (!exec_sql (store->db, "BEGIN IMMEDIATE;", error)) {
        g_free (migration_sql);
        return FALSE;
    }

    gboolean ok = FALSE;
    if (!exec_sql (store->db, migration_sql, error)) {
        goto rollback;
    }

    char *version_sql = g_strdup_printf (
        "PRAGMA user_version=%d;",
        ATM_CONVERSATION_STORE_SCHEMA_V3_VERSION
    );
    if (!exec_sql (store->db, version_sql, error)) {
        g_free (version_sql);
        goto rollback;
    }
    g_free (version_sql);

    if (!validate_v3_for_migration (store->db, error)) {
        goto rollback;
    }

    if (!exec_sql (store->db, "COMMIT;", error)) {
        goto rollback;
    }

    ok = TRUE;

rollback:
    if (!ok) {
        sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
    }
    g_free (migration_sql);
    return ok;
}

static gboolean
migrate_v3_to_v4 (
    AtmConversationStore *store,
    GError **error
)
{
    if (store == NULL || store->db == NULL) {
        g_set_error_literal (error, ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store schema migration requires an open store.");
        return FALSE;
    }
    if (!validate_v3_for_migration (store->db, error))
        return FALSE;

    char *migration_sql = NULL;
    if (!load_migration_v3_v4_sql (&migration_sql, error))
        return FALSE;
    if (!exec_sql (store->db, "BEGIN IMMEDIATE;", error)) {
        g_free (migration_sql);
        return FALSE;
    }

    gboolean ok = FALSE;
    if (!exec_sql (store->db, migration_sql, error))
        goto rollback;

    char *version_sql = g_strdup_printf ("PRAGMA user_version=%d;",
        ATM_CONVERSATION_STORE_SCHEMA_V4_VERSION);
    if (!exec_sql (store->db, version_sql, error)) {
        g_free (version_sql);
        goto rollback;
    }
    g_free (version_sql);

    if (!validate_v4_for_migration (store->db, error))
        goto rollback;
    if (!exec_sql (store->db, "COMMIT;", error))
        goto rollback;

    ok = TRUE;
rollback:
    if (!ok)
        sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
    g_free (migration_sql);
    return ok;
}

static gboolean
migrate_v4_to_v5 (
    AtmConversationStore *store,
    GError **error
)
{
    if (store == NULL || store->db == NULL) {
        g_set_error_literal (error, ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store schema migration requires an open store.");
        return FALSE;
    }
    char *migration_sql = NULL;
    if (!load_migration_v4_v5_sql (&migration_sql, error))
        return FALSE;
    if (!exec_sql (store->db, "BEGIN IMMEDIATE;", error)) {
        g_free (migration_sql);
        return FALSE;
    }

    gboolean ok = FALSE;
    if (!validate_v4_for_migration (store->db, error))
        goto rollback;
    if (!exec_sql (store->db, migration_sql, error))
        goto rollback;

    char *version_sql = g_strdup_printf ("PRAGMA user_version=%d;",
        ATM_CONVERSATION_STORE_SCHEMA_V5_VERSION);
    if (!exec_sql (store->db, version_sql, error)) {
        g_free (version_sql);
        goto rollback;
    }
    g_free (version_sql);

    if (!validate_v5_for_migration (store->db, error))
        goto rollback;
    if (!exec_sql (store->db, "COMMIT;", error))
        goto rollback;

    ok = TRUE;
rollback:
    if (!ok)
        sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
    g_free (migration_sql);
    return ok;
}

static gboolean
exec_v5_v6_migration_sql (
    sqlite3 *db,
    const char *sql,
    GError **error
)
{
    const char *cursor = sql;
    guint statement_number = 0;
    while (cursor != NULL && *cursor != '\0') {
        sqlite3_stmt *statement = NULL;
        const char *tail = NULL;
        int rc = sqlite3_prepare_v2 (
            db,
            cursor,
            -1,
            &statement,
            &tail
        );
        if (rc != SQLITE_OK) {
            set_sqlite_error (
                db,
                error,
                ATM_CONVERSATION_STORE_ERROR_SQLITE,
                "Could not prepare v5-to-v6 migration statement"
            );
            sqlite3_finalize (statement);
            return FALSE;
        }
        if (tail == cursor) {
            sqlite3_finalize (statement);
            break;
        }
        cursor = tail;
        if (statement == NULL)
            continue;
        while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        }
        sqlite3_finalize (statement);
        if (rc != SQLITE_DONE) {
            set_sqlite_error (
                db,
                error,
                ATM_CONVERSATION_STORE_ERROR_SQLITE,
                "Could not execute v5-to-v6 migration statement"
            );
            return FALSE;
        }
        statement_number++;
#ifdef ATM_CONVERSATION_STORE_TESTING
        if (fail_v5_v6_after_statement == statement_number) {
            fail_v5_v6_after_statement = 0;
            g_set_error (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_SQLITE,
                "Injected v5-to-v6 migration failure after statement %u.",
                statement_number
            );
            return FALSE;
        }
#endif
    }
    return TRUE;
}

static gboolean
migrate_v5_to_v6 (
    AtmConversationStore *store,
    GError **error
)
{
    if (store == NULL || store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation-store schema migration requires an open store."
        );
        return FALSE;
    }

    char *migration_sql = NULL;
    if (!load_migration_v5_v6_sql (&migration_sql, error))
        return FALSE;
    if (!exec_sql (store->db, "BEGIN IMMEDIATE;", error)) {
        g_free (migration_sql);
        return FALSE;
    }

#ifdef ATM_CONVERSATION_STORE_TESTING
    if (v5_v6_lock_hook != NULL)
        v5_v6_lock_hook (v5_v6_lock_hook_data);
#endif

    gboolean ok = FALSE;
    if (!validate_v5_for_migration (store->db, error) ||
        !exec_v5_v6_migration_sql (store->db, migration_sql, error))
        goto rollback;
#ifdef ATM_CONVERSATION_STORE_TESTING
    if (fail_v5_v6_after_sql_resource) {
        fail_v5_v6_after_sql_resource = FALSE;
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Injected v5-to-v6 migration failure after SQL resource execution."
        );
        goto rollback;
    }
#endif
    if (!exec_sql (store->db, "PRAGMA user_version=6;", error) ||
        !validate_v6_for_migration (store->db, error) ||
        !exec_sql (store->db, "COMMIT;", error))
        goto rollback;

    ok = TRUE;
rollback:
    if (!ok)
        sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
    g_free (migration_sql);
    return ok;
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
        user_version ==
            ATM_CONVERSATION_STORE_SCHEMA_V1_VERSION
    ) {
        if (!migrate_v1_to_v3 (store, error) ||
            !migrate_v3_to_v4 (store, error) ||
            !migrate_v4_to_v5 (store, error) ||
            !migrate_v5_to_v6 (store, error)) {
            atm_conversation_store_close (store);
            return FALSE;
        }
    } else if (
        user_version ==
            ATM_CONVERSATION_STORE_SCHEMA_V2_VERSION
    ) {
        if (!migrate_v2_to_v3 (store, error) ||
            !migrate_v3_to_v4 (store, error) ||
            !migrate_v4_to_v5 (store, error) ||
            !migrate_v5_to_v6 (store, error)) {
            atm_conversation_store_close (store);
            return FALSE;
        }
    } else if (
        user_version == ATM_CONVERSATION_STORE_SCHEMA_V3_VERSION
    ) {
        if (!migrate_v3_to_v4 (store, error) ||
            !migrate_v4_to_v5 (store, error) ||
            !migrate_v5_to_v6 (store, error)) {
            atm_conversation_store_close (store);
            return FALSE;
        }
    } else if (
        user_version == ATM_CONVERSATION_STORE_SCHEMA_V4_VERSION
    ) {
        if (!migrate_v4_to_v5 (store, error) ||
            !migrate_v5_to_v6 (store, error)) {
            atm_conversation_store_close (store);
            return FALSE;
        }
    } else if (
        user_version == ATM_CONVERSATION_STORE_SCHEMA_V5_VERSION
    ) {
        if (!migrate_v5_to_v6 (store, error)) {
            atm_conversation_store_close (store);
            return FALSE;
        }
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
atm_conversation_store_list_repository_generation_ids_readonly (
    const char *path,
    gint64 **out_generation_ids,
    gsize *out_count,
    GError **error
)
{
    if (!nonempty (path) ||
        out_generation_ids == NULL ||
        out_count == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation generation-root query received invalid arguments."
        );
        return FALSE;
    }

    *out_generation_ids = NULL;
    *out_count = 0;

    AtmConversationStore *store = NULL;

    if (!open_existing_conversation_store_readonly (
            path,
            &store,
            error
        )) {
        return FALSE;
    }

    sqlite3_stmt *statement = NULL;
    GArray *generation_ids =
        g_array_new (
            FALSE,
            FALSE,
            sizeof (gint64)
        );
    gboolean ok = FALSE;

    if (!prepare_statement (
            store->db,
            "SELECT DISTINCT repository_generation_id "
            "FROM conversations "
            "ORDER BY repository_generation_id;",
            &statement,
            error
        )) {
        goto done;
    }

    int rc;

    while ((rc = sqlite3_step (
                statement
            )) == SQLITE_ROW) {
        if (sqlite3_column_type (
                statement,
                0
            ) != SQLITE_INTEGER) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
                "Conversation repository generation root is not an integer."
            );
            goto done;
        }

        gint64 generation_id =
            sqlite3_column_int64 (
                statement,
                0
            );

        if (generation_id < 0) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
                "Conversation repository generation root is negative."
            );
            goto done;
        }

        if (generation_id == 0) {
            continue;
        }

        g_array_append_val (
            generation_ids,
            generation_id
        );
    }

    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not list conversation repository generation roots"
        );
        goto done;
    }

    *out_count = generation_ids->len;
    *out_generation_ids =
        (gint64 *) g_array_free (
            generation_ids,
            FALSE
        );
    generation_ids = NULL;
    ok = TRUE;

done:
    if (statement != NULL) {
        sqlite3_finalize (
            statement
        );
    }

    if (generation_ids != NULL) {
        g_array_free (
            generation_ids,
            TRUE
        );
    }

    atm_conversation_store_close (
        store
    );
    return ok;
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
atm_conversation_store_set_archived (
    AtmConversationStore *store,
    const char *conversation_id,
    gboolean archived,
    gint64 updated_at_us,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (conversation_id) ||
        updated_at_us < 0) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation archive update received invalid arguments."
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
            "SET archived=?2,"
            "open_on_startup=CASE WHEN ?2=1 THEN 0 ELSE open_on_startup END,"
            "updated_at_us=?3 "
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
    sqlite3_bind_int (
        statement,
        2,
        archived ? 1 : 0
    );
    sqlite3_bind_int64 (
        statement,
        3,
        updated_at_us
    );

    if (!step_done (
            store->db,
            statement,
            "Could not update conversation archive state",
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
            ATM_CONVERSATION_STORE_ERROR_NOT_FOUND,
            "Conversation archive update did not match exactly one conversation."
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
atm_conversation_store_set_open_on_startup (
    AtmConversationStore *store,
    const char *conversation_id,
    gboolean open_on_startup,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (conversation_id)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation startup-open update received invalid arguments."
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
            "SET open_on_startup=?2 "
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
    sqlite3_bind_int (
        statement,
        2,
        open_on_startup ? 1 : 0
    );

    if (!step_done (
            store->db,
            statement,
            "Could not update conversation startup-open state",
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
            ATM_CONVERSATION_STORE_ERROR_NOT_FOUND,
            "Conversation startup-open update did not match exactly one conversation."
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
atm_conversation_store_delete_conversation (
    AtmConversationStore *store,
    const char *conversation_id,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        !nonempty (conversation_id)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation delete received invalid arguments."
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
            "DELETE FROM conversations "
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

    if (!step_done (
            store->db,
            statement,
            "Could not delete conversation",
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
            ATM_CONVERSATION_STORE_ERROR_NOT_FOUND,
            "Conversation delete did not match exactly one conversation."
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
atm_conversation_store_attach_chart (
    AtmConversationStore *store,
    const char *conversation_id,
    gint64 turn_no,
    const AtmConversationChartInput *chart,
    GError **error
)
{
    if (store == NULL || store->db == NULL ||
        !nonempty (conversation_id) || turn_no < 0 ||
        !chart_input_is_valid (chart)) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_ARGUMENT,
            "Conversation chart attachment received an invalid reconstruction recipe."
        );
        return FALSE;
    }

    if (!exec_sql (store->db, "BEGIN IMMEDIATE;", error)) {
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *statement = NULL;
    char *message_id = NULL;

    if (!prepare_statement (
            store->db,
            "SELECT message_id,grounded "
            "FROM messages "
            "WHERE conversation_id=?1 AND turn_no=?2 AND role='assistant';",
            &statement,
            error
        )) {
        goto out;
    }

    sqlite3_bind_text (statement, 1, conversation_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64 (statement, 2, turn_no);

    int rc = sqlite3_step (statement);
    if (rc == SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_NOT_FOUND,
            "Grounded assistant turn for chart attachment was not found."
        );
        goto out;
    }
    if (rc != SQLITE_ROW) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not resolve chart attachment message"
        );
        goto out;
    }

    const unsigned char *message_value =
        sqlite3_column_text (statement, 0);
    if (message_value == NULL) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Chart attachment target message has no identity."
        );
        goto out;
    }
    message_id = g_strdup ((const char *) message_value);
    gboolean grounded = sqlite3_column_int (statement, 1) != 0;
    if (sqlite3_step (statement) != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Chart attachment target query returned multiple assistant messages."
        );
        goto out;
    }
    sqlite3_finalize (statement);
    statement = NULL;

    if (!grounded) {
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Charts may only be attached to grounded assistant messages."
        );
        goto out;
    }

    const AtmConversationChartSeriesInput *series = &chart->series[0];
    if (!prepare_statement (
            store->db,
            "SELECT count(*) "
            "FROM conversation_repositories "
            "WHERE conversation_id=?1 "
            "AND repository_id=?2 "
            "AND snapshot_sha=?4 "
            "AND ("
            " (?5='atm-chart-spec/1' AND repository_version=?3) OR "
            " (?5='atm-chart-spec/2' AND ?3=('snapshot:' || ?4)) OR "
            " (?5='atm-chart-spec/3' AND ?3=('snapshot:' || ?4))"
            ");",
            &statement,
            error
        )) {
        goto out;
    }
    sqlite3_bind_text (statement, 1, conversation_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 2, series->repository_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 3, series->repository_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 4, series->snapshot_sha, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 5, chart->chart_schema, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step (statement);
    if (rc != SQLITE_ROW || sqlite3_column_int64 (statement, 0) != 1 ||
        sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        statement = NULL;
        g_set_error_literal (
            error,
            ATM_CONVERSATION_STORE_ERROR,
            ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
            "Chart reconstruction source is outside the conversation's pinned repository scope."
        );
        goto out;
    }
    sqlite3_finalize (statement);
    statement = NULL;

    if (!prepare_statement (
            store->db,
            "INSERT INTO message_charts("
            "message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile"
            ") VALUES(?1,?2,?3,?4,?5,?6);",
            &statement,
            error
        )) {
        goto out;
    }
    sqlite3_bind_text (statement, 1, message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (statement, 2, (int) chart->ordinal);
    sqlite3_bind_text (statement, 3, chart->chart_schema, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 4, chart->chart_spec_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 5, chart->chart_kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 6, chart->reconstruction_profile, -1, SQLITE_TRANSIENT);
    if (!step_done (
            store->db,
            statement,
            "Could not persist conversation chart binding",
            error
        )) {
        goto out;
    }
    sqlite3_finalize (statement);
    statement = NULL;

    if (!prepare_statement (
            store->db,
            "INSERT INTO chart_series("
            "message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,"
            "scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path"
            ") VALUES(?1,?2,0,?3,?4,?5,?6,?7,?8,?9,?10);",
            &statement,
            error
        )) {
        goto out;
    }
    sqlite3_bind_text (statement, 1, message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (statement, 2, (int) chart->ordinal);
    sqlite3_bind_text (statement, 3, series->series_profile, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 4, series->admission_profile, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 5, series->scientific_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 6, series->qualified_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 7, series->repository_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 8, series->repository_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 9, series->snapshot_sha, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 10, series->source_path, -1, SQLITE_TRANSIENT);
    if (!step_done (
            store->db,
            statement,
            "Could not persist conversation chart series binding",
            error
        )) {
        goto out;
    }
    sqlite3_finalize (statement);
    statement = NULL;

    if (!validate_chart_semantics (store->db, error) ||
        !exec_sql (store->db, "COMMIT;", error)) {
        goto out;
    }

    ok = TRUE;

out:
    if (statement != NULL) sqlite3_finalize (statement);
    if (!ok) rollback_best_effort (store->db);
    g_free (message_id);
    return ok;
}

gboolean
atm_conversation_store_attach_chart_values (
    AtmConversationStore *store,
    const char *conversation_id,
    gint64 turn_no,
    guint ordinal,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *chart_kind,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *scientific_id,
    const char *qualified_id,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path,
    GError **error
)
{
    AtmConversationChartSeriesInput series = {
        .series_profile = series_profile,
        .admission_profile = admission_profile,
        .scientific_id = scientific_id,
        .qualified_id = qualified_id,
        .repository_id = repository_id,
        .repository_version = repository_version,
        .snapshot_sha = snapshot_sha,
        .source_path = source_path
    };
    AtmConversationChartInput chart = {
        .ordinal = ordinal,
        .chart_schema = chart_schema,
        .chart_spec_id = chart_spec_id,
        .chart_kind = chart_kind,
        .reconstruction_profile = reconstruction_profile,
        .series = &series,
        .series_count = 1
    };

    return atm_conversation_store_attach_chart (
        store,
        conversation_id,
        turn_no,
        &chart,
        error
    );
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
    const char * const *immutable_permalinks,
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
         excerpts == NULL ||
         immutable_permalinks == NULL)) {
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
                nonempty (immutable_permalinks[i])
                    ? immutable_permalinks[i]
                    : NULL;
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
snapshot_chart_series_free (
    gpointer data
)
{
    AtmConversationSnapshotChartSeries *series = data;
    if (series == NULL) return;
    g_free (series->series_profile);
    g_free (series->admission_profile);
    g_free (series->scientific_id);
    g_free (series->qualified_id);
    g_free (series->repository_id);
    g_free (series->repository_version);
    g_free (series->snapshot_sha);
    g_free (series->source_path);
    g_free (series);
}

static void
snapshot_chart_free (
    gpointer data
)
{
    AtmConversationSnapshotChart *chart = data;
    if (chart == NULL) return;
    g_free (chart->chart_schema);
    g_free (chart->chart_spec_id);
    g_free (chart->chart_kind);
    g_free (chart->reconstruction_profile);
    g_clear_pointer (&chart->series, g_ptr_array_unref);
    g_free (chart);
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
    g_clear_pointer (
        &message->charts,
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

static AtmConversationSnapshotChart *
snapshot_chart_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint chart_index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (snapshot, message_index);
    if (message == NULL || message->charts == NULL ||
        chart_index >= message->charts->len) {
        return NULL;
    }
    return g_ptr_array_index (message->charts, chart_index);
}

static AtmConversationSnapshotChartSeries *
snapshot_chart_series_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint chart_index,
    guint series_index
)
{
    AtmConversationSnapshotChart *chart =
        snapshot_chart_at (snapshot, message_index, chart_index);
    if (chart == NULL || chart->series == NULL ||
        series_index >= chart->series->len) {
        return NULL;
    }
    return g_ptr_array_index (chart->series, series_index);
}

static AtmConversationSnapshotChart *
snapshot_chart_for_ordinal (
    AtmConversationSnapshotMessage *message,
    gint64 ordinal
)
{
    if (message == NULL || message->charts == NULL) return NULL;
    for (guint i = 0; i < message->charts->len; i++) {
        AtmConversationSnapshotChart *chart =
            g_ptr_array_index (message->charts, i);
        if (chart->ordinal == ordinal) return chart;
    }
    return NULL;
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
            "conversation_id,title,created_at_us,updated_at_us,archived,open_on_startup "
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
        entry->open_on_startup =
            sqlite3_column_int (
                statement,
                5
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
        message->charts =
            g_ptr_array_new_with_free_func (
                snapshot_chart_free
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

    if (!prepare_statement (
            store->db,
            "SELECT m.sequence_no,c.ordinal,c.chart_schema,c.chart_spec_id,"
            "c.chart_kind,c.reconstruction_profile "
            "FROM message_charts c "
            "JOIN messages m ON m.message_id=c.message_id "
            "WHERE m.conversation_id=?1 "
            "ORDER BY m.sequence_no ASC,c.ordinal ASC;",
            &statement,
            error
        )) {
        goto out;
    }
    sqlite3_bind_text (statement, 1, conversation_id, -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        gint64 sequence_no = sqlite3_column_int64 (statement, 0);
        AtmConversationSnapshotMessage *message =
            snapshot_message_for_sequence (snapshot, sequence_no);
        if (message == NULL) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
                "Conversation chart refers to an unavailable snapshot message."
            );
            goto out;
        }

        AtmConversationSnapshotChart *chart =
            g_new0 (AtmConversationSnapshotChart, 1);
        chart->ordinal = sqlite3_column_int64 (statement, 1);
        chart->chart_schema = column_text_dup (statement, 2);
        chart->chart_spec_id = column_text_dup (statement, 3);
        chart->chart_kind = column_text_dup (statement, 4);
        chart->reconstruction_profile = column_text_dup (statement, 5);
        chart->series =
            g_ptr_array_new_with_free_func (snapshot_chart_series_free);
        g_ptr_array_add (message->charts, chart);
    }
    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation chart bindings"
        );
        goto out;
    }
    sqlite3_finalize (statement);
    statement = NULL;

    if (!prepare_statement (
            store->db,
            "SELECT m.sequence_no,s.chart_ordinal,s.series_ordinal,"
            "s.series_profile,s.admission_profile,s.scientific_id,s.qualified_id,"
            "s.repository_id,s.repository_version,s.snapshot_sha,s.source_path "
            "FROM chart_series s "
            "JOIN messages m ON m.message_id=s.message_id "
            "WHERE m.conversation_id=?1 "
            "ORDER BY m.sequence_no ASC,s.chart_ordinal ASC,s.series_ordinal ASC;",
            &statement,
            error
        )) {
        goto out;
    }
    sqlite3_bind_text (statement, 1, conversation_id, -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        gint64 sequence_no = sqlite3_column_int64 (statement, 0);
        gint64 chart_ordinal = sqlite3_column_int64 (statement, 1);
        gint64 series_ordinal = sqlite3_column_int64 (statement, 2);
        AtmConversationSnapshotMessage *message =
            snapshot_message_for_sequence (snapshot, sequence_no);
        AtmConversationSnapshotChart *chart =
            snapshot_chart_for_ordinal (message, chart_ordinal);
        if (chart == NULL || series_ordinal < 0 ||
            (guint) series_ordinal != chart->series->len) {
            g_set_error_literal (
                error,
                ATM_CONVERSATION_STORE_ERROR,
                ATM_CONVERSATION_STORE_ERROR_INTEGRITY,
                "Conversation chart series order is unavailable or non-contiguous."
            );
            goto out;
        }

        AtmConversationSnapshotChartSeries *series =
            g_new0 (AtmConversationSnapshotChartSeries, 1);
        series->series_ordinal = (guint) series_ordinal;
        series->series_profile = column_text_dup (statement, 3);
        series->admission_profile = column_text_dup (statement, 4);
        series->scientific_id = column_text_dup (statement, 5);
        series->qualified_id = column_text_dup (statement, 6);
        series->repository_id = column_text_dup (statement, 7);
        series->repository_version = column_text_dup (statement, 8);
        series->snapshot_sha = column_text_dup (statement, 9);
        series->source_path = column_text_dup (statement, 10);
        g_ptr_array_add (chart->series, series);
    }
    if (rc != SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONVERSATION_STORE_ERROR_SQLITE,
            "Could not read durable conversation chart series bindings"
        );
        goto out;
    }
    sqlite3_finalize (statement);
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

gboolean
atm_conversation_list_open_on_startup_at (
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
        entry->open_on_startup;
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

guint
atm_conversation_snapshot_message_chart_count_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index
)
{
    AtmConversationSnapshotMessage *message =
        snapshot_message_at (snapshot, message_index);
    return message != NULL && message->charts != NULL
        ? message->charts->len : 0;
}

gint64
atm_conversation_snapshot_chart_ordinal_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint chart_index
)
{
    AtmConversationSnapshotChart *chart =
        snapshot_chart_at (snapshot, message_index, chart_index);
    return chart != NULL ? chart->ordinal : -1;
}

#define SNAPSHOT_CHART_TEXT_ACCESSOR(name, field) \
const char * \
name ( \
    const AtmConversationSnapshot *snapshot, \
    guint message_index, \
    guint chart_index \
) \
{ \
    AtmConversationSnapshotChart *chart = \
        snapshot_chart_at (snapshot, message_index, chart_index); \
    return chart != NULL ? chart->field : NULL; \
}

SNAPSHOT_CHART_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_schema_at,
    chart_schema
)
SNAPSHOT_CHART_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_spec_id_at,
    chart_spec_id
)
SNAPSHOT_CHART_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_kind_at,
    chart_kind
)
SNAPSHOT_CHART_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_reconstruction_profile_at,
    reconstruction_profile
)

#undef SNAPSHOT_CHART_TEXT_ACCESSOR

guint
atm_conversation_snapshot_chart_series_count_at (
    const AtmConversationSnapshot *snapshot,
    guint message_index,
    guint chart_index
)
{
    AtmConversationSnapshotChart *chart =
        snapshot_chart_at (snapshot, message_index, chart_index);
    return chart != NULL && chart->series != NULL
        ? chart->series->len : 0;
}

#define SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR(name, field) \
const char * \
name ( \
    const AtmConversationSnapshot *snapshot, \
    guint message_index, \
    guint chart_index, \
    guint series_index \
) \
{ \
    AtmConversationSnapshotChartSeries *series = \
        snapshot_chart_series_at ( \
            snapshot, message_index, chart_index, series_index \
        ); \
    return series != NULL ? series->field : NULL; \
}

SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_profile_at,
    series_profile
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_admission_profile_at,
    admission_profile
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_scientific_id_at,
    scientific_id
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_qualified_id_at,
    qualified_id
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_repository_id_at,
    repository_id
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_repository_version_at,
    repository_version
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_snapshot_sha_at,
    snapshot_sha
)
SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR (
    atm_conversation_snapshot_chart_series_source_path_at,
    source_path
)

#undef SNAPSHOT_CHART_SERIES_TEXT_ACCESSOR

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
