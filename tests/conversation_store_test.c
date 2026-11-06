#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <sqlite3.h>

#include "conversation_store.h"

#include <sys/stat.h>
#include <unistd.h>

void atm_conversation_store_test_fail_v5_v6_after_statement (guint statement_number);
void atm_conversation_store_test_fail_v5_v6_after_sql_resource (void);
void atm_conversation_store_test_set_v5_v6_lock_hook (
    void (*hook) (gpointer user_data),
    gpointer user_data
);

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0) {
        return;
    }

    if (!S_ISDIR (st.st_mode) ||
        S_ISLNK (st.st_mode)) {
        g_remove (path);
        return;
    }

    GDir *directory = g_dir_open (
        path,
        0,
        NULL
    );

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (
                    directory
                )) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_rmdir (path);
}

static char *
new_temp_root (const char *pattern)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        pattern,
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static char *
store_path (const char *root)
{
    return g_build_filename (
        root,
        "conversations.sqlite3",
        NULL
    );
}

static gint64
raw_int64 (
    const char *path,
    const char *sql
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gint64 value = -1;

    g_assert_cmpint (
        sqlite3_open (
            path,
            &db
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_ROW
    );

    value = sqlite3_column_int64 (
        statement,
        0
    );

    sqlite3_finalize (statement);
    sqlite3_close (db);
    return value;
}

static char *
raw_text (
    const char *path,
    const char *sql
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;

    g_assert_cmpint (
        sqlite3_open (
            path,
            &db
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_ROW
    );

    const unsigned char *value =
        sqlite3_column_text (
            statement,
            0
        );
    char *copy = g_strdup (
        (const char *) value
    );

    sqlite3_finalize (statement);
    sqlite3_close (db);
    return copy;
}

static char *
raw_schema_snapshot (
    const char *path
)
{
    return raw_text (
        path,
        "SELECT group_concat(type||'|'||name||'|'||tbl_name||'|'||"
        "coalesce(sql,'<NULL>'),char(10)) FROM ("
        "SELECT type,name,tbl_name,sql FROM sqlite_schema "
        "ORDER BY type,name,tbl_name);"
    );
}

static void
append_raw_query_snapshot (
    sqlite3 *db,
    const char *sql,
    GString *snapshot
)
{
    sqlite3_stmt *statement = NULL;
    g_assert_cmpint (
        sqlite3_prepare_v2 (db, sql, -1, &statement, NULL),
        ==,
        SQLITE_OK
    );
    int rc;
    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        g_string_append_c (snapshot, '[');
        for (int column = 0;
             column < sqlite3_column_count (statement);
             column++) {
            int type = sqlite3_column_type (statement, column);
            g_string_append_printf (snapshot, "%d:", type);
            if (type == SQLITE_INTEGER) {
                g_string_append_printf (
                    snapshot,
                    "%" G_GINT64_FORMAT,
                    (gint64) sqlite3_column_int64 (statement, column)
                );
            } else if (type == SQLITE_TEXT || type == SQLITE_BLOB) {
                const void *value = type == SQLITE_TEXT
                    ? (const void *) sqlite3_column_text (statement, column)
                    : sqlite3_column_blob (statement, column);
                int length = sqlite3_column_bytes (statement, column);
                g_string_append_printf (snapshot, "%d:", length);
                g_string_append_len (snapshot, value, length);
            }
            g_string_append_c (snapshot, ';');
        }
        g_string_append_c (snapshot, ']');
    }
    g_assert_cmpint (rc, ==, SQLITE_DONE);
    sqlite3_finalize (statement);
}

static char *
raw_schema_structure_snapshot (
    const char *path
)
{
    static const char *tables[] = {
        "installation", "conversations", "conversation_repositories",
        "messages", "citations", "message_charts", "chart_series"
    };
    sqlite3 *db = NULL;
    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    GString *snapshot = g_string_new (NULL);
    append_raw_query_snapshot (
        db,
        "SELECT name,ncol,wr,strict FROM pragma_table_list "
        "WHERE schema='main' ORDER BY name;",
        snapshot
    );
    for (gsize i = 0; i < G_N_ELEMENTS (tables); i++) {
        char *quoted_table = sqlite3_mprintf ("%Q", tables[i]);
        char *sql = sqlite3_mprintf (
            "PRAGMA main.table_xinfo(%s);",
            quoted_table
        );
        append_raw_query_snapshot (db, sql, snapshot);
        sqlite3_free (sql);
        sql = sqlite3_mprintf (
            "PRAGMA main.foreign_key_list(%s);",
            quoted_table
        );
        append_raw_query_snapshot (db, sql, snapshot);
        sqlite3_free (sql);
        sql = sqlite3_mprintf (
            "SELECT seq,name,\"unique\",origin,partial "
            "FROM pragma_index_list(%s) ORDER BY seq;",
            quoted_table
        );
        sqlite3_free (quoted_table);

        sqlite3_stmt *indexes = NULL;
        g_assert_cmpint (
            sqlite3_prepare_v2 (db, sql, -1, &indexes, NULL),
            ==,
            SQLITE_OK
        );
        sqlite3_free (sql);
        int rc;
        while ((rc = sqlite3_step (indexes)) == SQLITE_ROW) {
            const char *index_name = (const char *) sqlite3_column_text (
                indexes,
                1
            );
            for (int column = 0;
                 column < sqlite3_column_count (indexes);
                 column++) {
                g_string_append_printf (
                    snapshot,
                    "%d:",
                    sqlite3_column_type (indexes, column)
                );
                const char *value = (const char *) sqlite3_column_text (
                    indexes,
                    column
                );
                if (value != NULL)
                    g_string_append (snapshot, value);
                g_string_append_c (snapshot, ';');
            }
            char *quoted_index = sqlite3_mprintf ("%Q", index_name);
            char *index_sql = sqlite3_mprintf (
                "PRAGMA main.index_xinfo(%s);",
                quoted_index
            );
            sqlite3_free (quoted_index);
            append_raw_query_snapshot (db, index_sql, snapshot);
            sqlite3_free (index_sql);
        }
        g_assert_cmpint (rc, ==, SQLITE_DONE);
        sqlite3_finalize (indexes);
    }
    sqlite3_close (db);
    return g_string_free (snapshot, FALSE);
}

static char *
raw_table_data_snapshot (
    const char *path
)
{
    static const char *data_queries[] = {
        "SELECT * FROM installation ORDER BY singleton_id;",
        "SELECT * FROM conversations ORDER BY conversation_id;",
        "SELECT * FROM conversation_repositories "
        "ORDER BY conversation_id,repository_id;",
        "SELECT * FROM messages ORDER BY message_id;",
        "SELECT * FROM citations ORDER BY message_id,ordinal;",
        "SELECT * FROM message_charts ORDER BY message_id,ordinal;",
        "SELECT * FROM chart_series "
        "ORDER BY message_id,chart_ordinal,series_ordinal;"
    };
    GString *snapshot = g_string_new (NULL);
    sqlite3 *db = NULL;
    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    for (gsize i = 0; i < G_N_ELEMENTS (data_queries); i++)
        append_raw_query_snapshot (db, data_queries[i], snapshot);
    sqlite3_close (db);
    return g_string_free (snapshot, FALSE);
}

static char *
raw_logical_state_snapshot (
    const char *path
)
{
    GString *snapshot = g_string_new (NULL);
    char *schema = raw_schema_snapshot (path);
    char *structure = raw_schema_structure_snapshot (path);
    char *data = raw_table_data_snapshot (path);
    g_string_append_printf (
        snapshot,
        "schema{%s}\nstructure{%s}\ndata{%s}\n",
        schema,
        structure,
        data
    );
    g_free (schema);
    g_free (structure);
    g_free (data);

    sqlite3 *db = NULL;
    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    append_raw_query_snapshot (db, "PRAGMA application_id;", snapshot);
    append_raw_query_snapshot (db, "PRAGMA user_version;", snapshot);
    append_raw_query_snapshot (db, "PRAGMA schema_version;", snapshot);
    append_raw_query_snapshot (db, "PRAGMA encoding;", snapshot);
    append_raw_query_snapshot (db, "PRAGMA integrity_check;", snapshot);
    append_raw_query_snapshot (db, "PRAGMA foreign_key_check;", snapshot);
    sqlite3_close (db);
    return g_string_free (snapshot, FALSE);
}

static char *
remove_quoted_identifier_delimiters (
    const char *sql
)
{
    GString *result = g_string_new (NULL);
    gboolean in_single_quoted_literal = FALSE;
    for (gsize i = 0; sql[i] != '\0'; i++) {
        char ch = sql[i];
        if (ch == '\'' && in_single_quoted_literal &&
            sql[i + 1] == '\'') {
            g_string_append_len (result, sql + i, 2);
            i++;
            continue;
        }
        if (ch == '\'')
            in_single_quoted_literal = !in_single_quoted_literal;
        if (ch != '"' || in_single_quoted_literal)
            g_string_append_c (result, ch);
    }
    return g_string_free (result, FALSE);
}

static void
assert_canonical_schema_definitions_equivalent (
    const char *direct_path,
    const char *migrated_path
)
{
    static const char *allowed_rewritten_tables[] = {
        "installation", "message_charts", "chart_series"
    };
    sqlite3 *direct = NULL;
    sqlite3 *migrated = NULL;
    g_assert_cmpint (
        sqlite3_open_v2 (
            direct_path,
            &direct,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_open_v2 (
            migrated_path,
            &migrated,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_stmt *object_count = NULL;
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            direct,
            "SELECT count(*) FROM sqlite_schema;",
            -1,
            &object_count,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (object_count), ==, SQLITE_ROW);
    int direct_object_count = sqlite3_column_int (object_count, 0);
    sqlite3_finalize (object_count);
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            migrated,
            "SELECT count(*) FROM sqlite_schema;",
            -1,
            &object_count,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (object_count), ==, SQLITE_ROW);
    g_assert_cmpint (
        sqlite3_column_int (object_count, 0),
        ==,
        direct_object_count
    );
    sqlite3_finalize (object_count);

    sqlite3_stmt *objects = NULL;
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            direct,
            "SELECT type,name,tbl_name,sql FROM sqlite_schema "
            "ORDER BY type,name,tbl_name;",
            -1,
            &objects,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    guint rewritten_tables_seen = 0;
    int rc;
    while ((rc = sqlite3_step (objects)) == SQLITE_ROW) {
        const char *type = (const char *) sqlite3_column_text (objects, 0);
        const char *name = (const char *) sqlite3_column_text (objects, 1);
        const char *table = (const char *) sqlite3_column_text (objects, 2);
        sqlite3_stmt *match = NULL;
        g_assert_cmpint (
            sqlite3_prepare_v2 (
                migrated,
                "SELECT tbl_name,sql FROM sqlite_schema "
                "WHERE type=?1 AND name=?2;",
                -1,
                &match,
                NULL
            ),
            ==,
            SQLITE_OK
        );
        sqlite3_bind_text (match, 1, type, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (match, 2, name, -1, SQLITE_TRANSIENT);
        g_assert_cmpint (sqlite3_step (match), ==, SQLITE_ROW);
        const char *other_table =
            (const char *) sqlite3_column_text (match, 0);
        const char *sql = (const char *) sqlite3_column_text (objects, 3);
        const char *other_sql = (const char *) sqlite3_column_text (match, 1);
        g_assert_cmpstr (table, ==, other_table);
        if (g_strcmp0 (sql, other_sql) != 0) {
            gboolean allowed = FALSE;
            if (g_str_equal (type, "table")) {
                for (gsize i = 0;
                     i < G_N_ELEMENTS (allowed_rewritten_tables);
                     i++) {
                    if (g_str_equal (name, allowed_rewritten_tables[i])) {
                        allowed = TRUE;
                        rewritten_tables_seen++;
                        break;
                    }
                }
            }
            g_assert_true (allowed);
            char *normalized_direct =
                remove_quoted_identifier_delimiters (sql);
            char *normalized_migrated =
                remove_quoted_identifier_delimiters (other_sql);
            g_assert_cmpstr (normalized_direct, ==, normalized_migrated);
            g_free (normalized_direct);
            g_free (normalized_migrated);
        }
        sqlite3_finalize (match);
    }
    g_assert_cmpint (rc, ==, SQLITE_DONE);
    g_assert_cmpuint (
        rewritten_tables_seen,
        ==,
        G_N_ELEMENTS (allowed_rewritten_tables)
    );
    sqlite3_finalize (objects);
    sqlite3_close (direct);
    sqlite3_close (migrated);
}

static void
raw_exec (
    const char *path,
    const char *sql
)
{
    sqlite3 *db = NULL;
    char *message = NULL;

    g_assert_cmpint (
        sqlite3_open (
            path,
            &db
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            sql,
            NULL,
            NULL,
            &message
        ),
        ==,
        SQLITE_OK
    );

    sqlite3_free (message);
    sqlite3_close (db);
}

static void
test_bootstrap_and_reopen (void)
{
    char *root = new_temp_root (
        "atm-conversation-store-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    gboolean opened = atm_conversation_store_open (
        path, &store, &error);
    if (!opened)
        g_printerr ("conversation-store bootstrap diagnostic: %s\\n",
                    error != NULL ? error->message : "no error detail");
    g_assert_true (opened);
    g_assert_no_error (error);
    g_assert_nonnull (store);
    g_assert_cmpint (
        atm_conversation_store_application_id (
            store
        ),
        ==,
        ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    g_assert_cmpint (
        atm_conversation_store_schema_version (
            store
        ),
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_VERSION
    );
    g_assert_true (
        atm_conversation_store_validate (
            store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    g_assert_cmpint (
        raw_int64 (
            path,
            "PRAGMA application_id;"
        ),
        ==,
        ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_VERSION
    );

    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation "
        "WHERE singleton_id=1;"
    );
    g_assert_cmpstr (
        schema_id,
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_ID
    );
    g_free (schema_id);

    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM sqlite_master "
            "WHERE type='table' AND name='message_charts';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM sqlite_master "
            "WHERE type='table' AND name='chart_series';"
        ),
        ==,
        1
    );

    char *journal = raw_text (
        path,
        "PRAGMA journal_mode;"
    );
    g_assert_cmpstr (
        journal,
        ==,
        "wal"
    );
    g_free (journal);

    store = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
create_v1_store (
    const char *path
)
{
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v1.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (bytes);

    gsize size = 0;
    const char *data = g_bytes_get_data (
        bytes,
        &size
    );
    g_assert_nonnull (data);
    g_assert_cmpuint (size, >, 0);

    char *schema = g_strndup (
        data,
        size
    );
    char *sql = g_strdup_printf (
        "BEGIN IMMEDIATE;%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=1;"
        "COMMIT;",
        schema,
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID
    );

    raw_exec (
        path,
        sql
    );

    g_free (sql);
    g_free (schema);
    g_bytes_unref (bytes);
}

static void
create_v2_store (
    const char *path
)
{
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v2.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (bytes);

    gsize size = 0;
    const char *data = g_bytes_get_data (bytes, &size);
    g_assert_nonnull (data);
    g_assert_cmpuint (size, >, 0);

    char *schema = g_strndup (data, size);
    char *sql = g_strdup_printf (
        "BEGIN IMMEDIATE;%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=2;"
        "COMMIT;",
        schema,
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    raw_exec (path, sql);

    g_free (sql);
    g_free (schema);
    g_bytes_unref (bytes);
}

static void
create_v3_store (
    const char *path
)
{
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v3.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (bytes);
    gsize size = 0;
    const char *data = g_bytes_get_data (bytes, &size);
    g_assert_nonnull (data);
    g_assert_cmpuint (size, >, 0);

    char *schema = g_strndup (data, size);
    char *sql = g_strdup_printf (
        "BEGIN IMMEDIATE;%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=3;"
        "COMMIT;",
        schema,
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    raw_exec (path, sql);
    g_free (sql);
    g_free (schema);
    g_bytes_unref (bytes);
}

static void
create_v4_store (
    const char *path
)
{
    create_v3_store (path);
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v3-to-v4.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    g_assert_no_error (error);
    g_assert_nonnull (bytes);
    gsize size = 0;
    const char *data = g_bytes_get_data (bytes, &size);
    g_assert_nonnull (data);
    char *migration = g_strndup (data, size);
    char *sql = g_strdup_printf (
        "BEGIN IMMEDIATE;%sPRAGMA user_version=4;COMMIT;", migration);
    raw_exec (path, sql);
    g_free (sql);
    g_free (migration);
    g_bytes_unref (bytes);
}

static void
create_v5_store (
    const char *path
)
{
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v5.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (bytes);
    gsize size = 0;
    const char *data = g_bytes_get_data (bytes, &size);
    char *schema = g_strndup (data, size);
    char *sql = g_strdup_printf (
        "BEGIN IMMEDIATE;%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=5;"
        "COMMIT;",
        schema,
        (guint) ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    raw_exec (path, sql);
    g_free (sql);
    g_free (schema);
    g_bytes_unref (bytes);
}

static void
create_v5_store_via_v4 (
    const char *path
)
{
    create_v4_store (path);
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v4-to-v5.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (bytes);
    gsize size = 0;
    const char *data = g_bytes_get_data (bytes, &size);
    char *migration = g_strndup (data, size);
    char *sql = g_strdup_printf (
        "PRAGMA foreign_keys=ON;BEGIN IMMEDIATE;%s"
        "PRAGMA user_version=5;COMMIT;",
        migration
    );
    raw_exec (path, sql);
    g_free (sql);
    g_free (migration);
    g_bytes_unref (bytes);
}

static void
insert_v3_gistemp_chart_fixture (
    const char *path
)
{
    raw_exec (
        path,
        "PRAGMA foreign_keys=ON;"
        "BEGIN IMMEDIATE;"
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,"
        "model_name,model_digest,repository_generation_id,archived,open_on_startup"
        ") VALUES("
        "'v3-chart-conv','V3 Chart',1,2,'model-a',NULL,7,1,0"
        ");"
        "INSERT INTO conversation_repositories("
        "conversation_id,repository_id,repository_version,snapshot_sha"
        ") VALUES("
        "'v3-chart-conv','ewd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567'"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'v3-chart-user','v3-chart-conv',0,0,'user','q','q',0,1"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'v3-chart-assistant','v3-chart-conv',1,0,'assistant','a','a',1,2"
        ");"
        "INSERT INTO message_charts("
        "message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile"
        ") VALUES("
        "'v3-chart-assistant',1,'atm-chart-spec/1',"
        "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',"
        "'line','atm-chart-reconstruct/gistemp-complete-annual/1'"
        ");"
        "INSERT INTO chart_series("
        "message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,"
        "scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path"
        ") VALUES("
        "'v3-chart-assistant',1,0,'atm-series/gistemp-complete-annual/1',"
        "'atm-gistemp-pinned-admission/1',"
        "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',"
        "'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',"
        "'ewd','0.1.0','0123456789abcdef0123456789abcdef01234567',"
        "'science/data/processed/nasa_gistemp_global_2026.csv'"
        ");"
        "COMMIT;"
    );
}

static void
insert_v5_chart_fixture (
    const char *path,
    const char *conversation_id,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path
)
{
    char *sql = g_strdup_printf (
        "BEGIN IMMEDIATE;"
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,model_name,"
        "model_digest,repository_generation_id,archived,open_on_startup"
        ") VALUES('%s','Fixture',1,2,'model-a',NULL,7,1,0);"
        "INSERT INTO conversation_repositories("
        "conversation_id,repository_id,repository_version,snapshot_sha"
        ") VALUES('%s','%s','%s','%s');"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES('%s-user','%s',0,0,'user','q','q',0,1);"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES('%s-assistant','%s',1,0,'assistant','a','a',1,2);"
        "INSERT INTO message_charts("
        "message_id,ordinal,chart_schema,chart_spec_id,chart_kind,"
        "reconstruction_profile"
        ") VALUES('%s-assistant',1,'%s','%s','line','%s');"
        "INSERT INTO chart_series("
        "message_id,chart_ordinal,series_ordinal,series_profile,"
        "admission_profile,scientific_id,qualified_id,repository_id,"
        "repository_version,snapshot_sha,source_path"
        ") VALUES('%s-assistant',1,0,'%s','%s',"
        "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',"
        "'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',"
        "'%s','%s','%s','%s');"
        "COMMIT;",
        conversation_id,
        conversation_id,
        repository_id,
        repository_version,
        snapshot_sha,
        conversation_id,
        conversation_id,
        conversation_id,
        conversation_id,
        conversation_id,
        chart_schema,
        chart_spec_id,
        reconstruction_profile,
        conversation_id,
        series_profile,
        admission_profile,
        repository_id,
        repository_version,
        snapshot_sha,
        source_path
    );
    raw_exec (path, sql);
    g_free (sql);
}

static void
insert_v5_citation_fixture (
    const char *path,
    const char *message_id,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path
)
{
    char *sql = g_strdup_printf (
        "INSERT INTO citations("
        "message_id,ordinal,label,repository_id,repository_version,snapshot_sha,"
        "logical_source_id,source_path,locator,title,excerpt,immutable_permalink"
        ") VALUES('%s',1,'S1','%s','%s','%s','source-1','%s','row:1',"
        "'Fixture source','Fixture excerpt','https://example.invalid/source');",
        message_id,
        repository_id,
        repository_version,
        snapshot_sha,
        source_path
    );
    raw_exec (path, sql);
    g_free (sql);
}

static void
populate_v5_representative_data (
    const char *path
)
{
    insert_v3_gistemp_chart_fixture (path);
    insert_v5_citation_fixture (
        path,
        "v3-chart-assistant",
        "ewd",
        "0.1.0",
        "0123456789abcdef0123456789abcdef01234567",
        "science/data/processed/nasa_gistemp_global_2026.csv"
    );
    insert_v5_chart_fixture (
        path,
        "v5-energy-conv",
        "atm-chart-spec/2",
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
        "atm-chart-reconstruct/energy-institute-tes-complete-annual/1",
        "atm-series/energy-institute-tes-complete-annual/1",
        "atm-energy-institute-pinned-admission/3",
        "ewd",
        "snapshot:d9e249339663015f6d1c05752338a955bf64ad0b",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "science/data/processed/energy_institute_global_2026.csv"
    );
    insert_v5_citation_fixture (
        path,
        "v5-energy-conv-assistant",
        "ewd",
        "snapshot:d9e249339663015f6d1c05752338a955bf64ad0b",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "science/data/processed/energy_institute_global_2026.csv"
    );
    insert_v5_chart_fixture (
        path,
        "v5-bnr-conv",
        "atm-chart-spec/3",
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
        "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        "atm-series/bnr-bls-c01-calendar-quarter/1",
        "atm-bnr-bls-canonical-panel-admission/1",
        "rmd",
        "snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170",
        "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170",
        "data/processed/bnr_bls_realised_rounds.csv"
    );
    insert_v5_citation_fixture (
        path,
        "v5-bnr-conv-assistant",
        "rmd",
        "snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170",
        "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170",
        "data/processed/bnr_bls_realised_rounds.csv"
    );
}

static void
test_v3_to_v5_migration_preserves_chart_rows (void)
{
    char *root = new_temp_root ("atm-conversation-v3-v4-XXXXXX");
    char *path = store_path (root);
    create_v3_store (path);
    insert_v3_gistemp_chart_fixture (path);

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_cmpint (atm_conversation_store_schema_version (store), ==, ATM_CONVERSATION_STORE_SCHEMA_VERSION);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, ATM_CONVERSATION_STORE_SCHEMA_VERSION);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM message_charts WHERE chart_schema='atm-chart-spec/1' "
        "AND chart_spec_id='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' "
        "AND reconstruction_profile='atm-chart-reconstruct/gistemp-complete-annual/1';"), ==, 1);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM chart_series WHERE series_profile='atm-series/gistemp-complete-annual/1' "
        "AND admission_profile='atm-gistemp-pinned-admission/1' "
        "AND scientific_id='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' "
        "AND qualified_id='cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc';"), ==, 1);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM pragma_foreign_key_check;"), ==, 0);

    char *schema_id = raw_text (path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;");
    g_assert_cmpstr (schema_id, ==, ATM_CONVERSATION_STORE_SCHEMA_ID);
    g_free (schema_id);
    atm_conversation_store_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v3_to_v4_migration_rollback (void)
{
    char *root = new_temp_root ("atm-conversation-v3-v4-rollback-XXXXXX");
    char *path = store_path (root);
    create_v3_store (path);
    insert_v3_gistemp_chart_fixture (path);
    /* Force failure late: this trigger name is created only after the migration
     * has copied the chart rows, dropped the old chart tables, renamed the new
     * tables and recreated the index. Because this trigger belongs to messages,
     * dropping chart_series cannot remove the collision. The transaction must
     * restore the original v3 tables and rows. */
    raw_exec (path,
        "CREATE TRIGGER chart_series_tuple_insert "
        "BEFORE INSERT ON messages BEGIN SELECT 1; END;"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_nonnull (error);
    g_assert_null (store);
    g_assert_nonnull (g_strstr_len (error->message, -1,
        "chart_series_tuple_insert"));
    g_clear_error (&error);

    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 3);
    char *schema_id = raw_text (path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;");
    g_assert_cmpstr (schema_id, ==, "atm-conversation-store/3");
    g_free (schema_id);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM message_charts;"), ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM chart_series;"), ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM pragma_foreign_key_check;"), ==, 0);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM sqlite_schema WHERE type='trigger' "
        "AND name='chart_series_tuple_insert' AND tbl_name='messages';"), ==, 1);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v4_to_v5_migration_preserves_chart_rows (void)
{
    char *root = new_temp_root ("atm-conversation-v4-v5-XXXXXX");
    char *path = store_path (root);
    create_v4_store (path);
    insert_v3_gistemp_chart_fixture (path);

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_nonnull (store);
    g_assert_cmpint (atm_conversation_store_schema_version (store), ==,
                     ATM_CONVERSATION_STORE_SCHEMA_VERSION);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==,
                     ATM_CONVERSATION_STORE_SCHEMA_VERSION);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM message_charts WHERE chart_schema='atm-chart-spec/1' "
        "AND reconstruction_profile='atm-chart-reconstruct/gistemp-complete-annual/1';"), ==, 1);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM chart_series WHERE series_profile='atm-series/gistemp-complete-annual/1';"),
        ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM pragma_foreign_key_check;"), ==, 0);
    char *schema_id = raw_text (path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;");
    g_assert_cmpstr (schema_id, ==, ATM_CONVERSATION_STORE_SCHEMA_ID);
    g_free (schema_id);

    atm_conversation_store_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v4_to_v5_migration_rollback (void)
{
    char *root = new_temp_root ("atm-conversation-v4-v5-rollback-XXXXXX");
    char *path = store_path (root);
    create_v4_store (path);
    insert_v3_gistemp_chart_fixture (path);
    raw_exec (path,
        "DROP TRIGGER chart_series_tuple_insert;"
        "CREATE TRIGGER chart_series_tuple_insert "
        "BEFORE INSERT ON messages BEGIN SELECT 1; END;");

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_nonnull (error);
    g_assert_null (store);
    g_assert_nonnull (g_strstr_len (error->message, -1,
        "chart_series_tuple_insert"));
    g_clear_error (&error);

    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 4);
    char *schema_id = raw_text (path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;");
    g_assert_cmpstr (schema_id, ==, "atm-conversation-store/4");
    g_free (schema_id);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM message_charts;"), ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM chart_series;"), ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM pragma_foreign_key_check;"), ==, 0);
    g_assert_cmpint (raw_int64 (path,
        "SELECT count(*) FROM sqlite_schema WHERE type='trigger' "
        "AND name='chart_series_tuple_insert' AND tbl_name='messages';"), ==, 1);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v5_direct_to_v6_migration (void)
{
    char *root = new_temp_root ("atm-conversation-v5-direct-XXXXXX");
    char *path = store_path (root);
    create_v5_store (path);
    insert_v3_gistemp_chart_fixture (path);
    raw_exec (
        path,
        "INSERT INTO citations("
        "message_id,ordinal,label,repository_id,repository_version,snapshot_sha,"
        "logical_source_id,source_path,locator,title,excerpt,immutable_permalink"
        ") VALUES("
        "'v3-chart-assistant',1,'citation 1','ewd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567','source-1',"
        "'science/data/processed/nasa_gistemp_global_2026.csv','row:1',"
        "'title','excerpt','https://example.invalid/source');"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_cmpint (
        atm_conversation_store_schema_version (store),
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_VERSION
    );
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 6);
    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;"
    );
    g_assert_cmpstr (schema_id, ==, ATM_CONVERSATION_STORE_SCHEMA_ID);
    g_free (schema_id);
    g_assert_cmpint (
        raw_int64 (path, "SELECT count(*) FROM pragma_foreign_key_check;"),
        ==,
        0
    );
    char *integrity = raw_text (path, "PRAGMA integrity_check;");
    g_assert_cmpstr (integrity, ==, "ok");
    g_free (integrity);
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM messages WHERE message_id IN "
            "('v3-chart-user','v3-chart-assistant');"
        ),
        ==,
        2
    );
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM citations;"), ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM message_charts;"), ==, 1);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM chart_series;"), ==, 1);
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM message_charts "
            "WHERE presentation_schema IS NULL;"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_int64 (path, "SELECT count(*) FROM chart_presentations_v4;"),
        ==,
        0
    );
    char *schema_after_migration = raw_schema_snapshot (path);
    atm_conversation_store_close (store);

    store = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    char *schema_after_reopen = raw_schema_snapshot (path);
    g_assert_cmpstr (schema_after_reopen, ==, schema_after_migration);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 6);
    g_free (schema_after_migration);
    g_free (schema_after_reopen);
    atm_conversation_store_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v5_chart_v1_v2_v3_rows_migrate (void)
{
    char *root = new_temp_root ("atm-conversation-v5-chart-versions-XXXXXX");
    char *path = store_path (root);
    create_v5_store (path);
    insert_v3_gistemp_chart_fixture (path);
    insert_v5_chart_fixture (
        path,
        "v5-energy-conv",
        "atm-chart-spec/2",
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
        "atm-chart-reconstruct/energy-institute-tes-complete-annual/1",
        "atm-series/energy-institute-tes-complete-annual/1",
        "atm-energy-institute-pinned-admission/3",
        "ewd",
        "snapshot:d9e249339663015f6d1c05752338a955bf64ad0b",
        "d9e249339663015f6d1c05752338a955bf64ad0b",
        "science/data/processed/energy_institute_global_2026.csv"
    );
    insert_v5_chart_fixture (
        path,
        "v5-bnr-conv",
        "atm-chart-spec/3",
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
        "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        "atm-series/bnr-bls-c01-calendar-quarter/1",
        "atm-bnr-bls-canonical-panel-admission/1",
        "rmd",
        "snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170",
        "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170",
        "data/processed/bnr_bls_realised_rounds.csv"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    for (guint version = 1; version <= 3; version++) {
        char *sql = g_strdup_printf (
            "SELECT count(*) FROM message_charts "
            "WHERE chart_schema='atm-chart-spec/%u';",
            version
        );
        g_assert_cmpint (raw_int64 (path, sql), ==, 1);
        g_free (sql);
    }
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM chart_series;"), ==, 3);
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM conversation_repositories "
            "WHERE snapshot_sha='0123456789abcdef0123456789abcdef01234567';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM chart_series "
            "WHERE snapshot_sha='0123456789abcdef0123456789abcdef01234567';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_int64 (path, "SELECT count(*) FROM conversation_repositories;"),
        ==,
        3
    );
    g_assert_cmpint (
        raw_int64 (path, "SELECT count(*) FROM chart_presentations_v4;"),
        ==,
        0
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM message_charts "
            "WHERE presentation_schema IS NOT NULL;"
        ),
        ==,
        0
    );
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM pragma_foreign_key_check;"), ==, 0);
    atm_conversation_store_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v5_via_v4_to_v6_migration (void)
{
    char *root = new_temp_root ("atm-conversation-v5-via-v4-XXXXXX");
    char *path = store_path (root);
    create_v5_store_via_v4 (path);

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 6);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    atm_conversation_store_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v5_canonical_fixture_forms (void)
{
    char *direct_root = new_temp_root ("atm-conversation-v5-direct-form-XXXXXX");
    char *migrated_root = new_temp_root ("atm-conversation-v5-migrated-form-XXXXXX");
    char *direct_path = store_path (direct_root);
    char *migrated_path = store_path (migrated_root);
    create_v5_store (direct_path);
    create_v5_store_via_v4 (migrated_path);
    populate_v5_representative_data (direct_path);
    populate_v5_representative_data (migrated_path);

    char *direct_structure = raw_schema_structure_snapshot (direct_path);
    char *migrated_structure = raw_schema_structure_snapshot (migrated_path);
    g_assert_cmpstr (direct_structure, ==, migrated_structure);
    g_free (direct_structure);
    g_free (migrated_structure);
    assert_canonical_schema_definitions_equivalent (
        direct_path,
        migrated_path
    );
    /* ALTER TABLE rewrites only the three audited tables' identifier quoting. */
    char *direct_data = raw_table_data_snapshot (direct_path);
    char *migrated_data = raw_table_data_snapshot (migrated_path);
    g_assert_cmpstr (direct_data, ==, migrated_data);
    g_free (direct_data);
    g_free (migrated_data);
    g_assert_cmpint (
        raw_int64 (direct_path, "PRAGMA application_id;"),
        ==,
        raw_int64 (migrated_path, "PRAGMA application_id;")
    );
    g_assert_cmpint (
        raw_int64 (direct_path, "PRAGMA user_version;"),
        ==,
        raw_int64 (migrated_path, "PRAGMA user_version;")
    );
    char *direct_encoding = raw_text (direct_path, "PRAGMA encoding;");
    char *migrated_encoding = raw_text (migrated_path, "PRAGMA encoding;");
    g_assert_cmpstr (direct_encoding, ==, migrated_encoding);
    g_free (direct_encoding);
    g_free (migrated_encoding);

    g_free (direct_path);
    g_free (migrated_path);
    remove_tree_best_effort (direct_root);
    remove_tree_best_effort (migrated_root);
    g_free (direct_root);
    g_free (migrated_root);
}

