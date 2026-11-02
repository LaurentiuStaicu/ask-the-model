#include "pres09_sidecar_tamper.h"
#include "conversation_store.h"

#include <sqlite3.h>

static gboolean
set_sqlite_failure (
    GError **error,
    sqlite3 *database,
    const gchar *operation
) {
    g_set_error (
        error,
        G_IO_ERROR,
        G_IO_ERROR_FAILED,
        "%s: %s",
        operation,
        database != NULL ? sqlite3_errmsg (database) : "SQLite database unavailable"
    );
    return FALSE;
}

gboolean
atm_pres09_tamper_first_persistent_v4_sidecar (
    const gchar *database_path,
    const gchar *conversation_id,
    GError **error
) {
    static const gchar forged_id[] =
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    sqlite3 *database = NULL;
    sqlite3_stmt *select_statement = NULL;
    sqlite3_stmt *update_statement = NULL;
    gchar *message_id = NULL;
    gint chart_ordinal = 0;
    gboolean transaction_open = FALSE;
    gboolean success = FALSE;
    gint rc = SQLITE_OK;

    if (database_path == NULL || database_path[0] == '\0' ||
        conversation_id == NULL || conversation_id[0] == '\0') {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_ARGUMENT,
            "Persistent sidecar tamper received an empty database path or conversation id."
        );
        return FALSE;
    }

    rc = sqlite3_open_v2 (
        database_path,
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not open persistent conversation database");
        goto out;
    }

    rc = sqlite3_exec (database, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not begin persistent sidecar tamper transaction");
        goto out;
    }
    transaction_open = TRUE;

    rc = sqlite3_prepare_v2 (
        database,
        "SELECT p.message_id,p.chart_ordinal,p.presentation_spec_id "
        "FROM chart_presentations_v4 p "
        "JOIN messages m ON m.message_id=p.message_id "
        "WHERE m.conversation_id=?1 "
        "AND p.series_id='energy.energy-institute.global-total-energy-supply' "
        "ORDER BY m.sequence_no ASC,p.chart_ordinal ASC LIMIT 1;",
        -1,
        &select_statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not prepare persistent EI sidecar selection");
        goto out;
    }
    sqlite3_bind_text (
        select_statement,
        1,
        conversation_id,
        -1,
        SQLITE_TRANSIENT
    );
    rc = sqlite3_step (select_statement);
    if (rc != SQLITE_ROW) {
        set_sqlite_failure (error, database, "No persisted EI v4 sidecar was found to tamper");
        goto out;
    }

    const gchar *stored_message_id =
        (const gchar *) sqlite3_column_text (select_statement, 0);
    const gchar *stored_presentation_id =
        (const gchar *) sqlite3_column_text (select_statement, 2);
    if (stored_message_id == NULL ||
        stored_presentation_id == NULL ||
        g_strcmp0 (stored_presentation_id, forged_id) == 0) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Persisted EI sidecar identity was missing or already forged."
        );
        goto out;
    }
    message_id = g_strdup (stored_message_id);
    chart_ordinal = sqlite3_column_int (select_statement, 1);
    sqlite3_finalize (select_statement);
    select_statement = NULL;

    rc = sqlite3_prepare_v2 (
        database,
        "UPDATE chart_presentations_v4 "
        "SET presentation_spec_id=?1 "
        "WHERE message_id=?2 AND chart_ordinal=?3;",
        -1,
        &update_statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not prepare persistent EI sidecar mutation");
        goto out;
    }
    sqlite3_bind_text (update_statement, 1, forged_id, -1, SQLITE_STATIC);
    sqlite3_bind_text (update_statement, 2, message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (update_statement, 3, chart_ordinal);
    rc = sqlite3_step (update_statement);
    if (rc != SQLITE_DONE || sqlite3_changes (database) != 1) {
        set_sqlite_failure (error, database, "Persistent EI sidecar mutation did not update exactly one row");
        goto out;
    }
    sqlite3_finalize (update_statement);
    update_statement = NULL;

    rc = sqlite3_exec (database, "COMMIT;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not commit persistent EI sidecar mutation");
        goto out;
    }
    transaction_open = FALSE;
    success = TRUE;

