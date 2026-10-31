#include "pres09_sidecar_tamper.h"

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
