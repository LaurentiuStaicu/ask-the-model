#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <sqlite3.h>

#include "control_state.h"

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


static int
raw_exec (
    const char *path,
    const char *sql,
    char **out_message
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

    int rc = sqlite3_exec (
        db,
        sql,
        NULL,
        NULL,
        &message
    );

    if (out_message != NULL) {
        *out_message = g_strdup (
            message != NULL
                ? message
                : ""
        );
    }

    sqlite3_free (message);
    sqlite3_close (db);
    return rc;
}

static void
create_v1_control_db (
    const char *path,
    gboolean with_repository_state,
    gboolean with_persisted_candidate
)
{
    GError *resource_error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/control-state-v1.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &resource_error
    );

    g_assert_no_error (resource_error);
    g_assert_nonnull (bytes);

    gsize size = 0;
    const char *schema = g_bytes_get_data (
        bytes,
        &size
    );
    g_assert_nonnull (schema);
    g_assert_cmpuint (size, >, 0);

    sqlite3 *db = NULL;
    g_assert_cmpint (
        sqlite3_open (
            path,
            &db
        ),
        ==,
        SQLITE_OK
    );

    char *schema_sql = g_strndup (
        schema,
        size
    );
    char *bootstrap = g_strdup_printf (
        "BEGIN IMMEDIATE;"
        "%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=1;"
        "COMMIT;",
        schema_sql,
        (guint) ATM_CONTROL_STATE_APPLICATION_ID
    );

    g_assert_cmpint (
        sqlite3_exec (
            db,
            bootstrap,
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    if (with_repository_state) {
        g_assert_cmpint (
            sqlite3_exec (
                db,
                "BEGIN IMMEDIATE;"
                "INSERT INTO repository_generations("
                "generation_id,lifecycle,origin"
                ") VALUES(1,'COMPLETE','v1-fixture');"
                "INSERT INTO generation_repositories("
                "generation_id,repository_id,snapshot_sha,"
                "repository_version,snapshot_seal_sha256"
                ") VALUES("
                "1,'rmd',"
                "'0123456789abcdef0123456789abcdef01234567',"
                "'0.1.0',"
                "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'"
                ");"
                "UPDATE active_state "
                "SET active_repository_generation=1 "
                "WHERE singleton_id=1;"
                "INSERT INTO migration_ledger("
                "migration_id,schema_version,applied_origin"
                ") VALUES('v1-fixture-ledger',1,'test-fixture');"
                "COMMIT;",
                NULL,
                NULL,
                NULL
            ),
            ==,
            SQLITE_OK
        );
    }

    if (with_persisted_candidate) {
        g_assert_cmpint (
            sqlite3_exec (
                db,
                "INSERT INTO repository_generations("
                "generation_id,lifecycle,origin"
                ") VALUES(99,'CANDIDATE','invalid-persisted-candidate');",
                NULL,
                NULL,
                NULL
            ),
            ==,
            SQLITE_OK
        );
    }

    g_free (bootstrap);
    g_free (schema_sql);
    sqlite3_close (db);
    g_bytes_unref (bytes);
}

static void
test_sqlite_security_floor (void)
{
    g_assert_cmpint (
        sqlite3_libversion_number (),
        >=,
        3031000
    );
}

static void
test_control_state_symlink_is_rejected (void)
{
    char *root = new_temp_root (
        "atm-control-state-nofollow-XXXXXX"
    );
    char *target = g_build_filename (
        root,
        "real-control-state.sqlite3",
        NULL
    );
    char *link_path = db_path (root);

    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_control_state_open (
            target,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (store);
    atm_control_state_close (store);
    store = NULL;

    g_assert_cmpint (
        symlink (
            target,
            link_path
        ),
        ==,
        0
    );

    g_assert_false (
        atm_control_state_open (
            link_path,
            &store,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_SQLITE
    );
    g_assert_null (store);
    g_clear_error (&error);

    gint64 generation_id = -1;
    g_assert_false (
        atm_control_state_active_generation_id (
            link_path,
            &generation_id,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_IO
    );
    g_assert_cmpint (generation_id, ==, 0);
    g_clear_error (&error);

    GStatBuf st;
    g_assert_cmpint (
        g_lstat (
            link_path,
            &st
        ),
        ==,
        0
    );
    g_assert_true (S_ISLNK (st.st_mode));
    g_assert_true (
        g_file_test (
            target,
            G_FILE_TEST_IS_REGULAR
        )
    );

    g_free (link_path);
    g_free (target);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_read_only_control_state_is_rejected (void)
{
    if (geteuid () == 0) {
        g_test_skip (
            "Permission fallback cannot be qualified reliably as root."
        );
        return;
    }

    char *root = new_temp_root (
        "atm-control-state-readonly-XXXXXX"
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
    atm_control_state_close (store);
    store = NULL;

    g_assert_cmpint (
        chmod (
            path,
            0444
        ),
        ==,
        0
    );
    g_assert_cmpint (
        chmod (
            root,
            0500
        ),
        ==,
        0
    );

    g_assert_false (
        atm_control_state_open (
            path,
            &store,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_cmpuint (
        error->domain,
        ==,
        ATM_CONTROL_STATE_ERROR
    );
    g_assert_true (
        error->code == ATM_CONTROL_STATE_ERROR_IO ||
        error->code == ATM_CONTROL_STATE_ERROR_SQLITE
    );
    g_assert_null (store);
    g_clear_error (&error);

    gint64 generation_id = -1;
    g_assert_false (
        atm_control_state_active_generation_id (
            path,
            &generation_id,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_cmpuint (
        error->domain,
        ==,
        ATM_CONTROL_STATE_ERROR
    );
    g_assert_true (
        error->code == ATM_CONTROL_STATE_ERROR_IO ||
        error->code == ATM_CONTROL_STATE_ERROR_SQLITE
    );
    g_assert_cmpint (generation_id, ==, 0);
    g_clear_error (&error);

    g_assert_cmpint (
        chmod (
            root,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        chmod (
            path,
            0600
        ),
        ==,
        0
    );

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
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


static char *
legacy_state_path (
    const char *root,
    const char *name
)
{
    return g_build_filename (
        root,
        name,
        NULL
    );
}

static void
write_legacy_state (
    const char *path,
    const char *contents
)
{
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);
}

static void
test_legacy_v2_import_and_equivalence (void)
{
    const char *legacy_json =
        "{\n"
        "  \"schema_version\": 2,\n"
        "  \"repositories\": [\n"
        "    {\"id\":\"rmd\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\",\"snapshot_seal_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},\n"
        "    {\"id\":\"ewd\",\"sha\":\"1111111111111111111111111111111111111111\",\"version\":\"0.2.0\",\"snapshot_seal_sha256\":null},\n"
        "    {\"id\":\"cbd\",\"sha\":\"2222222222222222222222222222222222222222\",\"version\":\"0.3.0\",\"snapshot_seal_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}\n"
        "  ]\n"
        "}\n";
    const char *equivalent_json =
        "{\"repositories\":["
        "{\"version\":\"0.3.0\",\"snapshot_seal_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"sha\":\"2222222222222222222222222222222222222222\",\"id\":\"cbd\"},"
        "{\"snapshot_seal_sha256\":null,\"id\":\"ewd\",\"version\":\"0.2.0\",\"sha\":\"1111111111111111111111111111111111111111\"},"
        "{\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"id\":\"rmd\",\"snapshot_seal_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"version\":\"0.1.0\"}"
        "],\"schema_version\":2}";

    char *root = new_temp_root (
        "atm-control-state-import-v2-XXXXXX"
    );
    char *path = db_path (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    char *equivalent = legacy_state_path (
        root,
        "repository-state-equivalent.json"
    );

    write_legacy_state (
        legacy,
        legacy_json
    );
    write_legacy_state (
        equivalent,
        equivalent_json
    );

    char *before = NULL;
    gsize before_length = 0;
    GError *error = NULL;

    g_assert_true (
        g_file_get_contents (
            legacy,
            &before,
            &before_length,
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

    gint64 generation_id = 0;
    g_assert_true (
        atm_control_state_import_legacy_json (
            store,
            legacy,
            &generation_id,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        generation_id,
        ==,
        1
    );

    gboolean matches = FALSE;
    g_assert_true (
        atm_control_state_generation_matches_legacy_json (
            store,
            generation_id,
            legacy,
            &matches,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (matches);

    matches = FALSE;
    g_assert_true (
        atm_control_state_generation_matches_legacy_json (
            store,
            generation_id,
            equivalent,
            &matches,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (matches);

    gint64 second_generation = 0;
    g_assert_false (
        atm_control_state_import_legacy_json (
            store,
            equivalent,
            &second_generation,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_CONFLICT
    );
    g_assert_cmpint (
        second_generation,
        ==,
        0
    );
    g_clear_error (&error);

    char *after = NULL;
    gsize after_length = 0;
    g_assert_true (
        g_file_get_contents (
            legacy,
            &after,
            &after_length,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        before_length,
        ==,
        after_length
    );
    g_assert_cmpmem (
        before,
        before_length,
        after,
        after_length
    );

    atm_control_state_close (store);

    char *lifecycle = raw_pragma_text (
        path,
        "SELECT lifecycle FROM repository_generations WHERE generation_id=1;"
    );
    g_assert_cmpstr (
        lifecycle,
        ==,
        "COMPLETE"
    );
    g_free (lifecycle);

    char *origin = raw_pragma_text (
        path,
        "SELECT origin FROM repository_generations WHERE generation_id=1;"
    );
    g_assert_cmpstr (
        origin,
        ==,
        "repository-state.json/schema=2"
    );
    g_free (origin);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT active_repository_generation IS NULL FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM generation_repositories WHERE generation_id=1;"
        ),
        ==,
        3
    );

    char *rmd_version = raw_pragma_text (
        path,
        "SELECT repository_version FROM generation_repositories WHERE generation_id=1 AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        rmd_version,
        ==,
        "0.1.0"
    );
    g_free (rmd_version);

    char *rmd_seal = raw_pragma_text (
        path,
        "SELECT snapshot_seal_sha256 FROM generation_repositories WHERE generation_id=1 AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        rmd_seal,
        ==,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    );
    g_free (rmd_seal);

    g_free (before);
    g_free (after);
    g_free (equivalent);
    g_free (legacy);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_equivalence_rejects_missing_generation (void)
{
    const char *legacy_json =
        "{\"schema_version\":1,\"repositories\":[]}";

    char *root = new_temp_root (
        "atm-control-state-equivalence-missing-XXXXXX"
    );
    char *path = db_path (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    write_legacy_state (
        legacy,
        legacy_json
    );

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

    gboolean matches = TRUE;
    g_assert_false (
        atm_control_state_generation_matches_legacy_json (
            store,
            1,
            legacy,
            &matches,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_CONFLICT
    );
    g_assert_false (matches);
    g_clear_error (&error);

    atm_control_state_close (store);
    g_free (legacy);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_legacy_v1_import_preserves_null_seal (void)
{
    const char *legacy_json =
        "{"
        "\"schema_version\":1,"
        "\"repositories\":["
        "{\"id\":\"rmd\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\"}"
        "]"
        "}";

    char *root = new_temp_root (
        "atm-control-state-import-v1-XXXXXX"
    );
    char *path = db_path (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    write_legacy_state (
        legacy,
        legacy_json
    );

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

    gint64 generation_id = 0;
    g_assert_true (
        atm_control_state_import_legacy_json (
            store,
            legacy,
            &generation_id,
            &error
        )
    );
    g_assert_no_error (error);

    gboolean matches = FALSE;
    g_assert_true (
        atm_control_state_generation_matches_legacy_json (
            store,
            generation_id,
            legacy,
            &matches,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (matches);
    atm_control_state_close (store);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT snapshot_seal_sha256 IS NULL FROM generation_repositories WHERE generation_id=1 AND repository_id='rmd';"
        ),
        ==,
        1
    );

    char *origin = raw_pragma_text (
        path,
        "SELECT origin FROM repository_generations WHERE generation_id=1;"
    );
    g_assert_cmpstr (
        origin,
        ==,
        "repository-state.json/schema=1"
    );
    g_free (origin);

    g_free (legacy);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
assert_invalid_legacy_import (
    const char *legacy_json
)
{
    char *root = new_temp_root (
        "atm-control-state-import-invalid-XXXXXX"
    );
    char *path = db_path (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    write_legacy_state (
        legacy,
        legacy_json
    );

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

    gint64 generation_id = 0;
    g_assert_false (
        atm_control_state_import_legacy_json (
            store,
            legacy,
            &generation_id,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_LEGACY_STATE
    );
    g_clear_error (&error);
    g_assert_cmpint (
        generation_id,
        ==,
        0
    );

    atm_control_state_close (store);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM repository_generations;"
        ),
        ==,
        0
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT active_repository_generation IS NULL FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        1
    );

    g_free (legacy);
    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_legacy_imports_fail_closed (void)
{
    assert_invalid_legacy_import (
        "{ this is not json"
    );

    assert_invalid_legacy_import (
        "{"
        "\"schema_version\":2,"
        "\"repositories\":["
        "{\"id\":\"rmd\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\"}"
        "]"
        "}"
    );

    assert_invalid_legacy_import (
        "{"
        "\"schema_version\":1,"
        "\"repositories\":["
        "{\"id\":\"rmd\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\"},"
        "{\"id\":\"rmd\",\"sha\":\"1111111111111111111111111111111111111111\",\"version\":\"0.2.0\"}"
        "]"
        "}"
    );

    assert_invalid_legacy_import (
        "{"
        "\"schema_version\":1,"
        "\"repositories\":["
        "{\"id\":\"unknown\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\"}"
        "]"
        "}"
    );
}


static char *
control_state_path_for_root (
    const char *root
)
{
    return g_build_filename (
        root,
        "control-state.sqlite3",
        NULL
    );
}

static void
assert_no_cutover_candidates (
    const char *root
)
{
    GError *error = NULL;
    GDir *directory = g_dir_open (
        root,
        0,
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (directory);

    const char *name;

    while ((name = g_dir_read_name (directory)) != NULL) {
        g_assert_false (
            g_str_has_prefix (
                name,
                ".control-state-cutover-"
            )
        );
    }

    g_dir_close (directory);
}

static void
test_atomic_cutover_imports_and_activates (void)
{
    const char *legacy_json =
        "{"
        "\"schema_version\":2,"
        "\"repositories\":["
        "{\"id\":\"rmd\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\",\"snapshot_seal_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
        "{\"id\":\"cbd\",\"sha\":\"2222222222222222222222222222222222222222\",\"version\":\"0.3.0\",\"snapshot_seal_sha256\":null}"
        "]"
        "}";

    char *root = new_temp_root (
        "atm-control-state-cutover-XXXXXX"
    );
    char *control =
        control_state_path_for_root (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    write_legacy_state (
        legacy,
        legacy_json
    );

    char *before = NULL;
    gsize before_length = 0;
    GError *error = NULL;

    g_assert_true (
        g_file_get_contents (
            legacy,
            &before,
            &before_length,
            &error
        )
    );
    g_assert_no_error (error);

    AtmControlStateCutoverDisposition disposition =
        ATM_CONTROL_STATE_CUTOVER_EMPTY;

    g_assert_true (
        atm_control_state_publish_cutover (
            control,
            legacy,
            &disposition,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        disposition,
        ==,
        ATM_CONTROL_STATE_CUTOVER_IMPORTED_LEGACY
    );
    g_assert_true (
        g_file_test (
            control,
            G_FILE_TEST_IS_REGULAR
        )
    );

    char *wal = g_strconcat (
        control,
        "-wal",
        NULL
    );
    char *shm = g_strconcat (
        control,
        "-shm",
        NULL
    );
    g_assert_false (
        g_file_test (
            wal,
            G_FILE_TEST_EXISTS
        )
    );
    g_assert_false (
        g_file_test (
            shm,
            G_FILE_TEST_EXISTS
        )
    );
    g_free (wal);
    g_free (shm);

    char *after = NULL;
    gsize after_length = 0;

    g_assert_true (
        g_file_get_contents (
            legacy,
            &after,
            &after_length,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        before_length,
        ==,
        after_length
    );
    g_assert_cmpmem (
        before,
        before_length,
        after,
        after_length
    );

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM migration_ledger WHERE migration_id='state-03a-cutover-v1';"
        ),
        ==,
        1
    );

    char *origin = raw_pragma_text (
        control,
        "SELECT applied_origin FROM migration_ledger WHERE migration_id='state-03a-cutover-v1';"
    );
    g_assert_cmpstr (
        origin,
        ==,
        "repository-state.json/schema=2"
    );
    g_free (origin);

    AtmControlStateStore *store = NULL;
    g_assert_true (
        atm_control_state_open (
            control,
            &store,
            &error
        )
    );
    g_assert_no_error (error);

    gboolean matches = FALSE;
    g_assert_true (
        atm_control_state_generation_matches_legacy_json (
            store,
            1,
            legacy,
            &matches,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (matches);
    atm_control_state_close (store);

    assert_no_cutover_candidates (root);

    AtmControlStateCutoverDisposition second =
        ATM_CONTROL_STATE_CUTOVER_EMPTY;
    g_assert_false (
        atm_control_state_publish_cutover (
            control,
            legacy,
            &second,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_CONFLICT
    );
    g_clear_error (&error);

    g_free (before);
    g_free (after);
    g_free (legacy);
    g_free (control);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_atomic_cutover_invalid_legacy_publishes_nothing (void)
{
    char *root = new_temp_root (
        "atm-control-state-cutover-invalid-XXXXXX"
    );
    char *control =
        control_state_path_for_root (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    write_legacy_state (
        legacy,
        "{ this is not json"
    );

    AtmControlStateCutoverDisposition disposition =
        ATM_CONTROL_STATE_CUTOVER_EMPTY;
    GError *error = NULL;

    g_assert_false (
        atm_control_state_publish_cutover (
            control,
            legacy,
            &disposition,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_LEGACY_STATE
    );
    g_clear_error (&error);

    g_assert_false (
        g_file_test (
            control,
            G_FILE_TEST_EXISTS
        )
    );
    assert_no_cutover_candidates (root);

    g_free (legacy);
    g_free (control);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_atomic_cutover_empty_bootstrap (void)
{
    char *root = new_temp_root (
        "atm-control-state-cutover-empty-XXXXXX"
    );
    char *control =
        control_state_path_for_root (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );

    AtmControlStateCutoverDisposition disposition =
        ATM_CONTROL_STATE_CUTOVER_IMPORTED_LEGACY;
    GError *error = NULL;

    g_assert_true (
        atm_control_state_publish_cutover (
            control,
            legacy,
            &disposition,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        disposition,
        ==,
        ATM_CONTROL_STATE_CUTOVER_EMPTY
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation IS NULL FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM repository_generations;"
        ),
        ==,
        0
    );

    char *origin = raw_pragma_text (
        control,
        "SELECT applied_origin FROM migration_ledger WHERE migration_id='state-03a-cutover-v1';"
    );
    g_assert_cmpstr (
        origin,
        ==,
        "empty-bootstrap"
    );
    g_free (origin);

    assert_no_cutover_candidates (root);

    g_free (legacy);
    g_free (control);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_atomic_cutover_never_replaces_existing_db (void)
{
    char *root = new_temp_root (
        "atm-control-state-cutover-existing-XXXXXX"
    );
    char *control =
        control_state_path_for_root (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );

    AtmControlStateStore *store = NULL;
    GError *error = NULL;
    g_assert_true (
        atm_control_state_open (
            control,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_control_state_close (store);

    gint64 application_id_before =
        raw_pragma_int64 (
            control,
            "PRAGMA application_id;"
        );

    write_legacy_state (
        legacy,
        "{"
        "\"schema_version\":1,"
        "\"repositories\":["
        "{\"id\":\"rmd\",\"sha\":\"0123456789abcdef0123456789abcdef01234567\",\"version\":\"0.1.0\"}"
        "]"
        "}"
    );

    AtmControlStateCutoverDisposition disposition =
        ATM_CONTROL_STATE_CUTOVER_EMPTY;
    g_assert_false (
        atm_control_state_publish_cutover (
            control,
            legacy,
            &disposition,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_CONFLICT
    );
    g_clear_error (&error);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "PRAGMA application_id;"
        ),
        ==,
        application_id_before
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM migration_ledger;"
        ),
        ==,
        0
    );
    assert_no_cutover_candidates (root);

    g_free (legacy);
    g_free (control);
    remove_tree_best_effort (root);
    g_free (root);
}


static void
test_runtime_mutations_are_copy_on_write (void)
{
    char *root = new_temp_root (
        "atm-control-state-cow-XXXXXX"
    );
    char *control =
        control_state_path_for_root (root);
    char *legacy = legacy_state_path (
        root,
        "repository-state.json"
    );
    GError *error = NULL;
    AtmControlStateCutoverDisposition disposition =
        ATM_CONTROL_STATE_CUTOVER_IMPORTED_LEGACY;

    g_assert_true (
        atm_control_state_publish_cutover (
            control,
            legacy,
            &disposition,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        disposition,
        ==,
        ATM_CONTROL_STATE_CUTOVER_EMPTY
    );

    const char *rmd_sha_1 =
        "1111111111111111111111111111111111111111";
    const char *rmd_sha_2 =
        "2222222222222222222222222222222222222222";
    const char *ewd_sha =
        "3333333333333333333333333333333333333333";
    const char *seal =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    g_assert_true (
        atm_control_state_set_current_values (
            control,
            "rmd",
            rmd_sha_1,
            "0.1.0",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM repository_generations;"
        ),
        ==,
        1
    );

    g_assert_true (
        atm_control_state_set_current_values (
            control,
            "ewd",
            ewd_sha,
            "0.2.0",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        2
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM generation_repositories "
            "WHERE generation_id=1;"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM generation_repositories "
            "WHERE generation_id=2;"
        ),
        ==,
        2
    );

    char *generation_1_rmd = raw_pragma_text (
        control,
        "SELECT snapshot_sha FROM generation_repositories "
        "WHERE generation_id=1 AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        generation_1_rmd,
        ==,
        rmd_sha_1
    );
    g_free (generation_1_rmd);

    g_assert_true (
        atm_control_state_set_current_values (
            control,
            "rmd",
            rmd_sha_2,
            "0.2.0",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        3
    );

    char *generation_2_rmd = raw_pragma_text (
        control,
        "SELECT snapshot_sha FROM generation_repositories "
        "WHERE generation_id=2 AND repository_id='rmd';"
    );
    char *generation_3_rmd = raw_pragma_text (
        control,
        "SELECT snapshot_sha FROM generation_repositories "
        "WHERE generation_id=3 AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        generation_2_rmd,
        ==,
        rmd_sha_1
    );
    g_assert_cmpstr (
        generation_3_rmd,
        ==,
        rmd_sha_2
    );
    g_free (generation_2_rmd);
    g_free (generation_3_rmd);

    g_assert_true (
        atm_control_state_set_snapshot_seal_values (
            control,
            "rmd",
            rmd_sha_2,
            seal,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        4
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT snapshot_seal_sha256 IS NULL "
            "FROM generation_repositories "
            "WHERE generation_id=3 AND repository_id='rmd';"
        ),
        ==,
        1
    );

    char *generation_4_seal = raw_pragma_text (
        control,
        "SELECT snapshot_seal_sha256 "
        "FROM generation_repositories "
        "WHERE generation_id=4 AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        generation_4_seal,
        ==,
        seal
    );
    g_free (generation_4_seal);

    char *generation_4_ewd = raw_pragma_text (
        control,
        "SELECT snapshot_sha FROM generation_repositories "
        "WHERE generation_id=4 AND repository_id='ewd';"
    );
    g_assert_cmpstr (
        generation_4_ewd,
        ==,
        ewd_sha
    );
    g_free (generation_4_ewd);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM repository_generations "
            "WHERE lifecycle!='COMPLETE';"
        ),
        ==,
        0
    );

    g_assert_false (
        atm_control_state_set_snapshot_seal_values (
            control,
            "rmd",
            rmd_sha_1,
            seal,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_CONFLICT
    );
    g_clear_error (&error);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        4
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM repository_generations;"
        ),
        ==,
        4
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM repository_generations "
            "WHERE lifecycle='CANDIDATE';"
        ),
        ==,
        0
    );

    g_free (legacy);
    g_free (control);
    remove_tree_best_effort (root);
    g_free (root);
}


static void
test_direct_complete_insert_is_rejected (void)
{
    char *root = new_temp_root (
        "atm-control-state-direct-complete-XXXXXX"
    );
    char *control =
        control_state_path_for_root (root);
    AtmControlStateStore *store = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_control_state_open (
            control,
            &store,
            &error
        )
    );
    g_assert_no_error (error);
    atm_control_state_close (store);

    char *message = NULL;
    int rc = raw_exec (
        control,
        "INSERT INTO repository_generations("
        "generation_id,lifecycle,origin"
        ") VALUES(1,'COMPLETE','direct-complete');",
        &message
    );

    g_assert_cmpint (
        rc,
        ==,
        SQLITE_CONSTRAINT
    );
    g_assert_nonnull (
        strstr (
            message,
            "must be completed from CANDIDATE"
        )
    );
    g_free (message);

    g_assert_cmpint (
        raw_pragma_int64 (
            control,
            "SELECT count(*) FROM repository_generations;"
        ),
        ==,
        0
    );

    g_free (control);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v1_auto_migration_preserves_state (void)
{
    char *root = new_temp_root (
        "atm-control-state-v1-migrate-XXXXXX"
    );
    char *path = db_path (root);

    create_v1_control_db (
        path,
        TRUE,
        FALSE
    );

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        1
    );
    char *before_schema = raw_pragma_text (
        path,
        "SELECT schema_id FROM installation "
        "WHERE singleton_id=1;"
    );
    g_assert_cmpstr (
        before_schema,
        ==,
        "atm-control-state/1"
    );
    g_free (before_schema);

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
    g_assert_cmpint (
        atm_control_state_schema_version (
            store
        ),
        ==,
        2
    );
    atm_control_state_close (store);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        2
    );

    char *schema_id = raw_pragma_text (
        path,
        "SELECT schema_id FROM installation "
        "WHERE singleton_id=1;"
    );
    g_assert_cmpstr (
        schema_id,
        ==,
        "atm-control-state/2"
    );
    g_free (schema_id);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        1
    );

    char *sha = raw_pragma_text (
        path,
        "SELECT snapshot_sha "
        "FROM generation_repositories "
        "WHERE generation_id=1 "
        "AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        sha,
        ==,
        "0123456789abcdef0123456789abcdef01234567"
    );
    g_free (sha);

    char *seal = raw_pragma_text (
        path,
        "SELECT snapshot_seal_sha256 "
        "FROM generation_repositories "
        "WHERE generation_id=1 "
        "AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        seal,
        ==,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    );
    g_free (seal);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM migration_ledger "
            "WHERE migration_id='v1-fixture-ledger';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM migration_ledger "
            "WHERE migration_id='state-schema-v1-to-v2' "
            "AND schema_version=2 "
            "AND applied_origin='automatic-open-migration';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM sqlite_master "
            "WHERE type='trigger' "
            "AND name LIKE 'trg_%';"
        ),
        ==,
        12
    );

    g_assert_true (
        atm_control_state_set_current_values (
            path,
            "rmd",
            "1111111111111111111111111111111111111111",
            "0.2.0",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        2
    );

    char *historical_sha = raw_pragma_text (
        path,
        "SELECT snapshot_sha "
        "FROM generation_repositories "
        "WHERE generation_id=1 "
        "AND repository_id='rmd';"
    );
    g_assert_cmpstr (
        historical_sha,
        ==,
        "0123456789abcdef0123456789abcdef01234567"
    );
    g_free (historical_sha);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_v1_invalid_state_rolls_back_migration (void)
{
    char *root = new_temp_root (
        "atm-control-state-v1-invalid-XXXXXX"
    );
    char *path = db_path (root);

    create_v1_control_db (
        path,
        TRUE,
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
        ATM_CONTROL_STATE_ERROR_INTEGRITY
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "PRAGMA user_version;"
        ),
        ==,
        1
    );

    char *schema_id = raw_pragma_text (
        path,
        "SELECT schema_id FROM installation "
        "WHERE singleton_id=1;"
    );
    g_assert_cmpstr (
        schema_id,
        ==,
        "atm-control-state/1"
    );
    g_free (schema_id);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM sqlite_master "
            "WHERE type='trigger' "
            "AND name LIKE 'trg_%';"
        ),
        ==,
        0
    );
    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT count(*) FROM migration_ledger "
            "WHERE migration_id='state-schema-v1-to-v2';"
        ),
        ==,
        0
    );

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_schema_v2_direct_sql_guards (void)
{
    char *root = new_temp_root (
        "atm-control-state-v2-guards-XXXXXX"
    );
    char *path = db_path (root);
    GError *error = NULL;

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

    g_assert_true (
        atm_control_state_set_current_values (
            path,
            "rmd",
            "0123456789abcdef0123456789abcdef01234567",
            "0.1.0",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    char *message = NULL;
    g_assert_cmpint (
        raw_exec (
            path,
            "UPDATE generation_repositories "
            "SET repository_version='tampered' "
            "WHERE generation_id=1 "
            "AND repository_id='rmd';",
            &message
        ),
        ==,
        SQLITE_CONSTRAINT
    );
    g_assert_nonnull (
        strstr (
            message,
            "generation rows are immutable"
        )
    );
    g_clear_pointer (&message, g_free);

    g_assert_cmpint (
        raw_exec (
            path,
            "DELETE FROM generation_repositories "
            "WHERE generation_id=1 "
            "AND repository_id='rmd';",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "INSERT INTO generation_repositories("
            "generation_id,repository_id,snapshot_sha,"
            "repository_version,snapshot_seal_sha256"
            ") VALUES("
            "1,'ewd',"
            "'1111111111111111111111111111111111111111',"
            "'0.1.0',NULL"
            ");",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "UPDATE repository_generations "
            "SET origin='tampered' "
            "WHERE generation_id=1;",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "DELETE FROM repository_generations "
            "WHERE generation_id=1;",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "INSERT INTO repository_generations("
            "generation_id,lifecycle,origin"
            ") VALUES(2,'COMPLETE','direct-complete');",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    sqlite3 *db = NULL;
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
            "BEGIN IMMEDIATE;"
            "INSERT INTO repository_generations("
            "generation_id,lifecycle,origin"
            ") VALUES(2,'CANDIDATE','candidate-test');",
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "UPDATE active_state "
            "SET active_repository_generation=2 "
            "WHERE singleton_id=1;",
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_close (db);

    g_assert_true (
        atm_control_state_set_current_values (
            path,
            "ewd",
            "2222222222222222222222222222222222222222",
            "0.2.0",
            NULL,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        raw_pragma_int64 (
            path,
            "SELECT active_repository_generation "
            "FROM active_state WHERE singleton_id=1;"
        ),
        ==,
        2
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "UPDATE active_state "
            "SET active_repository_generation=1 "
            "WHERE singleton_id=1;",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "UPDATE migration_ledger "
            "SET applied_origin='tampered' "
            "WHERE migration_id='state-03a-cutover-v1';",
            NULL
        ),
        ==,
        SQLITE_OK
    );

    // Empty update above may touch zero rows. Create a ledger row and prove append-only.
    g_assert_cmpint (
        raw_exec (
            path,
            "INSERT INTO migration_ledger("
            "migration_id,schema_version,applied_origin"
            ") VALUES('guard-test',2,'test');",
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        raw_exec (
            path,
            "UPDATE migration_ledger "
            "SET applied_origin='tampered' "
            "WHERE migration_id='guard-test';",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );
    g_assert_cmpint (
        raw_exec (
            path,
            "DELETE FROM migration_ledger "
            "WHERE migration_id='guard-test';",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "DELETE FROM installation "
            "WHERE singleton_id=1;",
            NULL
        ),
        ==,
        SQLITE_CONSTRAINT
    );

    g_assert_cmpint (
        raw_exec (
            path,
            "INSERT INTO repository_generations("
            "generation_id,lifecycle,origin"
            ") VALUES(99,'CANDIDATE','persisted-candidate');",
            NULL
        ),
        ==,
        SQLITE_OK
    );

    store = NULL;
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
        ATM_CONTROL_STATE_ERROR_INTEGRITY
    );
    g_assert_null (store);
    g_clear_error (&error);

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/control-state/sqlite-security-floor",
        test_sqlite_security_floor
    );
    g_test_add_func (
        "/control-state/nofollow-symlink",
        test_control_state_symlink_is_rejected
    );
    g_test_add_func (
        "/control-state/reject-read-only-authority",
        test_read_only_control_state_is_rejected
    );
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
    g_test_add_func (
        "/control-state/legacy-v2-import-equivalence",
        test_legacy_v2_import_and_equivalence
    );
    g_test_add_func (
        "/control-state/equivalence-missing-generation",
        test_equivalence_rejects_missing_generation
    );
    g_test_add_func (
        "/control-state/legacy-v1-null-seal",
        test_legacy_v1_import_preserves_null_seal
    );
    g_test_add_func (
        "/control-state/legacy-invalid-fail-closed",
        test_invalid_legacy_imports_fail_closed
    );
    g_test_add_func (
        "/control-state/cutover-import-activate",
        test_atomic_cutover_imports_and_activates
    );
    g_test_add_func (
        "/control-state/cutover-invalid-no-publish",
        test_atomic_cutover_invalid_legacy_publishes_nothing
    );
    g_test_add_func (
        "/control-state/cutover-empty-bootstrap",
        test_atomic_cutover_empty_bootstrap
    );
    g_test_add_func (
        "/control-state/cutover-no-replace",
        test_atomic_cutover_never_replaces_existing_db
    );
    g_test_add_func (
        "/control-state/runtime-copy-on-write",
        test_runtime_mutations_are_copy_on_write
    );
    g_test_add_func (
        "/control-state/direct-complete-insert-rejected",
        test_direct_complete_insert_is_rejected
    );
    g_test_add_func (
        "/control-state/schema-v1-auto-migration",
        test_v1_auto_migration_preserves_state
    );
    g_test_add_func (
        "/control-state/schema-v1-invalid-rollback",
        test_v1_invalid_state_rolls_back_migration
    );
    g_test_add_func (
        "/control-state/schema-v2-direct-sql-guards",
        test_schema_v2_direct_sql_guards
    );

    return g_test_run ();
}
