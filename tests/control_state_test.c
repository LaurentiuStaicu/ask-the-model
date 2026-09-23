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

    return g_test_run ();
}