out:
    if (select_statement != NULL) {
        sqlite3_finalize (select_statement);
    }
    if (update_statement != NULL) {
        sqlite3_finalize (update_statement);
    }
    if (transaction_open) {
        sqlite3_exec (database, "ROLLBACK;", NULL, NULL, NULL);
    }
    g_free (message_id);
    if (database != NULL) {
        sqlite3_close (database);
    }
    return success;
}


static gboolean
pres09_query_int64 (
    sqlite3 *database,
    const gchar *sql,
    gint64 *out_value,
    GError **error
) {
    sqlite3_stmt *statement = NULL;
    gint rc = sqlite3_prepare_v2 (database, sql, -1, &statement, NULL);
    if (rc != SQLITE_OK) {
        if (statement != NULL) sqlite3_finalize (statement);
        return set_sqlite_failure (error, database, "Could not prepare PRES-09 scalar query");
    }
    rc = sqlite3_step (statement);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        return set_sqlite_failure (error, database, "PRES-09 scalar query returned no row");
    }
    *out_value = sqlite3_column_int64 (statement, 0);
    rc = sqlite3_step (statement);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize (statement);
        return set_sqlite_failure (error, database, "PRES-09 scalar query returned more than one row");
    }
    sqlite3_finalize (statement);
    return TRUE;
}

static gboolean
pres09_query_text (
    sqlite3 *database,
    const gchar *sql,
    gchar **out_value,
    GError **error
) {
    sqlite3_stmt *statement = NULL;
    gint rc = sqlite3_prepare_v2 (database, sql, -1, &statement, NULL);
    if (rc != SQLITE_OK) {
        if (statement != NULL) sqlite3_finalize (statement);
        return set_sqlite_failure (error, database, "Could not prepare PRES-09 text query");
    }
    rc = sqlite3_step (statement);
    if (rc != SQLITE_ROW || sqlite3_column_type (statement, 0) == SQLITE_NULL) {
        sqlite3_finalize (statement);
        return set_sqlite_failure (error, database, "PRES-09 text query returned no value");
    }
    const guchar *value = sqlite3_column_text (statement, 0);
    *out_value = g_strdup ((const gchar *) value);
    rc = sqlite3_step (statement);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_clear_pointer (out_value, g_free);
        return set_sqlite_failure (error, database, "PRES-09 text query returned more than one row");
    }
    sqlite3_finalize (statement);
    return TRUE;
}

static gboolean
pres09_exec_sql (
    sqlite3 *database,
    const gchar *sql,
    const gchar *operation,
    GError **error
) {
    gchar *message = NULL;
    gint rc = sqlite3_exec (database, sql, NULL, NULL, &message);
    if (rc == SQLITE_OK) {
        sqlite3_free (message);
        return TRUE;
    }
    g_set_error (
        error,
        G_IO_ERROR,
        G_IO_ERROR_FAILED,
        "%s: %s",
        operation,
        message != NULL ? message : sqlite3_errmsg (database)
    );
    sqlite3_free (message);
    return FALSE;
}