static void
test_v5_canonical_ddl_resource_hashes (void)
{
    /* Resource edits must not silently broaden the accepted v5 contract. */
    static const struct {
        const char *resource;
        const char *sha256;
    } canonical_resources[] = {
        {
            "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v5.sql",
            "f66ef43bf0d44707fdcfcdf8ca1bead35f1b4a14554f008fed0505864e8c13d7"
        },
        {
            "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v4.sql",
            "eda8993db0184ba35f8bf0320e7220eb51eb78e4c99db03ae872a7ea5fb57a75"
        },
        {
            "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v4-to-v5.sql",
            "ec955b45e8b0b47d2d0c4380a2e64a049400ecb7394374e2b7aa93fea70ac033"
        }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (canonical_resources); i++) {
        GError *error = NULL;
        GBytes *bytes = g_resources_lookup_data (
            canonical_resources[i].resource,
            G_RESOURCE_LOOKUP_FLAGS_NONE,
            &error
        );
        g_assert_no_error (error);
        g_assert_nonnull (bytes);
        gsize size = 0;
        const guchar *data = g_bytes_get_data (bytes, &size);
        char *digest = g_compute_checksum_for_data (
            G_CHECKSUM_SHA256,
            data,
            size
        );
        g_assert_cmpstr (digest, ==, canonical_resources[i].sha256);
        g_free (digest);
        g_bytes_unref (bytes);
    }
}

