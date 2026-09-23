#include "control_state.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include <errno.h>
#include <string.h>

#define ATM_CONTROL_STATE_SCHEMA_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/control-state-v1.sql"

struct AtmControlStateStore {
    sqlite3 *db;
    char *path;
    gint64 application_id;
    gint schema_version;
};

GQuark
atm_control_state_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-control-state-error-quark"
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
    AtmControlStateError code,
    const char *context
)
{
    g_set_error (
        error,
        ATM_CONTROL_STATE_ERROR,
        code,
        "%s: %s",
        context,
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
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Control-state SQL failed (%d): %s",
            rc,
            message != NULL
                ? message
                : sqlite3_errmsg (db)
        );
        sqlite3_free (message);
        return FALSE;
    }

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

    if (sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not prepare control-state integer query"
        );
        return FALSE;
    }

    int rc = sqlite3_step (statement);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Control-state integer query returned no row"
        );
        return FALSE;
    }

    *out_value = sqlite3_column_int64 (
        statement,
        0
    );

    if (sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Control-state integer query returned more than one row."
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

    if (sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not prepare control-state text query"
        );
        return FALSE;
    }

    if (sqlite3_step (statement) != SQLITE_ROW) {
        sqlite3_finalize (statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Control-state text query returned no row"
        );
        return FALSE;
    }

    const unsigned char *text = sqlite3_column_text (
        statement,
        0
    );
    *out_value = g_strdup (
        text != NULL
            ? (const char *) text
            : ""
    );

    if (sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_clear_pointer (out_value, g_free);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Control-state text query returned more than one row."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
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
            "SELECT count(*) "
            "FROM sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%';",
            &count,
            error
        )) {
        return FALSE;
    }

    *out_has_objects = count != 0;
    return TRUE;
}

static gboolean
load_schema_sql (
    char **out_sql,
    GError **error
)
{
    GError *resource_error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        ATM_CONTROL_STATE_SCHEMA_RESOURCE,
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &resource_error
    );

    if (bytes == NULL) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Could not load control-state schema resource: %s",
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
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state schema resource is empty."
        );
        return FALSE;
    }

    *out_sql = g_strndup (data, size);
    g_bytes_unref (bytes);
    return TRUE;
}

static gboolean
configure_connection (
    sqlite3 *db,
    GError **error
)
{
    if (sqlite3_busy_timeout (db, 5000) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not configure control-state busy timeout"
        );
        return FALSE;
    }

    if (!exec_sql (
            db,
            "PRAGMA foreign_keys=ON;",
            error
        ) ||
        !exec_sql (
            db,
            "PRAGMA journal_mode=WAL;",
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
    char *journal_mode = NULL;

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
        ) ||
        !query_single_text (
            db,
            "PRAGMA journal_mode;",
            &journal_mode,
            error
        )) {
        g_free (journal_mode);
        return FALSE;
    }

    gboolean valid =
        foreign_keys == 1 &&
        synchronous == 2 &&
        g_ascii_strcasecmp (
            journal_mode,
            "wal"
        ) == 0;

    g_free (journal_mode);

    if (!valid) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state SQLite safety pragmas are not active."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