gboolean
atm_pres09_create_v5_legacy_history_fixture (
    const gchar *source_database_path,
    const gchar *target_database_path,
    const gchar *conversation_id,
    GError **error
) {
    sqlite3 *database = NULL;
    sqlite3_stmt *attach_statement = NULL;
    GBytes *schema_bytes = NULL;
    gchar *schema_sql = NULL;
    gchar *copy_sql = NULL;
    gchar *quoted_conversation_id = NULL;
    gboolean transaction_open = FALSE;
    gboolean success = FALSE;
    gint64 source_application_id = 0;
    gint64 source_version = 0;
    gchar *source_schema_id = NULL;
    gint64 source_chart_count = 0;
    gint64 source_series_count = 0;
    gint64 target_chart_count = 0;
    gint64 target_series_count = 0;
    gint64 foreign_key_count = 0;
    gchar *integrity = NULL;
    GError *resource_error = NULL;

    if (source_database_path == NULL || source_database_path[0] == '\0' ||
        target_database_path == NULL || target_database_path[0] == '\0' ||
        conversation_id == NULL || conversation_id[0] == '\0') {
        g_set_error_literal (
            error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "PRES-09 v5 fixture requires source/target database paths and a conversation id."
        );
        return FALSE;
    }
    if (g_file_test (target_database_path, G_FILE_TEST_EXISTS)) {
        g_set_error_literal (
            error, G_IO_ERROR, G_IO_ERROR_EXISTS,
            "PRES-09 v5 fixture target already exists; refusing to overwrite it."
        );
        return FALSE;
    }

    schema_bytes = g_resources_lookup_data (
        "/io/github/laurentiustaicu/ask_the_model/schemas/conversation-store-v5.sql",
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &resource_error
    );
    if (schema_bytes == NULL) {
        g_set_error (
            error, G_IO_ERROR, G_IO_ERROR_FAILED,
            "Could not load the v5 conversation-store fixture schema: %s",
            resource_error != NULL ? resource_error->message : "unknown resource error"
        );
        g_clear_error (&resource_error);
        goto out;
    }
    gsize schema_size = 0;
    const gchar *schema_data = g_bytes_get_data (schema_bytes, &schema_size);
    schema_sql = g_strndup (schema_data, schema_size);

    gint rc = sqlite3_open_v2 (
        target_database_path,
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not create the PRES-09 v5 fixture database");
        goto out;
    }

    if (!pres09_exec_sql (database, "PRAGMA foreign_keys=ON;", "Could not enable fixture foreign keys", error) ||
        !pres09_exec_sql (database, schema_sql, "Could not create the v5 fixture schema", error) ||
        !pres09_exec_sql (database,
            "PRAGMA application_id=1096041265; PRAGMA user_version=5;",
            "Could not identify the v5 fixture database",
            error))
        goto out;

    rc = sqlite3_prepare_v2 (
        database,
        "ATTACH DATABASE ?1 AS source;",
        -1,
        &attach_statement,
        NULL
    );
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not prepare pinned v6 source attachment");
        goto out;
    }
    sqlite3_bind_text (attach_statement, 1, source_database_path, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step (attach_statement);
    sqlite3_finalize (attach_statement);
    attach_statement = NULL;
    if (rc != SQLITE_DONE) {
        set_sqlite_failure (error, database, "Could not attach the live v6 conversation database");
        goto out;
    }

    if (!pres09_query_int64 (database, "PRAGMA source.application_id;", &source_application_id, error) ||
        !pres09_query_int64 (database, "PRAGMA source.user_version;", &source_version, error) ||
        !pres09_query_text (database,
            "SELECT schema_id FROM source.installation WHERE singleton_id=1;",
            &source_schema_id, error))
        goto out;
    if (source_application_id != ATM_CONVERSATION_STORE_APPLICATION_ID ||
        source_version != ATM_CONVERSATION_STORE_SCHEMA_VERSION ||
        g_strcmp0 (source_schema_id, ATM_CONVERSATION_STORE_SCHEMA_ID) != 0) {
        g_set_error (
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "PRES-09 source store identity/version is not AtM schema v6 (application_id=%" G_GINT64_FORMAT ", user_version=%" G_GINT64_FORMAT ", schema_id=%s).",
            source_application_id, source_version,
            source_schema_id != NULL ? source_schema_id : "unavailable"
        );
        goto out;
    }

    quoted_conversation_id = sqlite3_mprintf ("%Q", conversation_id);
    if (quoted_conversation_id == NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Could not quote the PRES-09 conversation id.");
        goto out;
    }

    copy_sql = g_strdup_printf (
        "SELECT count(*) FROM source.message_charts c "
        "JOIN source.messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    if (!pres09_query_int64 (database, copy_sql, &source_chart_count, error))
        goto out;
    g_free (copy_sql);
    copy_sql = g_strdup_printf (
        "SELECT count(*) FROM source.chart_series s "
        "JOIN source.messages m ON m.message_id=s.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    if (!pres09_query_int64 (database, copy_sql, &source_series_count, error))
        goto out;
    g_free (copy_sql);
    copy_sql = NULL;
    if (source_chart_count != 4 || source_series_count != 4) {
        g_set_error (
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "PRES-09 live fixture expected four source chart recipes/series, found %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT ".",
            source_chart_count, source_series_count
        );
        goto out;
    }

    if (!pres09_exec_sql (database, "BEGIN IMMEDIATE;", "Could not begin v5 fixture copy transaction", error))
        goto out;
    transaction_open = TRUE;

    copy_sql = g_strdup_printf (
        "INSERT INTO conversations("
        "conversation_id,title,created_at_us,updated_at_us,model_name,model_digest,"
        "repository_generation_id,archived,open_on_startup) "
        "SELECT conversation_id,title,created_at_us,updated_at_us,model_name,model_digest,"
        "repository_generation_id,archived,open_on_startup "
        "FROM source.conversations WHERE conversation_id=%s;"
        "INSERT INTO conversation_repositories("
        "conversation_id,repository_id,repository_version,snapshot_sha) "
        "SELECT conversation_id,repository_id,repository_version,snapshot_sha "
        "FROM source.conversation_repositories WHERE conversation_id=%s;"
        "INSERT INTO messages("
        "message_id,conversation_id,sequence_no,turn_no,role,provider_content,display_content,grounded,created_at_us) "
        "SELECT message_id,conversation_id,sequence_no,turn_no,role,provider_content,display_content,grounded,created_at_us "
        "FROM source.messages WHERE conversation_id=%s;"
        "INSERT INTO citations("
        "message_id,ordinal,label,repository_id,repository_version,snapshot_sha,logical_source_id,"
        "source_path,locator,title,excerpt,immutable_permalink) "
        "SELECT c.message_id,c.ordinal,c.label,c.repository_id,c.repository_version,c.snapshot_sha,"
        "c.logical_source_id,c.source_path,c.locator,c.title,c.excerpt,c.immutable_permalink "
        "FROM source.citations c JOIN source.messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id=%s;"
        "INSERT INTO message_charts("
        "message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile) "
        "SELECT c.message_id,c.ordinal,c.chart_schema,c.chart_spec_id,c.chart_kind,c.reconstruction_profile "
        "FROM source.message_charts c JOIN source.messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id=%s;"
        "INSERT INTO chart_series("
        "message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,scientific_id,qualified_id,"
        "repository_id,repository_version,snapshot_sha,source_path) "
        "SELECT s.message_id,s.chart_ordinal,s.series_ordinal,s.series_profile,s.admission_profile,"
        "s.scientific_id,s.qualified_id,s.repository_id,s.repository_version,s.snapshot_sha,s.source_path "
        "FROM source.chart_series s JOIN source.messages m ON m.message_id=s.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id, quoted_conversation_id, quoted_conversation_id,
        quoted_conversation_id, quoted_conversation_id, quoted_conversation_id
    );
    if (!pres09_exec_sql (database, copy_sql, "Could not copy real History rows into the v5 fixture", error))
        goto out;
    g_free (copy_sql);
    copy_sql = NULL;

    if (!pres09_exec_sql (database, "COMMIT;", "Could not commit v5 History fixture", error))
        goto out;
    transaction_open = FALSE;

    copy_sql = g_strdup_printf (
        "SELECT count(*) FROM message_charts c "
        "JOIN messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    if (!pres09_query_int64 (database, copy_sql, &target_chart_count, error))
        goto out;
    g_free (copy_sql);
    copy_sql = g_strdup_printf (
        "SELECT count(*) FROM chart_series s "
        "JOIN messages m ON m.message_id=s.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    if (!pres09_query_int64 (database, copy_sql, &target_series_count, error))
        goto out;
    g_free (copy_sql);
    copy_sql = NULL;

    if (target_chart_count != source_chart_count || target_series_count != source_series_count) {
        g_set_error (
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "PRES-09 v5 fixture row counts changed during copy (charts=%" G_GINT64_FORMAT "/%" G_GINT64_FORMAT ", series=%" G_GINT64_FORMAT "/%" G_GINT64_FORMAT ").",
            target_chart_count, source_chart_count, target_series_count, source_series_count
        );
        goto out;
    }
    if (!pres09_query_int64 (database, "SELECT count(*) FROM pragma_foreign_key_check;", &foreign_key_count, error) ||
        !pres09_query_text (database, "PRAGMA integrity_check;", &integrity, error))
        goto out;
    if (foreign_key_count != 0) {
        g_set_error (
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "PRES-09 v5 fixture has %" G_GINT64_FORMAT " foreign-key violations.",
            foreign_key_count
        );
        goto out;
    }
    if (g_strcmp0 (integrity, "ok") != 0) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "PRES-09 v5 fixture failed SQLite integrity_check.");
        goto out;
    }

    success = TRUE;
    g_debug ("PRES-09 created populated v5 legacy fixture with four real source chart recipes.");
out:
    if (transaction_open && database != NULL)
        sqlite3_exec (database, "ROLLBACK;", NULL, NULL, NULL);
    if (attach_statement != NULL)
        sqlite3_finalize (attach_statement);
    g_free (integrity);
    g_free (source_schema_id);
    g_free (copy_sql);
    if (quoted_conversation_id != NULL)
        sqlite3_free (quoted_conversation_id);
    g_free (schema_sql);
    g_clear_pointer (&schema_bytes, g_bytes_unref);
    if (database != NULL)
        sqlite3_close (database);
    return success;
}

gboolean
atm_pres09_verify_migrated_v5_legacy_history (
    const gchar *database_path,
    const gchar *conversation_id,
    GError **error
) {
    sqlite3 *database = NULL;
    gchar *schema_id = NULL;
    gchar *integrity = NULL;
    gchar *quoted_conversation_id = NULL;
    gchar *sql = NULL;
    gint64 application_id = 0;
    gint64 user_version = 0;
    gint64 archived = 0;
    gint64 chart_count = 0;
    gint64 series_count = 0;
    gint64 sidecar_count = 0;
    gint64 marked_count = 0;
    gint64 foreign_key_count = 0;
    gboolean success = FALSE;

    g_printerr ("PRES09-VERIFY enter db=%s conversation=%s\n", database_path != NULL ? database_path : "(null)", conversation_id != NULL ? conversation_id : "(null)");
    if (database_path == NULL || database_path[0] == '\0' ||
        conversation_id == NULL || conversation_id[0] == '\0') {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "PRES-09 migrated History verification received empty identity.");
        return FALSE;
    }

    gint rc = sqlite3_open_v2 (
        database_path, &database, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL
    );
    g_printerr ("PRES09-VERIFY sqlite open rc=%d\n", rc);
    if (rc != SQLITE_OK) {
        set_sqlite_failure (error, database, "Could not reopen migrated v6 History database");
        goto out;
    }

    g_printerr ("PRES09-VERIFY opened database\n");
    if (!pres09_query_int64 (database, "PRAGMA application_id;", &application_id, error) ||
        !pres09_query_int64 (database, "PRAGMA user_version;", &user_version, error) ||
        !pres09_query_text (database,
            "SELECT schema_id FROM installation WHERE singleton_id=1;",
            &schema_id, error))
        goto out;

    if (application_id != ATM_CONVERSATION_STORE_APPLICATION_ID ||
        user_version != ATM_CONVERSATION_STORE_SCHEMA_VERSION ||
        g_strcmp0 (schema_id, ATM_CONVERSATION_STORE_SCHEMA_ID) != 0) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Application did not migrate the populated v5 store to schema v6.");
        goto out;
    }

    g_printerr ("PRES09-VERIFY identity app=%" G_GINT64_FORMAT " version=%" G_GINT64_FORMAT " schema=%s\n", application_id, user_version, schema_id);
    quoted_conversation_id = sqlite3_mprintf ("%Q", conversation_id);
    if (quoted_conversation_id == NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Could not quote the migrated conversation id.");
        goto out;
    }
    sql = g_strdup_printf (
        "SELECT archived FROM conversations WHERE conversation_id=%s;",
        quoted_conversation_id
    );
    g_printerr ("PRES09-VERIFY query archived\n");
    if (!pres09_query_int64 (database, sql, &archived, error))
        goto out;
    g_free (sql);
    sql = g_strdup_printf (
        "SELECT count(*) FROM message_charts c JOIN messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    g_printerr ("PRES09-VERIFY query charts\n");
    if (!pres09_query_int64 (database, sql, &chart_count, error))
        goto out;
    g_free (sql);
    sql = g_strdup_printf (
        "SELECT count(*) FROM chart_series s JOIN messages m ON m.message_id=s.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    g_printerr ("PRES09-VERIFY query series\n");
    if (!pres09_query_int64 (database, sql, &series_count, error))
        goto out;
    g_free (sql);
    sql = g_strdup_printf (
        "SELECT count(*) FROM message_charts c JOIN messages m ON m.message_id=c.message_id "
        "WHERE m.conversation_id=%s AND c.presentation_schema IS NOT NULL;",
        quoted_conversation_id
    );
    g_printerr ("PRES09-VERIFY query markers\n");
    if (!pres09_query_int64 (database, sql, &marked_count, error))
        goto out;
    g_free (sql);
    sql = g_strdup_printf (
        "SELECT count(*) FROM chart_presentations_v4 p JOIN messages m ON m.message_id=p.message_id "
        "WHERE m.conversation_id=%s;",
        quoted_conversation_id
    );
    g_printerr ("PRES09-VERIFY query sidecars\n");
    if (!pres09_query_int64 (database, sql, &sidecar_count, error))
        goto out;
    g_free (sql);
    g_printerr ("PRES09-VERIFY query fk/integrity\n");
    if (!pres09_query_int64 (database, "SELECT count(*) FROM pragma_foreign_key_check;", &foreign_key_count, error) ||
        !pres09_query_text (database, "PRAGMA integrity_check;", &integrity, error))
        goto out;

    if (archived != 1 || chart_count != 4 || series_count != 4 ||
        marked_count != 0 || sidecar_count != 0 ||
        foreign_key_count != 0 || g_strcmp0 (integrity, "ok") != 0) {
        g_set_error (
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "Migrated v5 History fixture invariants failed (archived=%" G_GINT64_FORMAT
            ", charts=%" G_GINT64_FORMAT ", series=%" G_GINT64_FORMAT
            ", markers=%" G_GINT64_FORMAT ", sidecars=%" G_GINT64_FORMAT
            ", foreign_keys=%" G_GINT64_FORMAT ", integrity=%s).",
            archived, chart_count, series_count, marked_count, sidecar_count,
            foreign_key_count, integrity != NULL ? integrity : "unavailable"
        );
        goto out;
    }

    g_printerr ("PRES09-VERIFY rows archived=%" G_GINT64_FORMAT " charts=%" G_GINT64_FORMAT " series=%" G_GINT64_FORMAT " markers=%" G_GINT64_FORMAT " sidecars=%" G_GINT64_FORMAT " fk=%" G_GINT64_FORMAT " integrity=%s\n", archived, chart_count, series_count, marked_count, sidecar_count, foreign_key_count, integrity != NULL ? integrity : "(null)");
    success = TRUE;
out:
    g_free (schema_id);
    g_free (integrity);
    g_free (sql);
    if (quoted_conversation_id != NULL)
        sqlite3_free (quoted_conversation_id);
    if (database != NULL) {
        g_printerr ("PRES09-VERIFY close success=%d\n", success);
        sqlite3_close (database);
    }
    return success;
}
