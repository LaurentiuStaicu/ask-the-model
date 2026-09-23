#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include "control_state.h"

#include <sys/stat.h>

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

        while ((name = g_dir_read_name (directory)) != NULL) {
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
db_path (const char *root)
{
    return g_build_filename (
        root,
        "control-state.sqlite3",
        NULL
    );
}

static gint64
raw_pragma_int64 (
    const char *path,
    const char *pragma
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gint64 value = -1;

    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            pragma,
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
raw_pragma_text (
    const char *path,
    const char *pragma
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            pragma,
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

    char *value = g_strdup (
        (const char *) sqlite3_column_text (
            statement,
            0
        )
    );

    sqlite3_finalize (statement);
    sqlite3_close (db);
    return value;
}

static void
test_bootstrap_and_reopen (void)
{
    char *root = new_temp_root (
        "atm-control-state-XXXXXX"
    );
    char *path = db_path (root);
    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (store);
    g_assert_cmpstr (
        atm_control_state_path (store),
        ==,
        path
    );
    g_assert_cmpint (
        atm_control_state_application_id (store),
        ==,
        ATM_CONTROL_STATE_APPLICATION_ID
    );
    g_assert_cmpint (
        atm_control_state_schema_version (store),
        ==,
        ATM_CONTROL_STATE_SCHEMA_VERSION
    );
    g_assert_true (
        atm_control_state_validate (
            store,
            &error
        )
    );
    g_assert_no_error (error);

    atm_control_state_close (store);
    store = NULL;

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "PRAGMA application_id;"
        ),
        ==,
        ATM_CONTROL_STATE_APPLICATION_ID
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        ATM_CONTROL_STATE_SCHEMA_VERSION
    );

    char *journal = raw_pragma_text (
        path,
        "PRAGMA journal_mode;"
    );
    g_assert_cmpstr (journal, ==, "wal");
    g_free (journal);

    g_assert_true (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_control_state_validate (
            store,
            &error
        )
    );
    g_assert_no_error (error);

    atm_control_state_close (store);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
create_foreign_db (
    const char *path,
    guint application_id,
    guint user_version,
    gboolean create_table
)
{
    sqlite3 *db = NULL;

    g_assert_cmpint (
        sqlite3_open (path, &db),
        ==,
        SQLITE_OK
    );

    char *sql = g_strdup_printf (
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=%u;"
        "%s",
        application_id,
        user_version,
        create_table
            ? "CREATE TABLE foreign_table(id INTEGER);"
            : ""
    );

    g_assert_cmpint (
        sqlite3_exec (
            db,
            sql,
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_free (sql);
    sqlite3_close (db);
}

static void
test_foreign_application_id_rejected (void)
{
    char *root = new_temp_root (
        "atm-control-state-foreign-XXXXXX"
    );
    char *path = db_path (root);

    create_foreign_db (
        path,
        0x47504b47,
        1,
        TRUE
    );

    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_IDENTITY
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_newer_schema_rejected (void)
{
    char *root = new_temp_root (
        "atm-control-state-newer-XXXXXX"
    );
    char *path = db_path (root);

    create_foreign_db (
        path,
        ATM_CONTROL_STATE_APPLICATION_ID,
        ATM_CONTROL_STATE_SCHEMA_VERSION + 1,
        TRUE
    );

    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_SCHEMA
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_missing_schema_table_rejected (void)
{
    char *root = new_temp_root (
        "atm-control-state-schema-XXXXXX"
    );
    char *path = db_path (root);

    create_foreign_db (
        path,
        ATM_CONTROL_STATE_APPLICATION_ID,
        ATM_CONTROL_STATE_SCHEMA_VERSION,
        TRUE
    );

    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_SCHEMA
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_corrupt_file_rejected (void)
{
    char *root = new_temp_root (
        "atm-control-state-corrupt-XXXXXX"
    );
    char *path = db_path (root);
    GError *write_error = NULL;

    g_assert_true (
        g_file_set_contents (
            path,
            "not a sqlite database",
            -1,
            &write_error
        )
    );
    g_assert_no_error (write_error);

    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_shadow_store_does_not_touch_legacy_json (void)
{
    char *root = new_temp_root (
        "atm-control-state-shadow-XXXXXX"
    );
    char *path = db_path (root);
    char *legacy = g_build_filename (
        root,
        "repository-state.json",
        NULL
    );
    const char *sentinel =
        "{\"schema_version\":2,\"sentinel\":true}";
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            legacy,
            sentinel,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    AtmControlStateStore *store = NULL;

    g_assert_true (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_control_state_close (store);

    char *after = NULL;
    gsize length = 0;

    g_assert_true (
        g_file_get_contents (
            legacy,
            &after,
            &length,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (after, ==, sentinel);

    g_free (after);
    g_free (legacy);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/control-state/bootstrap-reopen",
        test_bootstrap_and_reopen
    );
    g_test_add_func (
        "/control-state/foreign-application-id",
        test_foreign_application_id_rejected
    );
    g_test_add_func (
        "/control-state/newer-schema",
        test_newer_schema_rejected
    );
    g_test_add_func (
        "/control-state/missing-schema",
        test_missing_schema_table_rejected
    );
    g_test_add_func (
        "/control-state/corrupt-file",
        test_corrupt_file_rejected
    );
    g_test_add_func (
        "/control-state/shadow-does-not-touch-json",
        test_shadow_store_does_not_touch_legacy_json
    );

    return g_test_run ();
}