bootstrap_schema (
    sqlite3 *db,
    GError **error
)
{
    char *schema_sql = NULL;

    if (!load_schema_sql (
            &schema_sql,
            error
        )) {
        return FALSE;
    }

    char *bootstrap = g_strdup_printf (
        "BEGIN IMMEDIATE;"
        "%s"
        "PRAGMA application_id=%u;"
        "PRAGMA user_version=%d;"
        "COMMIT;",
        schema_sql,
        (guint) ATM_CONTROL_STATE_APPLICATION_ID,
        ATM_CONTROL_STATE_SCHEMA_VERSION
    );

    gboolean ok = exec_sql (
        db,
        bootstrap,
        error
    );

    if (!ok) {
        sqlite3_exec (
            db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    g_free (bootstrap);
    g_free (schema_sql);
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
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not prepare control-state schema query"
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
        sqlite3_step (statement) == SQLITE_ROW;
    sqlite3_finalize (statement);

    if (!exists) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state required table '%s' is missing.",
            name
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_identity (
    sqlite3 *db,
    AtmControlStateStore *store,
    GError **error
)
{
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
        return FALSE;
    }

    if (application_id !=
        (gint64) ATM_CONTROL_STATE_APPLICATION_ID) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IDENTITY,
            "Control-state application_id is %" G_GINT64_FORMAT
            ", expected %u.",
            application_id,
            (guint) ATM_CONTROL_STATE_APPLICATION_ID
        );
        return FALSE;
    }

    if (user_version !=
        ATM_CONTROL_STATE_SCHEMA_VERSION) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state user_version is %" G_GINT64_FORMAT
            ", expected %d.",
            user_version,
            ATM_CONTROL_STATE_SCHEMA_VERSION
        );
        return FALSE;
    }

    store->application_id = application_id;
    store->schema_version = (gint) user_version;
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
        g_strcmp0 (integrity, "ok") == 0;
    g_free (integrity);

    if (!integrity_ok) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_INTEGRITY,
            "Control-state integrity_check did not return exactly 'ok'."
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
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not prepare control-state foreign_key_check"
        );
        return FALSE;
    }

    int rc = sqlite3_step (statement);
    sqlite3_finalize (statement);

    if (rc != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_INTEGRITY,
            "Control-state foreign_key_check reported a violation."
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_control_state_validate (
    AtmControlStateStore *store,
    GError **error
)
{
    static const char *required_tables[] = {
        "installation",
        "repository_generations",
        "generation_repositories",
        "active_state",
        "migration_ledger"
    };

    if (store == NULL || store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state validation requires an open store."
        );
        return FALSE;
    }

    if (!validate_identity (
            store->db,
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
            ATM_CONTROL_STATE_SCHEMA_ID
        ) == 0;
    g_free (schema_id);

    if (!schema_id_ok) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state installation schema identity is invalid."
        );
        return FALSE;
    }

    return validate_integrity (
        store->db,
        error
    );
}

gboolean
atm_control_state_open (
    const char *path,
    AtmControlStateStore **out_store,
    GError **error
)
{
    if (!nonempty (path) ||
        out_store == NULL ||
        *out_store != NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state open received invalid arguments."
        );
        return FALSE;
    }

    char *parent = g_path_get_dirname (path);

    if (g_mkdir_with_parents (
            parent,
            0700
        ) != 0) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not create control-state directory: %s",
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
        SQLITE_OPEN_FULLMUTEX,
        NULL
    );

    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not open control-state database"
        );
        if (db != NULL) {
            sqlite3_close (db);
        }
        return FALSE;
    }

    AtmControlStateStore *store = g_new0 (
        AtmControlStateStore,
        1
    );
    store->db = db;
    store->path = g_strdup (path);

    if (!configure_connection (
            db,
            error
        )) {
        atm_control_state_close (store);
        return FALSE;
    }

    gboolean has_objects = FALSE;

    if (!database_has_user_objects (
            db,
            &has_objects,
            error
        )) {
        atm_control_state_close (store);
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
        atm_control_state_close (store);
        return FALSE;
    }

    if (!has_objects &&
        application_id == 0 &&
        user_version == 0) {
        if (!bootstrap_schema (
                db,
                error
            )) {
            atm_control_state_close (store);
            return FALSE;
        }
    } else if (
        application_id !=
            (gint64) ATM_CONTROL_STATE_APPLICATION_ID
    ) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IDENTITY,
            "Refusing SQLite file with application_id %" G_GINT64_FORMAT
            " as AtM control state.",
            application_id
        );
        atm_control_state_close (store);
        return FALSE;
    } else if (
        user_version !=
            ATM_CONTROL_STATE_SCHEMA_VERSION
    ) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Unsupported AtM control-state user_version %" G_GINT64_FORMAT ".",
            user_version
        );
        atm_control_state_close (store);
        return FALSE;
    }

    if (!atm_control_state_validate (
            store,
            error
        )) {
        atm_control_state_close (store);
        return FALSE;
    }

    *out_store = store;
    return TRUE;
}

void
atm_control_state_close (
    AtmControlStateStore *store
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
atm_control_state_path (
    const AtmControlStateStore *store
)
{
    return store != NULL ? store->path : NULL;
}

gint64
atm_control_state_application_id (
    const AtmControlStateStore *store
)
{
    return store != NULL ? store->application_id : 0;
}

gint
atm_control_state_schema_version (
    const AtmControlStateStore *store
)
{
    return store != NULL ? store->schema_version : 0;
}