static void
test_v5_auxiliary_index_rejected_without_changes (void)
{
    char *root = new_temp_root ("atm-conversation-v5-aux-index-XXXXXX");
    char *path = store_path (root);
    create_v5_store (path);
    raw_exec (path, "CREATE INDEX user_extra_message_role ON messages(role);");
    char *before = raw_schema_snapshot (path);

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_null (store);
    g_assert_nonnull (error);
    g_assert_nonnull (g_strstr_len (
        error->message,
        -1,
        "object_name=user_extra_message_role"
    ));
    g_clear_error (&error);

    char *after = raw_schema_snapshot (path);
    g_assert_cmpstr (after, ==, before);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 5);
    g_free (before);
    g_free (after);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
assert_v5_ddl_rejected_without_changes (
    const char *test_name,
    const char *ddl,
    const char *expected_object
)
{
    char *root = new_temp_root (test_name);
    char *path = store_path (root);
    create_v5_store (path);
    raw_exec (path, ddl);
    char *before = raw_schema_snapshot (path);
    gint64 schema_version_before = raw_int64 (path, "PRAGMA schema_version;");
    gint64 data_rows_before = raw_int64 (
        path,
        "SELECT (SELECT count(*) FROM conversations)+"
        "(SELECT count(*) FROM conversation_repositories)+"
        "(SELECT count(*) FROM messages)+(SELECT count(*) FROM citations)+"
        "(SELECT count(*) FROM message_charts)+(SELECT count(*) FROM chart_series);"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_null (store);
    g_assert_nonnull (error);
    if (expected_object != NULL)
        g_assert_nonnull (g_strstr_len (error->message, -1, expected_object));
    g_clear_error (&error);

    char *after = raw_schema_snapshot (path);
    g_assert_cmpstr (after, ==, before);
    g_assert_cmpint (
        raw_int64 (path, "PRAGMA application_id;"),
        ==,
        ATM_CONVERSATION_STORE_APPLICATION_ID
    );
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 5);
    g_assert_cmpint (
        raw_int64 (path, "PRAGMA schema_version;"),
        ==,
        schema_version_before
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT (SELECT count(*) FROM conversations)+"
            "(SELECT count(*) FROM conversation_repositories)+"
            "(SELECT count(*) FROM messages)+(SELECT count(*) FROM citations)+"
            "(SELECT count(*) FROM message_charts)+(SELECT count(*) FROM chart_series);"
        ),
        ==,
        data_rows_before
    );
    g_free (before);
    g_free (after);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v5_auxiliary_objects_rejected_without_changes (void)
{
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-unique-index-XXXXXX",
        "CREATE UNIQUE INDEX user_unique_role ON messages(role);",
        "object_name=user_unique_role"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-partial-index-XXXXXX",
        "CREATE INDEX user_partial_role ON messages(role) WHERE role='user';",
        "object_name=user_partial_role"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-expression-index-XXXXXX",
        "CREATE INDEX user_expression_role ON messages(lower(role));",
        "object_name=user_expression_role"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-trigger-XXXXXX",
        "CREATE TRIGGER user_aux_trigger AFTER INSERT ON messages "
        "BEGIN SELECT 1; END;",
        "object_name=user_aux_trigger"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-view-XXXXXX",
        "CREATE VIEW user_aux_view AS "
        "SELECT conversation_id FROM conversations;",
        "object_name=user_aux_view"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-aux-table-XXXXXX",
        "CREATE TABLE user_aux_table (conversation_id TEXT);",
        "object_name=user_aux_table"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-external-fk-XXXXXX",
        "CREATE TABLE user_aux_chart_ref (message_id TEXT, ordinal INTEGER, "
        "FOREIGN KEY(message_id,ordinal) "
        "REFERENCES message_charts(message_id,ordinal));",
        "object_name=user_aux_chart_ref"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-extra-column-XXXXXX",
        "ALTER TABLE messages ADD COLUMN unexpected TEXT;",
        "object_name=messages"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-missing-index-XXXXXX",
        "DROP INDEX idx_messages_conversation_sequence;",
        "object_name=idx_messages_conversation_sequence"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-changed-index-XXXXXX",
        "DROP INDEX idx_messages_conversation_sequence;"
        "CREATE INDEX idx_messages_conversation_sequence "
        "ON messages(role);",
        "object_name=idx_messages_conversation_sequence"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-changed-trigger-XXXXXX",
        "DROP TRIGGER chart_series_tuple_update;"
        "CREATE TRIGGER chart_series_tuple_update "
        "BEFORE UPDATE ON chart_series BEGIN SELECT 1; END;",
        "object_name=chart_series_tuple_update"
    );
    assert_v5_ddl_rejected_without_changes (
        "atm-v5-sqlite-stat-XXXXXX",
        "ANALYZE;",
        "object_name=sqlite_stat1"
    );
}

