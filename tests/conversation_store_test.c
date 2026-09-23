#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <sqlite3.h>

#include "conversation_store.h"

#include <sys/stat.h>
#include <unistd.h>

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

    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
            &error
        )
    );
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
    g_assert_true (
        atm_conversation_store_open (
            path,
            &store,
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
test_v1_to_v2_migration (void)
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
        "PRAGMA user_version=2;"
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
        "/conversation-store/v1-to-v2-migration",
        test_v1_to_v2_migration
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
        "/conversation-store/nofollow-symlink",
        test_symlink_rejected
    );

    return g_test_run ();
}
