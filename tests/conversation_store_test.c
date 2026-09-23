#include <glib.h>
#include <glib/gstdio.h>
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
        "/conversation-store/nofollow-symlink",
        test_symlink_rejected
    );

    return g_test_run ();
}