static void
test_v5_to_v6_rollback_after_ddl (void)
{
    gboolean completed_after_last_statement = FALSE;
    guint injected_failures = 0;
    for (guint statement_number = 1;
         statement_number < 128;
         statement_number++) {
        char *root = new_temp_root ("atm-conversation-v5-rollback-XXXXXX");
        char *path = store_path (root);
        create_v5_store (path);
        populate_v5_representative_data (path);
        char *before = raw_logical_state_snapshot (path);
        gint64 schema_version_before = raw_int64 (
            path,
            "PRAGMA schema_version;"
        );

        atm_conversation_store_test_fail_v5_v6_after_statement (
            statement_number
        );
        AtmConversationStore *store = NULL;
        GError *error = NULL;
        gboolean opened = atm_conversation_store_open (path, &store, &error);
        if (opened) {
            g_assert_no_error (error);
            g_assert_nonnull (store);
            g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 6);
            atm_conversation_store_close (store);
            completed_after_last_statement = TRUE;
        } else {
            injected_failures++;
            g_assert_null (store);
            g_assert_nonnull (error);
            g_assert_nonnull (g_strstr_len (error->message, -1, "Injected"));
            char *after = raw_logical_state_snapshot (path);
            g_assert_cmpstr (after, ==, before);
            g_free (after);
            g_assert_cmpint (
                raw_int64 (path, "PRAGMA schema_version;"),
                ==,
                schema_version_before
            );
        }
        g_clear_error (&error);
        g_free (before);
        g_free (path);
        remove_tree_best_effort (root);
        g_free (root);
        if (completed_after_last_statement)
            break;
    }
    atm_conversation_store_test_fail_v5_v6_after_statement (0);
    g_assert_true (completed_after_last_statement);
    g_assert_cmpuint (injected_failures, >, 1);
}

