#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

static gboolean
exec_sql (sqlite3 *db, const char *sql)
{
    char *error = NULL;
    int rc = sqlite3_exec (db, sql, NULL, NULL, &error);

    if (rc != SQLITE_OK) {
        g_printerr (
            "SQLite error %d: %s\nSQL: %s\n",
            rc,
            error != NULL ? error : sqlite3_errmsg (db),
            sql
        );
        sqlite3_free (error);
        return FALSE;
    }

    return TRUE;
}

static int
query_single_int (sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;
    int value = -1;

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
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);
    value = sqlite3_column_int (statement, 0);
    sqlite3_finalize (statement);
    return value;
}

static char *
query_single_text (sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;
    char *value;

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
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);

    const unsigned char *text = sqlite3_column_text (
        statement,
        0
    );
    value = g_strdup ((const char *) text);
    sqlite3_finalize (statement);
    return value;
}

static void
test_schema_and_integrity (void)
{
    const char *schema_path = g_getenv ("ATM_RETRIEVAL_SCHEMA");
    GError *error = NULL;
    char *schema = NULL;
    gsize schema_length = 0;
    char *temp_dir = NULL;
    char *database_path = NULL;
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;

    g_assert_nonnull (schema_path);
    g_assert_true (
        g_file_get_contents (
            schema_path,
            &schema,
            &schema_length,
            &error
        )
    );
    g_assert_no_error (error);

    temp_dir = g_dir_make_tmp (
        "atm-retrieval-schema-test-XXXXXX",
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (temp_dir);

    database_path = g_build_filename (
        temp_dir,
        "index.sqlite",
        NULL
    );

    g_assert_cmpint (
        sqlite3_open (database_path, &db),
        ==,
        SQLITE_OK
    );
    g_assert_true (exec_sql (db, schema));

    g_assert_cmpint (
        query_single_int (db, "PRAGMA user_version;"),
        ==,
        1
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO snapshot_metadata("
            "id, repository_id, repository_version, "
            "snapshot_sha, manifest_schema_version, "
            "manifest_sha256, created_at_utc"
            ") VALUES("
            "1, 'ewd', '0.1.0', "
            "'0123456789abcdef0123456789abcdef01234567', "
            "1, "
            "'0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef', "
            "'2026-09-20T00:00:00Z'"
            ");"
        )
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO source_files("
            "id, path, sha256, byte_size, media_type, "
            "logical_source_id"
            ") VALUES("
            "1, 'STATUS.md', "
            "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', "
            "42, 'text/markdown', 'ewd:file:STATUS.md'"
            ");"
        )
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO source_roles(source_id, role) "
            "VALUES(1, 'canonical');"
            "INSERT INTO source_roles(source_id, role) "
            "VALUES(1, 'evidence');"
        )
    );

    g_assert_cmpint (
        query_single_int (
            db,
            "SELECT count(*) FROM source_roles "
            "WHERE source_id = 1;"
        ),
        ==,
        2
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO document_sections("
            "id, source_id, ordinal, heading_path, locator, "
            "logical_source_id, title, body"
            ") VALUES("
            "1, 1, 0, 'Status', 'heading:Status', "
            "'ewd:section:STATUS.md:Status', "
            "'Stare curentă', 'fără pădure'"
            ");"
        )
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO structured_entities("
            "id, source_id, entity_type, native_id, "
            "logical_source_id, locator, label, payload_json"
            ") VALUES("
            "1, 1, 'variable', 'food_per_capita', "
            "'ewd:entity:variable:food_per_capita', "
            "'json:/variables/food_per_capita', "
            "'Food per capita', '{}'"
            ");"
        )
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO structured_relations("
            "id, source_id, relation_type, native_id, "
            "logical_source_id, locator, "
            "from_logical_source_id, to_logical_source_id, "
            "payload_json"
            ") VALUES("
            "1, 1, 'influence', 'REL.TEST', "
            "'ewd:entity:relation:REL.TEST', "
            "'json:/relations/0', "
            "'ewd:entity:variable:food_per_capita', "
            "'ewd:entity:variable:population', '{}'"
            ");"
        )
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO datasets("
            "id, source_id, native_id, logical_source_id, "
            "locator, title, metadata_json"
            ") VALUES("
            "1, 1, 'food_series', "
            "'ewd:dataset:food_series', "
            "'csv:data/food.csv', 'Food series', '{}'"
            ");"
            "INSERT INTO dataset_rows("
            "id, dataset_id, ordinal, row_key, locator, "
            "payload_json, search_text"
            ") VALUES("
            "1, 1, 0, '2025', 'row:2025', "
            "'{\"year\":2025}', '2025 food'"
            ");"
        )
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO search_fts("
            "evidence_kind, evidence_id, logical_source_id, "
            "title, body"
            ") VALUES("
            "'section', 1, "
            "'ewd:section:STATUS.md:Status', "
            "'Stare curentă', 'fără pădure'"
            ");"
        )
    );

    g_assert_cmpint (
        query_single_int (
            db,
            "SELECT count(*) FROM search_fts "
            "WHERE search_fts MATCH 'fara';"
        ),
        ==,
        1
    );

    g_assert_true (
        exec_sql (
            db,
            "INSERT INTO search_fts(search_fts) "
            "VALUES('integrity-check');"
        )
    );

    char *integrity = query_single_text (
        db,
        "PRAGMA integrity_check;"
    );
    g_assert_cmpstr (integrity, ==, "ok");
    g_free (integrity);

    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            "PRAGMA foreign_key_check;",
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
        SQLITE_DONE
    );
    sqlite3_finalize (statement);
    statement = NULL;

    g_assert_false (
        exec_sql (
            db,
            "INSERT INTO source_roles(source_id, role) "
            "VALUES(999, 'canonical');"
        )
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (
            database_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_assert_true (
        exec_sql (
            db,
            "PRAGMA trusted_schema = OFF;"
            "PRAGMA query_only = ON;"
        )
    );

    char *sha = query_single_text (
        db,
        "SELECT snapshot_sha FROM snapshot_metadata "
        "WHERE id = 1;"
    );
    g_assert_cmpstr (
        sha,
        ==,
        "0123456789abcdef0123456789abcdef01234567"
    );
    g_free (sha);

    g_assert_false (
        exec_sql (
            db,
            "INSERT INTO source_files("
            "path, sha256, byte_size, media_type, "
            "logical_source_id"
            ") VALUES("
            "'x', "
            "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb', "
            "1, 'text/plain', 'ewd:file:x'"
            ");"
        )
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);

    g_remove (database_path);
    g_rmdir (temp_dir);
    g_free (database_path);
    g_free (temp_dir);
    g_free (schema);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-index/schema-and-integrity",
        test_schema_and_integrity
    );

    return g_test_run ();
}