static void
test_v5_to_v6_rollback_after_sql_resource (void)
{
    char *root = new_temp_root ("atm-conversation-v5-resource-rollback-XXXXXX");
    char *path = store_path (root);
    create_v5_store (path);
    populate_v5_representative_data (path);
    char *before = raw_logical_state_snapshot (path);

    atm_conversation_store_test_fail_v5_v6_after_sql_resource ();
    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_null (store);
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_SQLITE
    );
    g_assert_nonnull (g_strstr_len (
        error->message,
        -1,
        "after SQL resource execution"
    ));
    g_clear_error (&error);

    char *after = raw_logical_state_snapshot (path);
    g_assert_cmpstr (after, ==, before);
    g_free (after);
    g_free (before);
    g_assert_cmpint (raw_int64 (path, "PRAGMA application_id;"), ==,
                     ATM_CONVERSATION_STORE_APPLICATION_ID);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 5);
    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;"
    );
    g_assert_cmpstr (schema_id, ==, "atm-conversation-store/5");
    g_free (schema_id);

    sqlite3 *db = NULL;
    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_stmt *statement = NULL;
    g_assert_cmpint (
        sqlite3_prepare_v2 (db, "PRAGMA integrity_check;", -1, &statement, NULL),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    g_assert_cmpstr ((const char *) sqlite3_column_text (statement, 0), ==, "ok");
    sqlite3_finalize (statement);
    g_assert_cmpint (
        sqlite3_prepare_v2 (db, "PRAGMA foreign_key_check;", -1, &statement, NULL),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_DONE);
    sqlite3_finalize (statement);
    sqlite3_close (db);

    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_nonnull (store);
    g_assert_cmpint (atm_conversation_store_schema_version (store), ==, 6);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

typedef struct {
    const char *path;
    GMutex mutex;
    GCond condition;
    gboolean lock_acquired;
    gboolean release_migration;
    gboolean opened;
    AtmConversationStore *store;
    GError *error;
} V5MigrationWithLock;

static void
v5_migration_lock_hook (
    gpointer data
)
{
    V5MigrationWithLock *migration = data;
    g_mutex_lock (&migration->mutex);
    migration->lock_acquired = TRUE;
    g_cond_broadcast (&migration->condition);
    while (!migration->release_migration)
        g_cond_wait (&migration->condition, &migration->mutex);
    g_mutex_unlock (&migration->mutex);
}

static gpointer
v5_migration_thread (
    gpointer data
)
{
    V5MigrationWithLock *migration = data;
    migration->opened = atm_conversation_store_open (
        migration->path,
        &migration->store,
        &migration->error
    );
    return NULL;
}

static void
test_v5_preflight_serializes_with_concurrent_schema_change (void)
{
    char *root = new_temp_root ("atm-conversation-v5-concurrent-XXXXXX");
    char *path = store_path (root);
    create_v5_store (path);
    raw_exec (path, "PRAGMA journal_mode=WAL;");

    V5MigrationWithLock migration = { .path = path };
    g_mutex_init (&migration.mutex);
    g_cond_init (&migration.condition);
    atm_conversation_store_test_set_v5_v6_lock_hook (
        v5_migration_lock_hook,
        &migration
    );
    GThread *thread = g_thread_new (
        "v5-migration",
        v5_migration_thread,
        &migration
    );

    g_mutex_lock (&migration.mutex);
    while (!migration.lock_acquired)
        g_cond_wait (&migration.condition, &migration.mutex);
    g_mutex_unlock (&migration.mutex);

    sqlite3 *writer = NULL;
    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &writer,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_busy_timeout (writer, 0);
    char *message = NULL;
    int rc = sqlite3_exec (
        writer,
        "BEGIN IMMEDIATE;"
        "CREATE INDEX concurrent_aux_index ON messages(role);"
        "COMMIT;",
        NULL,
        NULL,
        &message
    );
    g_assert_cmpint (rc, ==, SQLITE_BUSY);
    sqlite3_free (message);
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM sqlite_schema "
            "WHERE type='index' AND name='concurrent_aux_index';"
        ),
        ==,
        0
    );
    sqlite3_close (writer);

    g_mutex_lock (&migration.mutex);
    migration.release_migration = TRUE;
    g_cond_broadcast (&migration.condition);
    g_mutex_unlock (&migration.mutex);
    g_thread_join (thread);
    atm_conversation_store_test_set_v5_v6_lock_hook (NULL, NULL);
    g_assert_true (migration.opened);
    g_assert_no_error (migration.error);
    g_assert_nonnull (migration.store);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 6);
    atm_conversation_store_close (migration.store);
    g_clear_error (&migration.error);

    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &writer,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            writer,
            "BEGIN IMMEDIATE;"
            "CREATE INDEX concurrent_aux_index ON messages(role);"
            "COMMIT;",
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_close (writer);

    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM sqlite_schema "
            "WHERE type='index' AND name='concurrent_aux_index';"
        ),
        ==,
        1
    );
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 6);
    g_mutex_clear (&migration.mutex);
    g_cond_clear (&migration.condition);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v1_to_v5_migration (void)
{
    char *root = new_temp_root (
        "atm-conversation-migration-XXXXXX"
    );
    char *path = store_path (root);

    create_v1_store (
        path
    );

    raw_exec (
        path,
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,"
        "model_name,model_digest,repository_generation_id,archived"
        ") VALUES("
        "'legacy','Legacy',1,1,'model-a',NULL,0,0"
        ");"
    );

    gint64 *generation_ids = NULL;
    gsize generation_count = 0;
    GError *readonly_error = NULL;

    g_assert_false (
        atm_conversation_store_list_repository_generation_ids_readonly (
            path,
            &generation_ids,
            &generation_count,
            &readonly_error
        )
    );
    g_assert_error (
        readonly_error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_SCHEMA
    );
    g_assert_null (generation_ids);
    g_assert_cmpuint (
        generation_count,
        ==,
        0
    );
    g_clear_error (&readonly_error);
    g_assert_cmpint (
        raw_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        1
    );
    char *readonly_schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation "
        "WHERE singleton_id=1;"
    );
    g_assert_cmpstr (
        readonly_schema_id,
        ==,
        "atm-conversation-store/1"
    );
    g_free (readonly_schema_id);

    AtmConversationStore *store = NULL;
    GError *error = NULL;

    gboolean opened = atm_conversation_store_open (path, &store, &error);
    if (!opened)
        g_printerr ("v1-to-v6 migration diagnostic: %s\n",
                    error != NULL ? error->message : "no error detail");
    g_assert_true (opened);
    g_assert_no_error (error);
    g_assert_nonnull (store);

    g_assert_cmpint (
        raw_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_VERSION
    );

    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation "
        "WHERE singleton_id=1;"
    );
    g_assert_cmpstr (
        schema_id,
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_ID
    );
    g_free (schema_id);

    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT open_on_startup FROM conversations "
            "WHERE conversation_id='legacy';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_int64 (path, "SELECT count(*) FROM message_charts;"),
        ==,
        0
    );
    g_assert_cmpint (
        raw_int64 (path, "SELECT count(*) FROM chart_series;"),
        ==,
        0
    );

    g_assert_true (
        atm_conversation_store_validate (
            store,
            &error
        )
    );
    g_assert_no_error (error);

    atm_conversation_store_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v1_to_v3_migration_rollback (void)
{
    char *root = new_temp_root ("atm-conversation-v1-v3-rollback-XXXXXX");
    char *path = store_path (root);
    create_v1_store (path);

    raw_exec (
        path,
        "CREATE TABLE message_charts(dummy INTEGER) STRICT;"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_nonnull (error);
    g_assert_null (store);
    g_clear_error (&error);

    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 1);
    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;"
    );
    g_assert_cmpstr (schema_id, ==, "atm-conversation-store/1");
    g_free (schema_id);
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM pragma_table_info('conversations') "
            "WHERE name='open_on_startup';"
        ),
        ==,
        0
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM sqlite_master "
            "WHERE type='table' AND name='chart_series';"
        ),
        ==,
        0
    );

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v2_to_v5_migration (void)
{
    char *root = new_temp_root ("atm-conversation-v2-v3-XXXXXX");
    char *path = store_path (root);
    create_v2_store (path);

    raw_exec (
        path,
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,"
        "model_name,model_digest,repository_generation_id,archived,open_on_startup"
        ") VALUES("
        "'legacy-v2','Legacy v2',1,2,'model-a',NULL,0,1,0"
        ");"
    );

    gint64 *generation_ids = NULL;
    gsize generation_count = 0;
    GError *error = NULL;
    g_assert_false (
        atm_conversation_store_list_repository_generation_ids_readonly (
            path, &generation_ids, &generation_count, &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_SCHEMA
    );
    g_clear_error (&error);
    g_assert_null (generation_ids);
    g_assert_cmpuint (generation_count, ==, 0);
    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 2);

    AtmConversationStore *store = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_cmpint (
        atm_conversation_store_schema_version (store),
        ==,
        ATM_CONVERSATION_STORE_SCHEMA_VERSION
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT open_on_startup FROM conversations "
            "WHERE conversation_id='legacy-v2';"
        ),
        ==,
        0
    );
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM message_charts;"), ==, 0);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM chart_series;"), ==, 0);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;"
    );
    g_assert_cmpstr (schema_id, ==, ATM_CONVERSATION_STORE_SCHEMA_ID);
    g_free (schema_id);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v2_to_v3_migration_rollback (void)
{
    char *root = new_temp_root ("atm-conversation-v2-v3-rollback-XXXXXX");
    char *path = store_path (root);
    create_v2_store (path);

    raw_exec (
        path,
        "CREATE TABLE message_charts(dummy INTEGER) STRICT;"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_nonnull (error);
    g_assert_null (store);
    g_clear_error (&error);

    g_assert_cmpint (raw_int64 (path, "PRAGMA user_version;"), ==, 2);
    char *schema_id = raw_text (
        path,
        "SELECT schema_id FROM installation WHERE singleton_id=1;"
    );
    g_assert_cmpstr (schema_id, ==, "atm-conversation-store/2");
    g_free (schema_id);
    g_assert_cmpint (
        raw_int64 (
            path,
            "SELECT count(*) FROM sqlite_master "
            "WHERE type='table' AND name='chart_series';"
        ),
        ==,
        0
    );

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_chart_recipe_semantics (void)
{
    char *root = new_temp_root ("atm-conversation-chart-recipe-XXXXXX");
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    raw_exec (
        path,
        "PRAGMA foreign_keys=ON;"
        "BEGIN IMMEDIATE;"
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,"
        "model_name,model_digest,repository_generation_id,archived,open_on_startup"
        ") VALUES("
        "'chart-conv','Chart',1,2,'model-a',NULL,7,1,0"
        ");"
        "INSERT INTO conversation_repositories("
        "conversation_id,repository_id,repository_version,snapshot_sha"
        ") VALUES("
        "'chart-conv','ewd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567'"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'chart-user','chart-conv',0,0,'user','q','q',0,1"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'chart-assistant','chart-conv',1,0,'assistant','a','a',1,2"
        ");"
        "INSERT INTO message_charts("
        "message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile"
        ") VALUES("
        "'chart-assistant',1,'atm-chart-spec/1',"
        "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',"
        "'line','atm-chart-reconstruct/gistemp-complete-annual/1'"
        ");"
        "INSERT INTO chart_series("
        "message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,"
        "scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path"
        ") VALUES("
        "'chart-assistant',1,0,'atm-series/gistemp-complete-annual/1',"
        "'atm-gistemp-pinned-admission/1',"
        "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',"
        "'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',"
        "'ewd','0.1.0','0123456789abcdef0123456789abcdef01234567',"
        "'science/data/processed/nasa_gistemp_global_2026.csv'"
        ");"
        "COMMIT;"
    );

    store = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    raw_exec (
        path,
        "PRAGMA ignore_check_constraints=ON;"
        "UPDATE chart_series SET source_path='README.md';"
    );

    store = NULL;
    g_assert_false (atm_conversation_store_open (path, &store, &error));
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_INTEGRITY
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_foreign_database_rejected (void)
{
    char *root = new_temp_root (
        "atm-conversation-foreign-XXXXXX"
    );
    char *path = store_path (root);

    raw_exec (
        path,
        "CREATE TABLE foreign_table(value INTEGER);"
        "PRAGMA application_id=305419896;"
        "PRAGMA user_version=1;"
    );

    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_IDENTITY
    );
    g_assert_null (store);
    g_clear_error (&error);

    char *journal = raw_text (
        path,
        "PRAGMA journal_mode;"
    );
    g_assert_cmpstr (
        journal,
        ==,
        "delete"
    );
    g_free (journal);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_newer_schema_rejected (void)
{
    char *root = new_temp_root (
        "atm-conversation-newer-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    raw_exec (
        path,
        "PRAGMA user_version=7;"
    );

    store = NULL;
    g_assert_false (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_SCHEMA
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_missing_schema_is_rejected_without_journal_mutation (void)
{
    char *root = new_temp_root (
        "atm-conversation-missing-schema-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    raw_exec (
        path,
        "PRAGMA journal_mode=DELETE;"
        "PRAGMA foreign_keys=OFF;"
        "DROP TABLE citations;"
    );

    char *before = raw_text (
        path,
        "PRAGMA journal_mode;"
    );
    g_assert_cmpstr (
        before,
        ==,
        "delete"
    );
    g_free (before);

    store = NULL;
    g_assert_false (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_SCHEMA
    );
    g_assert_null (store);
    g_clear_error (&error);

    char *after = raw_text (
        path,
        "PRAGMA journal_mode;"
    );
    g_assert_cmpstr (
        after,
        ==,
        "delete"
    );
    g_free (after);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
insert_valid_grounded_conversation (
    const char *path
)
{
    raw_exec (
        path,
        "PRAGMA foreign_keys=ON;"
        "BEGIN IMMEDIATE;"
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,"
        "model_name,model_digest,repository_generation_id,archived"
        ") VALUES("
        "'conv-1','Test',1,2,'model-a','digest-a',7,0"
        ");"
        "INSERT INTO conversation_repositories("
        "conversation_id,repository_id,repository_version,snapshot_sha"
        ") VALUES("
        "'conv-1','rmd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567'"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'m-user','conv-1',0,0,'user',"
        "'question','question',0,1"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'m-assistant','conv-1',1,0,'assistant',"
        "'answer [S1]','answer',1,2"
        ");"
        "INSERT INTO citations("
        "message_id,ordinal,label,repository_id,repository_version,"
        "snapshot_sha,logical_source_id,source_path,locator,title,excerpt,"
        "immutable_permalink"
        ") VALUES("
        "'m-assistant',1,'S1','rmd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567',"
        "'source-1','README.md','lines 1-2','Title','Excerpt',NULL"
        ");"
        "COMMIT;"
    );
}

static void
test_valid_grounded_semantics (void)
{
    char *root = new_temp_root (
        "atm-conversation-valid-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    insert_valid_grounded_conversation (
        path
    );

    store = NULL;
    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_conversation_store_validate (
            store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_citation_semantics_fail_closed (void)
{
    char *root = new_temp_root (
        "atm-conversation-invalid-citation-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    raw_exec (
        path,
        "PRAGMA foreign_keys=ON;"
        "BEGIN IMMEDIATE;"
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,"
        "model_name,model_digest,repository_generation_id,archived"
        ") VALUES("
        "'conv-bad','Bad',1,2,'model-a',NULL,7,0"
        ");"
        "INSERT INTO conversation_repositories("
        "conversation_id,repository_id,repository_version,snapshot_sha"
        ") VALUES("
        "'conv-bad','rmd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567'"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'u-bad','conv-bad',0,0,'user','q','q',0,1"
        ");"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,"
        "provider_content,display_content,grounded,created_at_us"
        ") VALUES("
        "'a-bad','conv-bad',1,0,'assistant','a','a',0,2"
        ");"
        "INSERT INTO citations("
        "message_id,ordinal,label,repository_id,repository_version,"
        "snapshot_sha,logical_source_id,source_path,locator"
        ") VALUES("
        "'a-bad',1,'S1','rmd','0.1.0',"
        "'0123456789abcdef0123456789abcdef01234567',"
        "'source-1','README.md','lines 1-2'"
        ");"
        "COMMIT;"
    );

    store = NULL;
    g_assert_false (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_INTEGRITY
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static gint64
count_for_conversation (
    const char *path,
    const char *table,
    const char *conversation_id
)
{
    char *sql = g_strdup_printf (
        "SELECT count(*) FROM %s WHERE conversation_id='%s';",
        table,
        conversation_id
    );
    gint64 count = raw_int64 (
        path,
        sql
    );
    g_free (sql);
    return count;
}

static void
test_atomic_write_api (void)
{
    char *root = new_temp_root (
        "atm-conversation-write-api-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);

    char *plain_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Plain",
            10,
            "model-a",
            NULL,
            0,
            NULL,
            0,
            &plain_id,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (plain_id);

    gint64 turn_no = -1;

    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            plain_id,
            "hello",
            "world",
            "world",
            FALSE,
            11,
            NULL,
            0,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        turn_no,
        ==,
        0
    );
    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            plain_id
        ),
        ==,
        2
    );

    AtmConversationRepositoryInput repository = {
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567"
    };
    char *grounded_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Grounded",
            20,
            "model-b",
            "digest-b",
            7,
            &repository,
            1,
            &grounded_id,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (grounded_id);

    AtmConversationCitationInput wrong_citation = {
        .label = "S1",
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "1111111111111111111111111111111111111111",
        .logical_source_id = "source-1",
        .source_path = "README.md",
        .locator = "lines 1-2",
        .title = "Title",
        .excerpt = "Excerpt",
        .immutable_permalink = NULL
    };

    turn_no = -1;
    g_assert_false (
        atm_conversation_store_commit_turn (
            store,
            grounded_id,
            "question",
            "answer [S1]",
            "answer",
            TRUE,
            21,
            &wrong_citation,
            1,
            &turn_no,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_INTEGRITY
    );
    g_assert_cmpint (
        turn_no,
        ==,
        -1
    );
    g_clear_error (&error);
    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            grounded_id
        ),
        ==,
        0
    );

    AtmConversationCitationInput citation = {
        .label = "S1",
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .logical_source_id = "source-1",
        .source_path = "README.md",
        .locator = "lines 1-2",
        .title = "Title",
        .excerpt = "Excerpt",
        .immutable_permalink = NULL
    };

    turn_no = -1;
    g_assert_false (
        atm_conversation_store_commit_turn (
            store,
            grounded_id,
            "question",
            "answer [S1]",
            "answer",
            TRUE,
            19,
            &citation,
            1,
            &turn_no,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_cmpint (
        turn_no,
        ==,
        -1
    );
    g_clear_error (&error);

    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            grounded_id
        ),
        ==,
        0
    );

    char *updated_sql = g_strdup_printf (
        "SELECT updated_at_us FROM conversations "
        "WHERE conversation_id='%s';",
        grounded_id
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            updated_sql
        ),
        ==,
        20
    );
    g_free (updated_sql);

    turn_no = -1;
    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            grounded_id,
            "question",
            "answer [S1]",
            "answer",
            TRUE,
            21,
            &citation,
            1,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        turn_no,
        ==,
        0
    );
    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            grounded_id
        ),
        ==,
        2
    );

    char *provider_sql = g_strdup_printf (
        "SELECT provider_content FROM messages "
        "WHERE conversation_id='%s' AND role='assistant';",
        grounded_id
    );
    char *provider_content = raw_text (
        path,
        provider_sql
    );
    g_free (provider_sql);
    g_assert_cmpstr (
        provider_content,
        ==,
        "answer [S1]"
    );
    g_free (provider_content);

    char *display_sql = g_strdup_printf (
        "SELECT display_content FROM messages "
        "WHERE conversation_id='%s' AND role='assistant';",
        grounded_id
    );
    char *display_content = raw_text (
        path,
        display_sql
    );
    g_free (display_sql);
    g_assert_cmpstr (
        display_content,
        ==,
        "answer"
    );
    g_free (display_content);

    char *citation_sql = g_strdup_printf (
        "SELECT count(*) FROM citations c "
        "JOIN messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id='%s';",
        grounded_id
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            citation_sql
        ),
        ==,
        1
    );
    g_free (citation_sql);

    turn_no = -1;
    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            grounded_id,
            "follow-up",
            "plain follow-up",
            "plain follow-up",
            FALSE,
            22,
            NULL,
            0,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        turn_no,
        ==,
        1
    );
    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            grounded_id
        ),
        ==,
        4
    );

    g_assert_true (
        atm_conversation_store_validate (
            store,
            &error
        )
    );
    g_assert_no_error (error);

    atm_conversation_store_close (store);
    g_free (grounded_id);
    g_free (plain_id);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_snapshot_read_api (void)
{
    char *root = new_temp_root (
        "atm-conversation-read-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);

    char *plain_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Plain",
            10,
            "model-a",
            NULL,
            0,
            NULL,
            0,
            &plain_id,
            &error
        )
    );
    g_assert_no_error (error);

    gint64 turn_no = -1;

    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            plain_id,
            "hello",
            "world",
            "world",
            FALSE,
            11,
            NULL,
            0,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (turn_no, ==, 0);

    AtmConversationRepositoryInput repository = {
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567"
    };
    char *grounded_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Grounded",
            20,
            "model-b",
            "digest-b",
            7,
            &repository,
            1,
            &grounded_id,
            &error
        )
    );
    g_assert_no_error (error);

    AtmConversationCitationInput citation = {
        .label = "S1",
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .logical_source_id = "source-1",
        .source_path = "README.md",
        .locator = "lines 1-2",
        .title = "Title",
        .excerpt = "Excerpt",
        .immutable_permalink =
            "https://example.invalid/rmd/012345/README.md#L1-L2"
    };

    turn_no = -1;
    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            grounded_id,
            "question",
            "answer [S1]",
            "answer",
            TRUE,
            21,
            &citation,
            1,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (turn_no, ==, 0);

    turn_no = -1;
    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            grounded_id,
            "follow-up",
            "plain follow-up",
            "plain follow-up",
            FALSE,
            22,
            NULL,
            0,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (turn_no, ==, 1);

    g_assert_true (
        atm_conversation_store_update_title (
            store,
            grounded_id,
            "Updated grounded",
            23,
            &error
        )
    );
    g_assert_no_error (error);

    AtmConversationList *list = NULL;

    g_assert_true (
        atm_conversation_store_list_conversations (
            store,
            &list,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (list);
    g_assert_cmpuint (
        atm_conversation_list_count (list),
        ==,
        2
    );
    g_assert_cmpstr (
        atm_conversation_list_id_at (list, 0),
        ==,
        grounded_id
    );
    g_assert_cmpstr (
        atm_conversation_list_title_at (list, 0),
        ==,
        "Updated grounded"
    );
    g_assert_cmpint (
        atm_conversation_list_created_at_us_at (
            list,
            0
        ),
        ==,
        20
    );
    g_assert_cmpint (
        atm_conversation_list_updated_at_us_at (
            list,
            0
        ),
        ==,
        23
    );
    g_assert_false (
        atm_conversation_list_archived_at (
            list,
            0
        )
    );
    g_assert_null (
        atm_conversation_list_id_at (
            list,
            2
        )
    );

    AtmConversationSnapshot *snapshot = NULL;

    g_assert_true (
        atm_conversation_store_load_snapshot (
            store,
            grounded_id,
            &snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (snapshot);
    g_assert_cmpstr (
        atm_conversation_snapshot_id (snapshot),
        ==,
        grounded_id
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_title (snapshot),
        ==,
        "Updated grounded"
    );
    g_assert_cmpint (
        atm_conversation_snapshot_created_at_us (
            snapshot
        ),
        ==,
        20
    );
    g_assert_cmpint (
        atm_conversation_snapshot_updated_at_us (
            snapshot
        ),
        ==,
        23
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_model_name (
            snapshot
        ),
        ==,
        "model-b"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_model_digest (
            snapshot
        ),
        ==,
        "digest-b"
    );
    g_assert_cmpint (
        atm_conversation_snapshot_repository_generation_id (
            snapshot
        ),
        ==,
        7
    );
    g_assert_false (
        atm_conversation_snapshot_archived (
            snapshot
        )
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_repository_count (
            snapshot
        ),
        ==,
        1
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_repository_id_at (
            snapshot,
            0
        ),
        ==,
        "rmd"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_repository_version_at (
            snapshot,
            0
        ),
        ==,
        "0.1.0"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_repository_sha_at (
            snapshot,
            0
        ),
        ==,
        repository.snapshot_sha
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_message_count (
            snapshot
        ),
        ==,
        4
    );
    g_assert_cmpint (
        atm_conversation_snapshot_message_sequence_no_at (
            snapshot,
            0
        ),
        ==,
        0
    );
    g_assert_cmpint (
        atm_conversation_snapshot_message_turn_no_at (
            snapshot,
            0
        ),
        ==,
        0
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_message_role_at (
            snapshot,
            0
        ),
        ==,
        "user"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_message_provider_content_at (
            snapshot,
            0
        ),
        ==,
        "question"
    );
    g_assert_false (
        atm_conversation_snapshot_message_grounded_at (
            snapshot,
            0
        )
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_message_role_at (
            snapshot,
            1
        ),
        ==,
        "assistant"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_message_provider_content_at (
            snapshot,
            1
        ),
        ==,
        "answer [S1]"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_message_display_content_at (
            snapshot,
            1
        ),
        ==,
        "answer"
    );
    g_assert_true (
        atm_conversation_snapshot_message_grounded_at (
            snapshot,
            1
        )
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_message_citation_count_at (
            snapshot,
            1
        ),
        ==,
        1
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_citation_label_at (
            snapshot,
            1,
            0
        ),
        ==,
        "S1"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_citation_repository_id_at (
            snapshot,
            1,
            0
        ),
        ==,
        "rmd"
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_citation_immutable_permalink_at (
            snapshot,
            1,
            0
        ),
        ==,
        citation.immutable_permalink
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_message_citation_count_at (
            snapshot,
            3
        ),
        ==,
        0
    );

    atm_conversation_snapshot_free (
        snapshot
    );
    snapshot = NULL;

    g_assert_false (
        atm_conversation_store_load_snapshot (
            store,
            "00000000-0000-0000-0000-000000000000",
            &snapshot,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_NOT_FOUND
    );
    g_assert_null (snapshot);
    g_clear_error (&error);

    AtmConversationList *list_after = NULL;

    g_assert_true (
        atm_conversation_store_list_conversations (
            store,
            &list_after,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        atm_conversation_list_updated_at_us_at (
            list_after,
            0
        ),
        ==,
        23
    );

    atm_conversation_list_free (
        list_after
    );
    atm_conversation_list_free (
        list
    );
    atm_conversation_store_close (
        store
    );
    g_free (grounded_id);
    g_free (plain_id);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_archive_delete_lifecycle (void)
{
    char *root = new_temp_root (
        "atm-conversation-lifecycle-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);

    AtmConversationRepositoryInput repository = {
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567"
    };
    char *conversation_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Lifecycle",
            100,
            "model-a",
            "digest-a",
            7,
            &repository,
            1,
            &conversation_id,
            &error
        )
    );
    g_assert_no_error (error);

    AtmConversationCitationInput citation = {
        .label = "S1",
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .logical_source_id = "source-1",
        .source_path = "README.md",
        .locator = "lines 1-2",
        .title = "Title",
        .excerpt = "Excerpt",
        .immutable_permalink =
            "https://example.invalid/rmd/README.md#L1-L2"
    };
    gint64 turn_no = -1;

    g_assert_true (
        atm_conversation_store_commit_turn (
            store,
            conversation_id,
            "question",
            "answer [S1]",
            "answer",
            TRUE,
            101,
            &citation,
            1,
            &turn_no,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_conversation_store_set_open_on_startup (
            store,
            conversation_id,
            FALSE,
            &error
        )
    );
    g_assert_no_error (error);

    char *open_sql = g_strdup_printf (
        "SELECT open_on_startup FROM conversations "
        "WHERE conversation_id='%s';",
        conversation_id
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            open_sql
        ),
        ==,
        0
    );
    g_free (open_sql);

    g_assert_true (
        atm_conversation_store_set_open_on_startup (
            store,
            conversation_id,
            TRUE,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_conversation_store_set_archived (
            store,
            conversation_id,
            TRUE,
            102,
            &error
        )
    );
    g_assert_no_error (error);

    AtmConversationSnapshot *snapshot = NULL;
    g_assert_true (
        atm_conversation_store_load_snapshot (
            store,
            conversation_id,
            &snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_conversation_snapshot_archived (
            snapshot
        )
    );
    g_assert_cmpint (
        atm_conversation_snapshot_updated_at_us (
            snapshot
        ),
        ==,
        102
    );
    atm_conversation_snapshot_free (snapshot);

    open_sql = g_strdup_printf (
        "SELECT open_on_startup FROM conversations "
        "WHERE conversation_id='%s';",
        conversation_id
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            open_sql
        ),
        ==,
        0
    );
    g_free (open_sql);

    g_assert_true (
        atm_conversation_store_set_archived (
            store,
            conversation_id,
            FALSE,
            103,
            &error
        )
    );
    g_assert_no_error (error);

    snapshot = NULL;
    g_assert_true (
        atm_conversation_store_load_snapshot (
            store,
            conversation_id,
            &snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (
        atm_conversation_snapshot_archived (
            snapshot
        )
    );
    atm_conversation_snapshot_free (snapshot);

    g_assert_cmpint (
        count_for_conversation (
            path,
            "conversation_repositories",
            conversation_id
        ),
        ==,
        1
    );
    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            conversation_id
        ),
        ==,
        2
    );

    char *citation_count_sql = g_strdup_printf (
        "SELECT count(*) FROM citations c "
        "JOIN messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id='%s';",
        conversation_id
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            citation_count_sql
        ),
        ==,
        1
    );
    g_free (citation_count_sql);

    g_assert_true (
        atm_conversation_store_delete_conversation (
            store,
            conversation_id,
            &error
        )
    );
    g_assert_no_error (error);

    snapshot = NULL;
    g_assert_false (
        atm_conversation_store_load_snapshot (
            store,
            conversation_id,
            &snapshot,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_NOT_FOUND
    );
    g_clear_error (&error);
    g_assert_null (snapshot);

    g_assert_cmpint (
        count_for_conversation (
            path,
            "conversation_repositories",
            conversation_id
        ),
        ==,
        0
    );
    g_assert_cmpint (
        count_for_conversation (
            path,
            "messages",
            conversation_id
        ),
        ==,
        0
    );
    citation_count_sql = g_strdup_printf (
        "SELECT count(*) FROM citations c "
        "JOIN messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id='%s';",
        conversation_id
    );
    g_assert_cmpint (
        raw_int64 (
            path,
            citation_count_sql
        ),
        ==,
        0
    );
    g_free (citation_count_sql);

    g_assert_false (
        atm_conversation_store_delete_conversation (
            store,
            conversation_id,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_NOT_FOUND
    );
    g_clear_error (&error);

    atm_conversation_store_close (store);
    g_free (conversation_id);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_bnr_chart_recipe_v5_persistence (void)
{
    char *root = new_temp_root ("atm-conversation-bnr-chart-v5-XXXXXX");
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;
    const char *sha = "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170";
    char *version = g_strconcat ("snapshot:", sha, NULL);

    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    AtmConversationRepositoryInput repository = {
        .repository_id = "rmd",
        .repository_version = version,
        .snapshot_sha = sha
    };
    char *conversation_id = NULL;
    g_assert_true (atm_conversation_store_create_conversation (
        store, "BNR BLS C01 chart", 1, "model-a", NULL, 1,
        &repository, 1, &conversation_id, &error));
    g_assert_no_error (error);

    AtmConversationCitationInput citation = {
        .label = "BNR-S1",
        .repository_id = "rmd",
        .repository_version = version,
        .snapshot_sha = sha,
        .logical_source_id = "bnr-bls-c01-credit-standards",
        .source_path = "data/processed/bnr_bls_realised_rounds.csv",
        .locator = "2022-Q4..2025-Q3",
        .title = "BNR BLS C01 credit standards",
        .excerpt = "Pinned quarterly source observations",
        .immutable_permalink = NULL
    };
    gint64 turn = -1;
    g_assert_true (atm_conversation_store_commit_turn (
        store, conversation_id, "/chart bnr-bls-c01",
        "qualified BNR chart", "qualified BNR chart", TRUE, 2,
        &citation, 1, &turn, &error));
    g_assert_no_error (error);
    g_assert_cmpint (turn, ==, 0);

    AtmConversationChartSeriesInput series = {
        .series_profile = "atm-series/bnr-bls-c01-calendar-quarter/1",
        .admission_profile = "atm-bnr-bls-canonical-panel-admission/1",
        .scientific_id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        .qualified_id = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        .repository_id = "rmd",
        .repository_version = version,
        .snapshot_sha = sha,
        .source_path = "data/processed/bnr_bls_realised_rounds.csv"
    };
    AtmConversationChartInput chart = {
        .ordinal = 1,
        .chart_schema = "atm-chart-spec/3",
        .chart_spec_id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .chart_kind = "line",
        .reconstruction_profile = "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1",
        .series = &series,
        .series_count = 1
    };
    g_assert_true (atm_conversation_store_attach_chart (
        store, conversation_id, turn, &chart, &error));
    g_assert_no_error (error);
    g_assert_true (atm_conversation_store_validate (store, &error));
    g_assert_no_error (error);

    AtmConversationChartSeriesInput wrong = series;
    wrong.snapshot_sha = "1111111111111111111111111111111111111111";
    wrong.repository_version = "snapshot:1111111111111111111111111111111111111111";
    AtmConversationChartInput bad_chart = chart;
    bad_chart.ordinal = 2;
    bad_chart.series = &wrong;
    g_assert_false (atm_conversation_store_attach_chart (
        store, conversation_id, turn, &bad_chart, &error));
    g_assert_error (error, ATM_CONVERSATION_STORE_ERROR,
                    ATM_CONVERSATION_STORE_ERROR_ARGUMENT);
    g_clear_error (&error);

    atm_conversation_store_close (store);
    store = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);
    AtmConversationSnapshot *snapshot = NULL;
    g_assert_true (atm_conversation_store_load_snapshot (
        store, conversation_id, &snapshot, &error));
    g_assert_no_error (error);
    g_assert_cmpint (atm_conversation_store_schema_version (store), ==,
                     ATM_CONVERSATION_STORE_SCHEMA_VERSION);
    g_assert_cmpuint (atm_conversation_snapshot_message_chart_count_at (
        snapshot, 1u), ==, 1u);
    g_assert_cmpstr (atm_conversation_snapshot_chart_schema_at (
        snapshot, 1u, 0u), ==, "atm-chart-spec/3");
    g_assert_cmpstr (atm_conversation_snapshot_chart_reconstruction_profile_at (
        snapshot, 1u, 0u), ==,
        "atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1");
    g_assert_cmpstr (atm_conversation_snapshot_chart_series_profile_at (
        snapshot, 1u, 0u, 0u), ==, series.series_profile);
    g_assert_cmpstr (atm_conversation_snapshot_chart_series_repository_id_at (
        snapshot, 1u, 0u, 0u), ==, "rmd");
    g_assert_cmpstr (atm_conversation_snapshot_chart_series_snapshot_sha_at (
        snapshot, 1u, 0u, 0u), ==, sha);
    g_assert_cmpstr (atm_conversation_snapshot_chart_series_source_path_at (
        snapshot, 1u, 0u, 0u), ==, series.source_path);
    atm_conversation_snapshot_free (snapshot);
    atm_conversation_store_close (store);

    g_free (conversation_id);
    g_free (version);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_chart_binding_write_load (void)
{
    char *root = new_temp_root ("atm-conversation-chart-binding-XXXXXX");
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);

    AtmConversationRepositoryInput repository = {
        .repository_id = "ewd",
        .repository_version = "0.1.0",
        .snapshot_sha = "0123456789abcdef0123456789abcdef01234567"
    };
    char *conversation_id = NULL;
    g_assert_true (
        atm_conversation_store_create_conversation (
            store, "Chart binding", 1, "model-a", NULL, 7,
            &repository, 1, &conversation_id, &error
        )
    );
    g_assert_no_error (error);

    AtmConversationCitationInput citation = {
        .label = "S1",
        .repository_id = "ewd",
        .repository_version = "0.1.0",
        .snapshot_sha = "0123456789abcdef0123456789abcdef01234567",
        .logical_source_id = "gistemp-global-annual",
        .source_path = "science/data/processed/nasa_gistemp_global_2026.csv",
        .locator = "rows 1-146",
        .title = "GISTEMP global annual",
        .excerpt = "Qualified annual observations",
        .immutable_permalink = NULL
    };
    gint64 grounded_turn = -1;
    g_assert_true (
        atm_conversation_store_commit_turn (
            store, conversation_id, "show the series", "answer [S1]", "answer",
            TRUE, 2, &citation, 1, &grounded_turn, &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (grounded_turn, ==, 0);

    AtmConversationChartSeriesInput series = {
        .series_profile = "atm-series/gistemp-complete-annual/1",
        .admission_profile = "atm-gistemp-pinned-admission/1",
        .scientific_id =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        .qualified_id =
            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        .repository_id = "ewd",
        .repository_version = "0.1.0",
        .snapshot_sha = "0123456789abcdef0123456789abcdef01234567",
        .source_path = "science/data/processed/nasa_gistemp_global_2026.csv"
    };
    AtmConversationChartInput chart = {
        .ordinal = 1,
        .chart_schema = "atm-chart-spec/1",
        .chart_spec_id =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .chart_kind = "line",
        .reconstruction_profile =
            "atm-chart-reconstruct/gistemp-complete-annual/1",
        .series = &series,
        .series_count = 1
    };

    g_assert_true (
        atm_conversation_store_attach_chart (
            store, conversation_id, grounded_turn, &chart, &error
        )
    );
    g_assert_no_error (error);

    /* The native persistence contract deliberately requires normalized kinds. */
    AtmConversationChartInput uppercase_kind_chart = chart;
    uppercase_kind_chart.ordinal = 2;
    uppercase_kind_chart.chart_kind = "LINE";
    g_assert_false (
        atm_conversation_store_attach_chart (
            store, conversation_id, grounded_turn,
            &uppercase_kind_chart, &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_ARGUMENT
    );
    g_clear_error (&error);

    g_assert_false (
        atm_conversation_store_attach_chart (
            store, conversation_id, grounded_turn, &chart, &error
        )
    );
    g_assert_nonnull (error);
    g_clear_error (&error);

    AtmConversationChartInput invalid_chart = chart;
    invalid_chart.chart_spec_id = "not-a-qualified-id";
    g_assert_false (
        atm_conversation_store_attach_chart (
            store, conversation_id, grounded_turn, &invalid_chart, &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_ARGUMENT
    );
    g_clear_error (&error);

    AtmConversationChartSeriesInput wrong_scope = series;
    wrong_scope.snapshot_sha = "1111111111111111111111111111111111111111";
    AtmConversationChartInput wrong_scope_chart = chart;
    wrong_scope_chart.ordinal = 2;
    wrong_scope_chart.series = &wrong_scope;
    g_assert_false (
        atm_conversation_store_attach_chart (
            store, conversation_id, grounded_turn, &wrong_scope_chart, &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_INTEGRITY
    );
    g_clear_error (&error);

    /* The schema-v5 allowlist admits the source-bound EI v2 tuple, but not
     * cross-paired schema/reconstruction/series/admission profiles or unpinned snapshots. */
    const char *ei_snapshot = "d9e249339663015f6d1c05752338a955bf64ad0b";
    const char *ei_repository_version = "0.1.0";
    const char *ei_version =
        "snapshot:d9e249339663015f6d1c05752338a955bf64ad0b";
    AtmConversationRepositoryInput ei_repository = {
        .repository_id = "ewd",
        .repository_version = ei_repository_version,
        .snapshot_sha = ei_snapshot
    };
    char *ei_conversation_id = NULL;
    g_assert_true (atm_conversation_store_create_conversation (
        store, "EI Chart binding", 4, "model-a", NULL, 8,
        &ei_repository, 1, &ei_conversation_id, &error));
    g_assert_no_error (error);

    AtmConversationCitationInput ei_citation = {
        .label = "EI-S1",
        .repository_id = "ewd",
        .repository_version = ei_repository_version,
        .snapshot_sha = ei_snapshot,
        .logical_source_id = "ei-primary-energy",
        .source_path = "science/data/processed/energy_institute_global_2026.csv",
        .locator = "rows 2-62",
        .title = "Energy Institute global TES",
        .excerpt = "Qualified annual total energy supply",
        .immutable_permalink = NULL
    };
    gint64 ei_grounded_turn = -1;
    g_assert_true (atm_conversation_store_commit_turn (
        store, ei_conversation_id, "show TES", "answer [EI-S1]", "answer",
        TRUE, 5, &ei_citation, 1, &ei_grounded_turn, &error));
    g_assert_no_error (error);
    g_assert_cmpint (ei_grounded_turn, ==, 0);

    AtmConversationChartSeriesInput ei_series = {
        .series_profile = "atm-series/energy-institute-tes-complete-annual/1",
        .admission_profile = "atm-energy-institute-pinned-admission/3",
        .scientific_id =
            "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
        .qualified_id =
            "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
        .repository_id = "ewd",
        .repository_version = ei_version,
        .snapshot_sha = ei_snapshot,
        .source_path = "science/data/processed/energy_institute_global_2026.csv"
    };
    AtmConversationChartInput ei_chart = {
        .ordinal = 1,
        .chart_schema = "atm-chart-spec/2",
        .chart_spec_id =
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .chart_kind = "line",
        .reconstruction_profile =
            "atm-chart-reconstruct/energy-institute-tes-complete-annual/1",
        .series = &ei_series,
        .series_count = 1
    };
    g_assert_true (atm_conversation_store_attach_chart (
        store, ei_conversation_id, ei_grounded_turn, &ei_chart, &error));
    g_assert_no_error (error);

    /* Repository release version and immutable chart-source snapshot version
     * are different identities: the conversation pin keeps CITATION.cff's
     * version, while the EI chart recipe binds snapshot:<sha>. */
    AtmConversationChartSeriesInput wrong_ei_version = ei_series;
    wrong_ei_version.repository_version = ei_repository_version;
    AtmConversationChartInput wrong_ei_version_chart = ei_chart;
    wrong_ei_version_chart.ordinal = 2;
    wrong_ei_version_chart.series = &wrong_ei_version;
    g_assert_false (atm_conversation_store_attach_chart (
        store, ei_conversation_id, ei_grounded_turn,
        &wrong_ei_version_chart, &error));
    g_assert_error (error, ATM_CONVERSATION_STORE_ERROR,
                    ATM_CONVERSATION_STORE_ERROR_ARGUMENT);
    g_clear_error (&error);

    AtmConversationChartInput mixed_tuple = ei_chart;
    mixed_tuple.ordinal = 2;
    mixed_tuple.reconstruction_profile =
        "atm-chart-reconstruct/gistemp-complete-annual/1";
    g_assert_false (atm_conversation_store_attach_chart (
        store, ei_conversation_id, ei_grounded_turn, &mixed_tuple, &error));
    g_assert_error (error, ATM_CONVERSATION_STORE_ERROR,
                    ATM_CONVERSATION_STORE_ERROR_ARGUMENT);
    g_clear_error (&error);

    AtmConversationChartSeriesInput unsupported_ei_snapshot = ei_series;
    unsupported_ei_snapshot.snapshot_sha =
        "1111111111111111111111111111111111111111";
    unsupported_ei_snapshot.repository_version =
        "snapshot:1111111111111111111111111111111111111111";
    AtmConversationChartInput unsupported_snapshot_chart = ei_chart;
    unsupported_snapshot_chart.ordinal = 2;
    unsupported_snapshot_chart.series = &unsupported_ei_snapshot;
    g_assert_false (atm_conversation_store_attach_chart (
        store, ei_conversation_id, ei_grounded_turn,
        &unsupported_snapshot_chart, &error));
    g_assert_error (error, ATM_CONVERSATION_STORE_ERROR,
                    ATM_CONVERSATION_STORE_ERROR_ARGUMENT);
    g_clear_error (&error);

    gint64 ungrounded_turn = -1;
    g_assert_true (
        atm_conversation_store_commit_turn (
            store, conversation_id, "plain turn", "plain answer", "plain answer",
            FALSE, 3, NULL, 0, &ungrounded_turn, &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (ungrounded_turn, ==, 1);
    g_assert_false (
        atm_conversation_store_attach_chart (
            store, conversation_id, ungrounded_turn, &chart, &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_STORE_ERROR,
        ATM_CONVERSATION_STORE_ERROR_INTEGRITY
    );
    g_clear_error (&error);

    atm_conversation_store_close (store);
    store = NULL;
    g_assert_true (atm_conversation_store_open (path, &store, &error));
    g_assert_no_error (error);

    AtmConversationSnapshot *snapshot = NULL;
    g_assert_true (
        atm_conversation_store_load_snapshot (
            store, conversation_id, &snapshot, &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (atm_conversation_snapshot_message_count (snapshot), ==, 4);
    g_assert_cmpuint (
        atm_conversation_snapshot_message_chart_count_at (snapshot, 0), ==, 0
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_message_chart_count_at (snapshot, 1), ==, 1
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_message_chart_count_at (snapshot, 3), ==, 0
    );
    g_assert_cmpint (
        atm_conversation_snapshot_chart_ordinal_at (snapshot, 1, 0), ==, 1
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_schema_at (snapshot, 1, 0),
        ==, chart.chart_schema
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_spec_id_at (snapshot, 1, 0),
        ==, chart.chart_spec_id
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_kind_at (snapshot, 1, 0),
        ==, chart.chart_kind
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_reconstruction_profile_at (snapshot, 1, 0),
        ==, chart.reconstruction_profile
    );
    g_assert_cmpuint (
        atm_conversation_snapshot_chart_series_count_at (snapshot, 1, 0), ==, 1
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_profile_at (snapshot, 1, 0, 0),
        ==, series.series_profile
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_admission_profile_at (snapshot, 1, 0, 0),
        ==, series.admission_profile
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_scientific_id_at (snapshot, 1, 0, 0),
        ==, series.scientific_id
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_qualified_id_at (snapshot, 1, 0, 0),
        ==, series.qualified_id
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_repository_id_at (snapshot, 1, 0, 0),
        ==, series.repository_id
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_repository_version_at (snapshot, 1, 0, 0),
        ==, series.repository_version
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_snapshot_sha_at (snapshot, 1, 0, 0),
        ==, series.snapshot_sha
    );
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_source_path_at (snapshot, 1, 0, 0),
        ==, series.source_path
    );

    g_assert_cmpint (
        atm_conversation_snapshot_chart_ordinal_at (snapshot, 1, 1), ==, -1
    );
    g_assert_null (
        atm_conversation_snapshot_chart_spec_id_at (snapshot, 1, 1)
    );
    g_assert_null (
        atm_conversation_snapshot_chart_series_qualified_id_at (snapshot, 1, 0, 1)
    );

    atm_conversation_snapshot_free (snapshot);

    AtmConversationSnapshot *ei_snapshot_state = NULL;
    g_assert_true (atm_conversation_store_load_snapshot (
        store, ei_conversation_id, &ei_snapshot_state, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (atm_conversation_snapshot_message_count (ei_snapshot_state), ==, 2);
    g_assert_cmpuint (
        atm_conversation_snapshot_message_chart_count_at (ei_snapshot_state, 1), ==, 1);
    g_assert_cmpstr (
        atm_conversation_snapshot_repository_version_at (ei_snapshot_state, 0),
        ==, ei_repository_version);
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_schema_at (ei_snapshot_state, 1, 0),
        ==, "atm-chart-spec/2");
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_reconstruction_profile_at (ei_snapshot_state, 1, 0),
        ==, ei_chart.reconstruction_profile);
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_profile_at (ei_snapshot_state, 1, 0, 0),
        ==, ei_series.series_profile);
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_admission_profile_at (ei_snapshot_state, 1, 0, 0),
        ==, ei_series.admission_profile);
    g_assert_cmpstr (
        atm_conversation_snapshot_chart_series_snapshot_sha_at (ei_snapshot_state, 1, 0, 0),
        ==, ei_snapshot);
    atm_conversation_snapshot_free (ei_snapshot_state);

    g_assert_true (
        atm_conversation_store_delete_conversation (
            store, ei_conversation_id, &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_conversation_store_delete_conversation (
            store, conversation_id, &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM message_charts;"), ==, 0);
    g_assert_cmpint (raw_int64 (path, "SELECT count(*) FROM chart_series;"), ==, 0);

    atm_conversation_store_close (store);
    g_free (ei_conversation_id);
    g_free (conversation_id);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_readonly_repository_generation_roots (void)
{
    char *root = new_temp_root (
        "atm-conversation-generation-roots-XXXXXX"
    );
    char *path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);

    char *plain_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Plain",
            1,
            "model",
            NULL,
            0,
            NULL,
            0,
            &plain_id,
            &error
        )
    );
    g_assert_no_error (error);

    AtmConversationRepositoryInput repository = {
        .repository_id = "rmd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "1111111111111111111111111111111111111111"
    };

    char *first_id = NULL;
    char *duplicate_id = NULL;
    char *second_id = NULL;

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Pinned seven",
            2,
            "model",
            NULL,
            7,
            &repository,
            1,
            &first_id,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Pinned seven again",
            3,
            "model",
            NULL,
            7,
            &repository,
            1,
            &duplicate_id,
            &error
        )
    );
    g_assert_no_error (error);

    repository.snapshot_sha =
        "2222222222222222222222222222222222222222";

    g_assert_true (
        atm_conversation_store_create_conversation (
            store,
            "Pinned eleven",
            4,
            "model",
            NULL,
            11,
            &repository,
            1,
            &second_id,
            &error
        )
    );
    g_assert_no_error (error);

    atm_conversation_store_close (store);

    gint64 *generation_ids = NULL;
    gsize generation_count = 0;

    g_assert_true (
        atm_conversation_store_list_repository_generation_ids_readonly (
            path,
            &generation_ids,
            &generation_count,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        generation_count,
        ==,
        2
    );
    g_assert_nonnull (generation_ids);
    g_assert_cmpint (
        generation_ids[0],
        ==,
        7
    );
    g_assert_cmpint (
        generation_ids[1],
        ==,
        11
    );
    g_free (generation_ids);

    char *missing = g_build_filename (
        root,
        "missing.sqlite3",
        NULL
    );
    generation_ids = NULL;
    generation_count = 0;

    g_assert_false (
        atm_conversation_store_list_repository_generation_ids_readonly (
            missing,
            &generation_ids,
            &generation_count,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (generation_ids);
    g_assert_cmpuint (
        generation_count,
        ==,
        0
    );
    g_assert_false (
        g_file_test (
            missing,
            G_FILE_TEST_EXISTS
        )
    );
    g_clear_error (&error);

    g_free (missing);
    g_free (plain_id);
    g_free (first_id);
    g_free (duplicate_id);
    g_free (second_id);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}


static void
test_symlink_rejected (void)
{
    char *root = new_temp_root (
        "atm-conversation-nofollow-XXXXXX"
    );
    char *target = g_build_filename (
        root,
        "real.sqlite3",
        NULL
    );
    char *link_path = store_path (root);
    AtmConversationStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_conversation_store_open (
            target,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_conversation_store_close (store);

    g_assert_cmpint (
        symlink (
            target,
            link_path
        ),
        ==,
        0
    );

    store = NULL;
    g_assert_false (
        atm_conversation_store_open (
            link_path,
            &store,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (store);
    g_clear_error (&error);

    g_free (link_path);
    g_free (target);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (
        &argc,
        &argv,
        NULL
    );

    g_test_add_func (
        "/conversation-store/bootstrap-reopen",
        test_bootstrap_and_reopen
    );
    g_test_add_func (
        "/conversation-store/v1-to-v6-migration",
        test_v1_to_v5_migration
    );
    g_test_add_func (
        "/conversation-store/v1-to-v3-rollback",
        test_v1_to_v3_migration_rollback
    );
    g_test_add_func (
        "/conversation-store/v2-to-v6-migration",
        test_v2_to_v5_migration
    );
    g_test_add_func (
        "/conversation-store/v2-to-v3-rollback",
        test_v2_to_v3_migration_rollback
    );
    g_test_add_func (
        "/conversation-store/v3-to-v6-migration",
        test_v3_to_v5_migration_preserves_chart_rows
    );
    g_test_add_func (
        "/conversation-store/v3-to-v4-rollback",
        test_v3_to_v4_migration_rollback
    );
    g_test_add_func (
        "/conversation-store/v4-to-v6-migration",
        test_v4_to_v5_migration_preserves_chart_rows
    );
    g_test_add_func (
        "/conversation-store/v4-to-v5-rollback",
        test_v4_to_v5_migration_rollback
    );
    g_test_add_func (
        "/conversation-store/v5-direct-to-v6-migration",
        test_v5_direct_to_v6_migration
    );
    g_test_add_func (
        "/conversation-store/v5-chart-v1-v2-v3-migration",
        test_v5_chart_v1_v2_v3_rows_migrate
    );
    g_test_add_func (
        "/conversation-store/v5-via-v4-to-v6-migration",
        test_v5_via_v4_to_v6_migration
    );
    g_test_add_func (
        "/conversation-store/v5-canonical-fixture-forms",
        test_v5_canonical_fixture_forms
    );
    g_test_add_func (
        "/conversation-store/v5-canonical-ddl-resource-hashes",
        test_v5_canonical_ddl_resource_hashes
    );
    g_test_add_func (
        "/conversation-store/v5-auxiliary-index-rejected",
        test_v5_auxiliary_index_rejected_without_changes
    );
    g_test_add_func (
        "/conversation-store/v5-auxiliary-objects-rejected",
        test_v5_auxiliary_objects_rejected_without_changes
    );
    g_test_add_func (
        "/conversation-store/v5-to-v6-rollback-after-ddl",
        test_v5_to_v6_rollback_after_ddl
    );
    g_test_add_func (
        "/conversation-store/v5-to-v6-rollback-after-sql-resource",
        test_v5_to_v6_rollback_after_sql_resource
    );
    g_test_add_func (
        "/conversation-store/v5-preflight-concurrent-schema-change",
        test_v5_preflight_serializes_with_concurrent_schema_change
    );
    g_test_add_func (
        "/conversation-store/chart-recipe-semantics",
        test_chart_recipe_semantics
    );
    g_test_add_func (
        "/conversation-store/foreign-database",
        test_foreign_database_rejected
    );
    g_test_add_func (
        "/conversation-store/newer-schema",
        test_newer_schema_rejected
    );
    g_test_add_func (
        "/conversation-store/missing-schema-no-journal-mutation",
        test_missing_schema_is_rejected_without_journal_mutation
    );
    g_test_add_func (
        "/conversation-store/valid-grounded-semantics",
        test_valid_grounded_semantics
    );
    g_test_add_func (
        "/conversation-store/invalid-citation-semantics",
        test_invalid_citation_semantics_fail_closed
    );
    g_test_add_func (
        "/conversation-store/atomic-write-api",
        test_atomic_write_api
    );
    g_test_add_func (
        "/conversation-store/snapshot-read-api",
        test_snapshot_read_api
    );
    g_test_add_func (
        "/conversation-store/archive-delete-lifecycle",
        test_archive_delete_lifecycle
    );
    g_test_add_func (
        "/conversation-store/bnr-chart-v5-persistence",
        test_bnr_chart_recipe_v5_persistence
    );
    g_test_add_func (
        "/conversation-store/chart-binding-write-load",
        test_chart_binding_write_load
    );
    g_test_add_func (
        "/conversation-store/readonly-repository-generation-roots",
        test_readonly_repository_generation_roots
    );
    g_test_add_func (
        "/conversation-store/nofollow-symlink",
        test_symlink_rejected
    );

    return g_test_run ();
}
