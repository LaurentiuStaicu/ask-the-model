#define _GNU_SOURCE

#include "control_state.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ATM_CONTROL_STATE_SCHEMA_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/control-state-v2.sql"
#define ATM_CONTROL_STATE_MIGRATION_V1_V2_RESOURCE \
    "/io/github/laurentiustaicu/ask_the_model/schemas/control-state-v1-to-v2.sql"
#define ATM_CONTROL_STATE_SCHEMA_V1_ID "atm-control-state/1"
#define ATM_CONTROL_STATE_SCHEMA_V1_VERSION 1

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
load_sql_resource (
    const char *resource_path,
    char **out_sql,
    GError **error
)
{
    if (!nonempty (resource_path) ||
        out_sql == NULL ||
        *out_sql != NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state SQL resource load received invalid arguments."
        );
        return FALSE;
    }

    GError *resource_error = NULL;
    GBytes *bytes = g_resources_lookup_data (
        resource_path,
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &resource_error
    );

    if (bytes == NULL) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Could not load control-state SQL resource %s: %s",
            resource_path,
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
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state SQL resource %s is empty.",
            resource_path
        );
        return FALSE;
    }

    *out_sql = g_strndup (data, size);
    g_bytes_unref (bytes);
    return TRUE;
}

static gboolean
configure_db_flag (
    sqlite3 *db,
    int option,
    int requested,
    const char *name,
    GError **error
)
{
    int effective = -1;
    int rc = sqlite3_db_config (
        db,
        option,
        requested,
        &effective
    );

    if (rc != SQLITE_OK ||
        effective != requested) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Could not enforce SQLite connection setting %s=%d (rc=%d, effective=%d).",
            name,
            requested,
            rc,
            effective
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
configure_connection (
    sqlite3 *db,
    GError **error
)
{
    if (sqlite3_libversion_number () < 3031000) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state requires SQLite >= 3.31.0; runtime is %s.",
            sqlite3_libversion ()
        );
        return FALSE;
    }

    if (sqlite3_busy_timeout (db, 5000) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not configure control-state busy timeout"
        );
        return FALSE;
    }

    if (!configure_db_flag (
            db,
            SQLITE_DBCONFIG_DEFENSIVE,
            1,
            "defensive",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_TRUSTED_SCHEMA,
            0,
            "trusted_schema",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_DQS_DML,
            0,
            "dqs_dml",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_DQS_DDL,
            0,
            "dqs_ddl",
            error
        ) ||
        !configure_db_flag (
            db,
            SQLITE_DBCONFIG_ENABLE_TRIGGER,
            1,
            "enable_trigger",
            error
        )) {
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

    if (!load_sql_resource (
            ATM_CONTROL_STATE_SCHEMA_RESOURCE,
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


static gboolean
required_trigger_exists (
    sqlite3 *db,
    const char *name,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "SELECT 1 FROM sqlite_master "
            "WHERE type='trigger' AND name=?1;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not prepare control-state trigger query"
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
            "Control-state required trigger '%s' is missing.",
            name
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_schema_id_value (
    sqlite3 *db,
    const char *expected_schema_id,
    GError **error
)
{
    char *schema_id = NULL;

    if (!query_single_text (
            db,
            "SELECT schema_id FROM installation "
            "WHERE singleton_id=1;",
            &schema_id,
            error
        )) {
        return FALSE;
    }

    gboolean matches =
        g_strcmp0 (
            schema_id,
            expected_schema_id
        ) == 0;
    g_free (schema_id);

    if (!matches) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state installation schema identity does not match %s.",
            expected_schema_id
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_persisted_generation_semantics (
    sqlite3 *db,
    GError **error
)
{
    gint64 active_rows = 0;
    gint64 candidate_rows = 0;
    gint64 invalid_active_rows = 0;

    if (!query_single_int64 (
            db,
            "SELECT count(*) FROM active_state "
            "WHERE singleton_id=1;",
            &active_rows,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) FROM repository_generations "
            "WHERE lifecycle='CANDIDATE';",
            &candidate_rows,
            error
        ) ||
        !query_single_int64 (
            db,
            "SELECT count(*) "
            "FROM active_state a "
            "LEFT JOIN repository_generations g "
            "ON g.generation_id=a.active_repository_generation "
            "WHERE a.singleton_id=1 "
            "AND a.active_repository_generation IS NOT NULL "
            "AND (g.generation_id IS NULL OR g.lifecycle!='COMPLETE');",
            &invalid_active_rows,
            error
        )) {
        return FALSE;
    }

    if (active_rows != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control-state active_state singleton row is missing."
        );
        return FALSE;
    }

    if (candidate_rows != 0) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_INTEGRITY,
            "Control-state contains a persisted CANDIDATE repository generation."
        );
        return FALSE;
    }

    if (invalid_active_rows != 0) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_INTEGRITY,
            "Control-state active repository generation is not COMPLETE."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_v1_for_migration (
    sqlite3 *db,
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

    for (gsize i = 0;
         i < G_N_ELEMENTS (required_tables);
         i++) {
        if (!required_table_exists (
                db,
                required_tables[i],
                error
            )) {
            return FALSE;
        }
    }

    return validate_schema_id_value (
            db,
            ATM_CONTROL_STATE_SCHEMA_V1_ID,
            error
        ) &&
        validate_integrity (
            db,
            error
        ) &&
        validate_persisted_generation_semantics (
            db,
            error
        );
}

static gboolean
migrate_v1_to_v2 (
    AtmControlStateStore *store,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state schema migration requires an open store."
        );
        return FALSE;
    }

    if (!validate_v1_for_migration (
            store->db,
            error
        )) {
        return FALSE;
    }

    char *migration_sql = NULL;

    if (!load_sql_resource (
            ATM_CONTROL_STATE_MIGRATION_V1_V2_RESOURCE,
            &migration_sql,
            error
        )) {
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        g_free (migration_sql);
        return FALSE;
    }

    gboolean ok = FALSE;

    if (!exec_sql (
            store->db,
            migration_sql,
            error
        )) {
        goto rollback;
    }

    char *version_sql = g_strdup_printf (
        "PRAGMA user_version=%d;",
        ATM_CONTROL_STATE_SCHEMA_VERSION
    );

    if (!exec_sql (
            store->db,
            version_sql,
            error
        )) {
        g_free (version_sql);
        goto rollback;
    }

    g_free (version_sql);

    if (!atm_control_state_validate (
            store,
            error
        )) {
        goto rollback;
    }

    if (!exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto rollback;
    }

    ok = TRUE;

rollback:
    if (!ok) {
        sqlite3_exec (
            store->db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    g_free (migration_sql);
    return ok;
}

static gboolean
ensure_control_state_schema (
    AtmControlStateStore *store,
    gboolean allow_bootstrap,
    GError **error
)
{
    gboolean has_objects = FALSE;

    if (!database_has_user_objects (
            store->db,
            &has_objects,
            error
        )) {
        return FALSE;
    }

    gint64 application_id = 0;
    gint64 user_version = 0;

    if (!query_single_int64 (
            store->db,
            "PRAGMA application_id;",
            &application_id,
            error
        ) ||
        !query_single_int64 (
            store->db,
            "PRAGMA user_version;",
            &user_version,
            error
        )) {
        return FALSE;
    }

    if (!has_objects &&
        application_id == 0 &&
        user_version == 0) {
        if (!allow_bootstrap) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_SCHEMA,
                "Existing control-state authority is empty and cannot be bootstrapped implicitly."
            );
            return FALSE;
        }

        if (!bootstrap_schema (
                store->db,
                error
            )) {
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
        return FALSE;
    } else if (
        user_version ==
            ATM_CONTROL_STATE_SCHEMA_V1_VERSION
    ) {
        if (!migrate_v1_to_v2 (
                store,
                error
            )) {
            return FALSE;
        }

        return TRUE;
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
        return FALSE;
    }

    return atm_control_state_validate (
        store,
        error
    );
}


typedef struct {
    char *repository_id;
    char *snapshot_sha;
    char *repository_version;
    char *snapshot_seal_sha256;
} AtmLegacyRepositoryState;

typedef struct {
    gint schema_version;
    GPtrArray *repositories;
} AtmLegacyState;

static void
legacy_repository_state_free (
    AtmLegacyRepositoryState *record
)
{
    if (record == NULL) {
        return;
    }

    g_free (record->repository_id);
    g_free (record->snapshot_sha);
    g_free (record->repository_version);
    g_free (record->snapshot_seal_sha256);
    g_free (record);
}

static void
legacy_state_free (
    AtmLegacyState *state
)
{
    if (state == NULL) {
        return;
    }

    g_clear_pointer (
        &state->repositories,
        g_ptr_array_unref
    );
    g_free (state);
}

static gboolean
legacy_node_is_string (
    JsonNode *node
)
{
    return node != NULL &&
        json_node_get_node_type (node) == JSON_NODE_VALUE &&
        json_node_get_value_type (node) == G_TYPE_STRING;
}

static gboolean
legacy_node_is_int64 (
    JsonNode *node
)
{
    return node != NULL &&
        json_node_get_node_type (node) == JSON_NODE_VALUE &&
        json_node_get_value_type (node) == G_TYPE_INT64;
}

static gboolean
legacy_node_is_null (
    JsonNode *node
)
{
    return node != NULL &&
        json_node_get_node_type (node) == JSON_NODE_NULL;
}

static gboolean
lower_hex_exact (
    const char *value,
    gsize expected_length
)
{
    if (value == NULL ||
        strlen (value) != expected_length) {
        return FALSE;
    }

    for (gsize i = 0; i < expected_length; i++) {
        char c = value[i];

        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f'))) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
legacy_repository_id_known (
    const char *repository_id
)
{
    return g_strcmp0 (repository_id, "ewd") == 0 ||
        g_strcmp0 (repository_id, "cbd") == 0 ||
        g_strcmp0 (repository_id, "rmd") == 0;
}

static AtmLegacyRepositoryState *
legacy_state_find (
    const AtmLegacyState *state,
    const char *repository_id
)
{
    if (state == NULL ||
        state->repositories == NULL) {
        return NULL;
    }

    for (guint i = 0;
         i < state->repositories->len;
         i++) {
        AtmLegacyRepositoryState *record =
            g_ptr_array_index (
                state->repositories,
                i
            );

        if (g_strcmp0 (
                record->repository_id,
                repository_id
            ) == 0) {
            return record;
        }
    }

    return NULL;
}

static gboolean
legacy_state_parse (
    const char *path,
    AtmLegacyState **out_state,
    GError **error
)
{
    if (!nonempty (path) ||
        out_state == NULL ||
        *out_state != NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Legacy-state parser received invalid arguments."
        );
        return FALSE;
    }

    char *contents = NULL;
    gsize contents_length = 0;
    GError *read_error = NULL;

    if (!g_file_get_contents (
            path,
            &contents,
            &contents_length,
            &read_error
        )) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_LEGACY_STATE,
            "Could not read legacy repository state: %s",
            read_error != NULL
                ? read_error->message
                : "unknown read error"
        );
        g_clear_error (&read_error);
        return FALSE;
    }

    JsonParser *parser = json_parser_new ();
    GError *parse_error = NULL;

    if (!json_parser_load_from_data (
            parser,
            contents,
            (gssize) contents_length,
            &parse_error
        )) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_LEGACY_STATE,
            "Legacy repository state is not valid JSON: %s",
            parse_error != NULL
                ? parse_error->message
                : "unknown parse error"
        );
        g_clear_error (&parse_error);
        g_object_unref (parser);
        g_free (contents);
        return FALSE;
    }

    JsonNode *root_node = json_parser_get_root (parser);
    const char *problem = NULL;

    if (root_node == NULL ||
        json_node_get_node_type (root_node) !=
            JSON_NODE_OBJECT) {
        problem =
            "Legacy repository state root must be an object.";
        goto invalid;
    }

    JsonObject *root =
        json_node_get_object (root_node);
    JsonNode *schema_node =
        json_object_get_member (
            root,
            "schema_version"
        );
    JsonNode *repositories_node =
        json_object_get_member (
            root,
            "repositories"
        );

    if (!legacy_node_is_int64 (schema_node) ||
        repositories_node == NULL ||
        json_node_get_node_type (
            repositories_node
        ) != JSON_NODE_ARRAY) {
        problem =
            "Legacy repository state requires integer schema_version and repositories array.";
        goto invalid;
    }

    gint64 schema_version =
        json_node_get_int (schema_node);

    if (schema_version != 1 &&
        schema_version != 2) {
        problem =
            "Legacy repository state schema_version is unsupported.";
        goto invalid;
    }

    AtmLegacyState *state = g_new0 (
        AtmLegacyState,
        1
    );
    state->schema_version =
        (gint) schema_version;
    state->repositories =
        g_ptr_array_new_with_free_func (
            (GDestroyNotify)
                legacy_repository_state_free
        );

    JsonArray *repositories =
        json_node_get_array (
            repositories_node
        );

    for (guint i = 0;
         i < json_array_get_length (repositories);
         i++) {
        JsonNode *item_node =
            json_array_get_element (
                repositories,
                i
            );

        if (item_node == NULL ||
            json_node_get_node_type (item_node) !=
                JSON_NODE_OBJECT) {
            problem =
                "Legacy repository entry must be an object.";
            legacy_state_free (state);
            goto invalid;
        }

        JsonObject *item =
            json_node_get_object (item_node);
        JsonNode *id_node =
            json_object_get_member (item, "id");
        JsonNode *sha_node =
            json_object_get_member (item, "sha");
        JsonNode *version_node =
            json_object_get_member (
                item,
                "version"
            );

        if (!legacy_node_is_string (id_node) ||
            !legacy_node_is_string (sha_node) ||
            !legacy_node_is_string (
                version_node
            )) {
            problem =
                "Legacy repository entry requires string id, sha and version.";
            legacy_state_free (state);
            goto invalid;
        }

        const char *repository_id =
            json_node_get_string (id_node);
        const char *snapshot_sha =
            json_node_get_string (sha_node);
        const char *repository_version =
            json_node_get_string (
                version_node
            );
        const char *snapshot_seal = NULL;

        if (state->schema_version == 2) {
            JsonNode *seal_node =
                json_object_get_member (
                    item,
                    "snapshot_seal_sha256"
                );

            if (seal_node == NULL) {
                problem =
                    "Legacy v2 repository entry requires snapshot_seal_sha256.";
                legacy_state_free (state);
                goto invalid;
            }

            if (legacy_node_is_string (
                    seal_node
                )) {
                snapshot_seal =
                    json_node_get_string (
                        seal_node
                    );

                if (!lower_hex_exact (
                        snapshot_seal,
                        64
                    )) {
                    problem =
                        "Legacy repository snapshot seal is invalid.";
                    legacy_state_free (state);
                    goto invalid;
                }
            } else if (!legacy_node_is_null (
                           seal_node
                       )) {
                problem =
                    "Legacy repository snapshot seal must be string or null.";
                legacy_state_free (state);
                goto invalid;
            }
        }

        if (!legacy_repository_id_known (
                repository_id
            ) ||
            !lower_hex_exact (
                snapshot_sha,
                40
            ) ||
            repository_version == NULL ||
            repository_version[0] == '\0') {
            problem =
                "Legacy repository identity is invalid.";
            legacy_state_free (state);
            goto invalid;
        }

        if (legacy_state_find (
                state,
                repository_id
            ) != NULL) {
            problem =
                "Legacy repository state contains a duplicate repository id.";
            legacy_state_free (state);
            goto invalid;
        }

        AtmLegacyRepositoryState *record =
            g_new0 (
                AtmLegacyRepositoryState,
                1
            );
        record->repository_id =
            g_strdup (repository_id);
        record->snapshot_sha =
            g_strdup (snapshot_sha);
        record->repository_version =
            g_strdup (repository_version);
        record->snapshot_seal_sha256 =
            g_strdup (snapshot_seal);

        g_ptr_array_add (
            state->repositories,
            record
        );
    }

    g_object_unref (parser);
    g_free (contents);
    *out_state = state;
    return TRUE;

invalid:
    g_set_error_literal (
        error,
        ATM_CONTROL_STATE_ERROR,
        ATM_CONTROL_STATE_ERROR_LEGACY_STATE,
        problem != NULL
            ? problem
            : "Legacy repository state is invalid."
    );
    g_object_unref (parser);
    g_free (contents);
    return FALSE;
}

static gboolean
prepare_statement (
    sqlite3 *db,
    const char *sql,
    sqlite3_stmt **out_statement,
    GError **error
)
{
    if (sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            out_statement,
            NULL
        ) != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not prepare control-state statement"
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
generation_matches_state (
    sqlite3 *db,
    gint64 generation_id,
    const AtmLegacyState *state,
    gboolean *out_matches,
    GError **error
)
{
    *out_matches = FALSE;

    sqlite3_stmt *generation_statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT count(*) "
            "FROM repository_generations "
            "WHERE generation_id=?1;",
            &generation_statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        generation_statement,
        1,
        generation_id
    );

    if (sqlite3_step (generation_statement) !=
        SQLITE_ROW) {
        sqlite3_finalize (generation_statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not verify repository generation"
        );
        return FALSE;
    }

    gint64 generation_count =
        sqlite3_column_int64 (
            generation_statement,
            0
        );
    sqlite3_finalize (generation_statement);

    if (generation_count != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Repository generation does not exist."
        );
        return FALSE;
    }

    sqlite3_stmt *count_statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT count(*) "
            "FROM generation_repositories "
            "WHERE generation_id=?1;",
            &count_statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        count_statement,
        1,
        generation_id
    );

    if (sqlite3_step (count_statement) !=
        SQLITE_ROW) {
        sqlite3_finalize (count_statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not count generation repositories"
        );
        return FALSE;
    }

    gint64 count =
        sqlite3_column_int64 (
            count_statement,
            0
        );
    sqlite3_finalize (count_statement);

    if (count !=
        (gint64) state->repositories->len) {
        return TRUE;
    }

    sqlite3_stmt *statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT snapshot_sha, "
            "repository_version, "
            "snapshot_seal_sha256 "
            "FROM generation_repositories "
            "WHERE generation_id=?1 "
            "AND repository_id=?2;",
            &statement,
            error
        )) {
        return FALSE;
    }

    for (guint i = 0;
         i < state->repositories->len;
         i++) {
        AtmLegacyRepositoryState *record =
            g_ptr_array_index (
                state->repositories,
                i
            );

        sqlite3_reset (statement);
        sqlite3_clear_bindings (statement);
        sqlite3_bind_int64 (
            statement,
            1,
            generation_id
        );
        sqlite3_bind_text (
            statement,
            2,
            record->repository_id,
            -1,
            SQLITE_STATIC
        );

        int rc = sqlite3_step (statement);

        if (rc == SQLITE_DONE) {
            sqlite3_finalize (statement);
            return TRUE;
        }

        if (rc != SQLITE_ROW) {
            sqlite3_finalize (statement);
            set_sqlite_error (
                db,
                error,
                ATM_CONTROL_STATE_ERROR_SQLITE,
                "Could not read generation repository"
            );
            return FALSE;
        }

        const char *snapshot_sha =
            (const char *) sqlite3_column_text (
                statement,
                0
            );
        const char *repository_version =
            (const char *) sqlite3_column_text (
                statement,
                1
            );
        gboolean seal_matches;

        if (record->snapshot_seal_sha256 ==
            NULL) {
            seal_matches =
                sqlite3_column_type (
                    statement,
                    2
                ) == SQLITE_NULL;
        } else {
            const char *snapshot_seal =
                (const char *)
                    sqlite3_column_text (
                        statement,
                        2
                    );
            seal_matches =
                g_strcmp0 (
                    snapshot_seal,
                    record->
                        snapshot_seal_sha256
                ) == 0;
        }

        gboolean row_matches =
            g_strcmp0 (
                snapshot_sha,
                record->snapshot_sha
            ) == 0 &&
            g_strcmp0 (
                repository_version,
                record->repository_version
            ) == 0 &&
            seal_matches;

        if (!row_matches) {
            sqlite3_finalize (statement);
            return TRUE;
        }

        if (sqlite3_step (statement) !=
            SQLITE_DONE) {
            sqlite3_finalize (statement);
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_SQLITE,
                "Generation repository query returned multiple rows."
            );
            return FALSE;
        }
    }

    sqlite3_finalize (statement);
    *out_matches = TRUE;
    return TRUE;
}

gboolean
atm_control_state_generation_matches_legacy_json (
    AtmControlStateStore *store,
    gint64 generation_id,
    const char *legacy_json_path,
    gboolean *out_matches,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL ||
        generation_id <= 0 ||
        !nonempty (legacy_json_path) ||
        out_matches == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state equivalence check received invalid arguments."
        );
        return FALSE;
    }

    *out_matches = FALSE;
    AtmLegacyState *state = NULL;

    if (!legacy_state_parse (
            legacy_json_path,
            &state,
            error
        )) {
        return FALSE;
    }

    gboolean ok = generation_matches_state (
        store->db,
        generation_id,
        state,
        out_matches,
        error
    );

    legacy_state_free (state);
    return ok;
}

gboolean
atm_control_state_import_legacy_json (
    AtmControlStateStore *store,
    const char *legacy_json_path,
    gint64 *out_generation_id,
    GError **error
)
{
    static const char *repository_order[] = {
        "cbd",
        "ewd",
        "rmd"
    };
    const gint64 generation_id = 1;

    if (store == NULL ||
        store->db == NULL ||
        !nonempty (legacy_json_path) ||
        out_generation_id == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state legacy import received invalid arguments."
        );
        return FALSE;
    }

    *out_generation_id = 0;

    if (!atm_control_state_validate (
            store,
            error
        )) {
        return FALSE;
    }

    AtmLegacyState *state = NULL;

    if (!legacy_state_parse (
            legacy_json_path,
            &state,
            error
        )) {
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        legacy_state_free (state);
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *generation_statement = NULL;
    sqlite3_stmt *repository_statement = NULL;
    sqlite3_stmt *complete_statement = NULL;
    gint64 generation_count = 0;
    gint64 active_is_null = 0;

    if (!query_single_int64 (
            store->db,
            "SELECT count(*) "
            "FROM repository_generations;",
            &generation_count,
            error
        ) ||
        !query_single_int64 (
            store->db,
            "SELECT active_repository_generation IS NULL "
            "FROM active_state "
            "WHERE singleton_id=1;",
            &active_is_null,
            error
        )) {
        goto rollback;
    }

    if (generation_count != 0 ||
        active_is_null != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Control-state legacy import requires an empty, inactive repository-generation store."
        );
        goto rollback;
    }

    if (!prepare_statement (
            store->db,
            "INSERT INTO repository_generations("
            "generation_id, lifecycle, origin"
            ") VALUES(?1, 'CANDIDATE', ?2);",
            &generation_statement,
            error
        )) {
        goto rollback;
    }

    char *origin = g_strdup_printf (
        "repository-state.json/schema=%d",
        state->schema_version
    );
    sqlite3_bind_int64 (
        generation_statement,
        1,
        generation_id
    );
    sqlite3_bind_text (
        generation_statement,
        2,
        origin,
        -1,
        SQLITE_TRANSIENT
    );

    if (sqlite3_step (generation_statement) !=
        SQLITE_DONE) {
        g_free (origin);
        set_sqlite_error (
            store->db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not create legacy candidate generation"
        );
        goto rollback;
    }

    g_free (origin);
    sqlite3_finalize (generation_statement);
    generation_statement = NULL;

    if (!prepare_statement (
            store->db,
            "INSERT INTO generation_repositories("
            "generation_id, repository_id, "
            "snapshot_sha, repository_version, "
            "snapshot_seal_sha256"
            ") VALUES(?1, ?2, ?3, ?4, ?5);",
            &repository_statement,
            error
        )) {
        goto rollback;
    }

    for (gsize i = 0;
         i < G_N_ELEMENTS (repository_order);
         i++) {
        AtmLegacyRepositoryState *record =
            legacy_state_find (
                state,
                repository_order[i]
            );

        if (record == NULL) {
            continue;
        }

        sqlite3_reset (repository_statement);
        sqlite3_clear_bindings (
            repository_statement
        );
        sqlite3_bind_int64 (
            repository_statement,
            1,
            generation_id
        );
        sqlite3_bind_text (
            repository_statement,
            2,
            record->repository_id,
            -1,
            SQLITE_STATIC
        );
        sqlite3_bind_text (
            repository_statement,
            3,
            record->snapshot_sha,
            -1,
            SQLITE_STATIC
        );
        sqlite3_bind_text (
            repository_statement,
            4,
            record->repository_version,
            -1,
            SQLITE_STATIC
        );

        if (record->snapshot_seal_sha256 ==
            NULL) {
            sqlite3_bind_null (
                repository_statement,
                5
            );
        } else {
            sqlite3_bind_text (
                repository_statement,
                5,
                record->snapshot_seal_sha256,
                -1,
                SQLITE_STATIC
            );
        }

        if (sqlite3_step (
                repository_statement
            ) != SQLITE_DONE) {
            set_sqlite_error (
                store->db,
                error,
                ATM_CONTROL_STATE_ERROR_SQLITE,
                "Could not import legacy repository state"
            );
            goto rollback;
        }
    }

    sqlite3_finalize (repository_statement);
    repository_statement = NULL;

    gboolean matches = FALSE;

    if (!generation_matches_state (
            store->db,
            generation_id,
            state,
            &matches,
            error
        )) {
        goto rollback;
    }

    if (!matches) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_LEGACY_STATE,
            "Imported candidate generation is not semantically equivalent to legacy repository state."
        );
        goto rollback;
    }

    if (!prepare_statement (
            store->db,
            "UPDATE repository_generations "
            "SET lifecycle='COMPLETE' "
            "WHERE generation_id=?1 "
            "AND lifecycle='CANDIDATE';",
            &complete_statement,
            error
        )) {
        goto rollback;
    }

    sqlite3_bind_int64 (
        complete_statement,
        1,
        generation_id
    );

    if (sqlite3_step (complete_statement) !=
            SQLITE_DONE ||
        sqlite3_changes (store->db) != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not complete imported repository generation."
        );
        goto rollback;
    }

    sqlite3_finalize (complete_statement);
    complete_statement = NULL;

    if (!exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto rollback;
    }

    ok = TRUE;
    *out_generation_id = generation_id;

rollback:
    if (generation_statement != NULL) {
        sqlite3_finalize (
            generation_statement
        );
    }
    if (repository_statement != NULL) {
        sqlite3_finalize (
            repository_statement
        );
    }
    if (complete_statement != NULL) {
        sqlite3_finalize (
            complete_statement
        );
    }

    if (!ok) {
        sqlite3_exec (
            store->db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    legacy_state_free (state);
    return ok;
}


static gboolean
fsync_regular_file (
    const char *path,
    GError **error
)
{
    int fd = g_open (
        path,
        O_RDONLY | O_CLOEXEC,
        0
    );

    if (fd < 0) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not open cutover candidate for fsync: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (fsync (fd) != 0) {
        int saved_errno = errno;
        close (fd);
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not fsync cutover candidate: %s",
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    close (fd);
    return TRUE;
}

static gboolean
fsync_directory (
    const char *path,
    GError **error
)
{
    int fd = g_open (
        path,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC,
        0
    );

    if (fd < 0) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not open control-state directory for fsync: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    if (fsync (fd) != 0) {
        int saved_errno = errno;
        close (fd);
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not fsync control-state directory: %s",
            g_strerror (saved_errno)
        );
        return FALSE;
    }

    close (fd);
    return TRUE;
}

static void
remove_cutover_candidate (
    const char *candidate_path
)
{
    if (candidate_path == NULL) {
        return;
    }

    char *wal_path = g_strconcat (
        candidate_path,
        "-wal",
        NULL
    );
    char *shm_path = g_strconcat (
        candidate_path,
        "-shm",
        NULL
    );

    g_remove (wal_path);
    g_remove (shm_path);
    g_remove (candidate_path);

    g_free (wal_path);
    g_free (shm_path);
}

static gboolean
candidate_has_sidecars (
    const char *candidate_path
)
{
    char *wal_path = g_strconcat (
        candidate_path,
        "-wal",
        NULL
    );
    char *shm_path = g_strconcat (
        candidate_path,
        "-shm",
        NULL
    );

    gboolean has_sidecars =
        g_file_test (
            wal_path,
            G_FILE_TEST_EXISTS
        ) ||
        g_file_test (
            shm_path,
            G_FILE_TEST_EXISTS
        );

    g_free (wal_path);
    g_free (shm_path);
    return has_sidecars;
}

static gboolean
record_cutover_state (
    AtmControlStateStore *store,
    gboolean imported_legacy,
    gint legacy_schema_version,
    GError **error
)
{
    if (store == NULL ||
        store->db == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Cutover finalization requires an open control-state store."
        );
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *ledger = NULL;

    if (imported_legacy) {
        char *lifecycle = NULL;

        if (!query_single_text (
                store->db,
                "SELECT lifecycle "
                "FROM repository_generations "
                "WHERE generation_id=1;",
                &lifecycle,
                error
            )) {
            goto rollback;
        }

        gboolean complete =
            g_strcmp0 (
                lifecycle,
                "COMPLETE"
            ) == 0;
        g_free (lifecycle);

        if (!complete) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_CONFLICT,
                "Cutover candidate generation is not COMPLETE."
            );
            goto rollback;
        }

        if (!exec_sql (
                store->db,
                "UPDATE active_state "
                "SET active_repository_generation=1 "
                "WHERE singleton_id=1 "
                "AND active_repository_generation IS NULL;",
                error
            )) {
            goto rollback;
        }

        if (sqlite3_changes (store->db) != 1) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_CONFLICT,
                "Cutover candidate could not activate generation 1."
            );
            goto rollback;
        }
    }

    if (!prepare_statement (
            store->db,
            "INSERT INTO migration_ledger("
            "migration_id, schema_version, applied_origin"
            ") VALUES(?1, ?2, ?3);",
            &ledger,
            error
        )) {
        goto rollback;
    }

    sqlite3_bind_text (
        ledger,
        1,
        "state-03a-cutover-v1",
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_int (
        ledger,
        2,
        ATM_CONTROL_STATE_SCHEMA_VERSION
    );

    char *origin = imported_legacy
        ? g_strdup_printf (
            "repository-state.json/schema=%d",
            legacy_schema_version
        )
        : g_strdup ("empty-bootstrap");

    sqlite3_bind_text (
        ledger,
        3,
        origin,
        -1,
        SQLITE_TRANSIENT
    );

    if (sqlite3_step (ledger) != SQLITE_DONE) {
        g_free (origin);
        set_sqlite_error (
            store->db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not record control-state cutover"
        );
        goto rollback;
    }

    g_free (origin);
    sqlite3_finalize (ledger);
    ledger = NULL;

    if (!exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto rollback;
    }

    ok = TRUE;

rollback:
    if (ledger != NULL) {
        sqlite3_finalize (ledger);
    }

    if (!ok) {
        sqlite3_exec (
            store->db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    return ok;
}

static gboolean
checkpoint_cutover_candidate (
    AtmControlStateStore *store,
    GError **error
)
{
    int log_frames = -1;
    int checkpointed_frames = -1;
    int rc = sqlite3_wal_checkpoint_v2 (
        store->db,
        NULL,
        SQLITE_CHECKPOINT_TRUNCATE,
        &log_frames,
        &checkpointed_frames
    );

    if (rc != SQLITE_OK) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not checkpoint control-state cutover candidate"
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
publish_no_replace (
    const char *candidate_path,
    const char *control_path,
    GError **error
)
{
    if (renameat2 (
            AT_FDCWD,
            candidate_path,
            AT_FDCWD,
            control_path,
            RENAME_NOREPLACE
        ) == 0) {
        return TRUE;
    }

    int rename_errno = errno;

    if (rename_errno == EEXIST) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Refusing to replace an existing control-state database during cutover."
        );
        return FALSE;
    }

    if (rename_errno != ENOSYS &&
        rename_errno != EINVAL &&
        rename_errno != EOPNOTSUPP) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not publish control-state database: %s",
            g_strerror (rename_errno)
        );
        return FALSE;
    }

    if (link (
            candidate_path,
            control_path
        ) != 0) {
        int link_errno = errno;

        if (link_errno == EEXIST) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_CONFLICT,
                "Refusing to replace an existing control-state database during cutover."
            );
        } else {
            g_set_error (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_IO,
                "Could not publish control-state database with no-replace fallback: %s",
                g_strerror (link_errno)
            );
        }

        return FALSE;
    }

    g_remove (candidate_path);
    return TRUE;
}


static gboolean
open_existing_control_state (
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
            "Existing control-state open received invalid arguments."
        );
        return FALSE;
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2 (
        path,
        &db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_NOFOLLOW,
        NULL
    );

    if (rc != SQLITE_OK) {
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not open existing control-state database"
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
        ) ||
        !ensure_control_state_schema (
            store,
            FALSE,
            error
        )) {
        atm_control_state_close (store);
        return FALSE;
    }

    *out_store = store;
    return TRUE;
}

static gboolean
active_complete_generation (
    sqlite3 *db,
    gint64 *out_generation_id,
    GError **error
)
{
    sqlite3_stmt *active_statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT active_repository_generation "
            "FROM active_state "
            "WHERE singleton_id=1;",
            &active_statement,
            error
        )) {
        return FALSE;
    }

    int rc = sqlite3_step (active_statement);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (active_statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control state has no active_state singleton row."
        );
        return FALSE;
    }

    gboolean active_is_null =
        sqlite3_column_type (
            active_statement,
            0
        ) == SQLITE_NULL;
    gint64 generation_id =
        active_is_null
            ? 0
            : sqlite3_column_int64 (
                active_statement,
                0
            );

    if (sqlite3_step (active_statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (active_statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Control state has multiple active_state singleton rows."
        );
        return FALSE;
    }

    sqlite3_finalize (active_statement);

    if (generation_id == 0) {
        gint64 generation_count = 0;

        if (!query_single_int64 (
                db,
                "SELECT count(*) "
                "FROM repository_generations;",
                &generation_count,
                error
            )) {
            return FALSE;
        }

        if (generation_count != 0) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_CONFLICT,
                "Control state has repository generations but no active authority."
            );
            return FALSE;
        }

        *out_generation_id = 0;
        return TRUE;
    }

    sqlite3_stmt *lifecycle_statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT lifecycle "
            "FROM repository_generations "
            "WHERE generation_id=?1;",
            &lifecycle_statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        lifecycle_statement,
        1,
        generation_id
    );

    rc = sqlite3_step (lifecycle_statement);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (lifecycle_statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Active repository generation does not exist."
        );
        return FALSE;
    }

    const char *lifecycle =
        (const char *) sqlite3_column_text (
            lifecycle_statement,
            0
        );
    gboolean complete =
        g_strcmp0 (
            lifecycle,
            "COMPLETE"
        ) == 0;

    if (sqlite3_step (lifecycle_statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (lifecycle_statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Active repository generation lookup returned multiple rows."
        );
        return FALSE;
    }

    sqlite3_finalize (lifecycle_statement);

    if (!complete) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Active repository generation is not COMPLETE."
        );
        return FALSE;
    }

    *out_generation_id = generation_id;
    return TRUE;
}

static gboolean
create_candidate_generation (
    sqlite3 *db,
    gint64 active_generation_id,
    const char *origin,
    gint64 *out_generation_id,
    GError **error
)
{
    gint64 max_generation_id = 0;

    if (!query_single_int64 (
            db,
            "SELECT COALESCE(MAX(generation_id), 0) "
            "FROM repository_generations;",
            &max_generation_id,
            error
        )) {
        return FALSE;
    }

    if (max_generation_id == G_MAXINT64) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Control-state generation identifier space is exhausted."
        );
        return FALSE;
    }

    gint64 generation_id =
        max_generation_id + 1;
    sqlite3_stmt *generation_statement = NULL;

    if (!prepare_statement (
            db,
            "INSERT INTO repository_generations("
            "generation_id, lifecycle, origin"
            ") VALUES(?1, 'CANDIDATE', ?2);",
            &generation_statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        generation_statement,
        1,
        generation_id
    );
    sqlite3_bind_text (
        generation_statement,
        2,
        origin,
        -1,
        SQLITE_STATIC
    );

    if (sqlite3_step (generation_statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (generation_statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not create copy-on-write repository generation"
        );
        return FALSE;
    }

    sqlite3_finalize (generation_statement);

    if (active_generation_id != 0) {
        sqlite3_stmt *copy_statement = NULL;

        if (!prepare_statement (
                db,
                "INSERT INTO generation_repositories("
                "generation_id, repository_id, "
                "snapshot_sha, repository_version, "
                "snapshot_seal_sha256"
                ") "
                "SELECT ?1, repository_id, "
                "snapshot_sha, repository_version, "
                "snapshot_seal_sha256 "
                "FROM generation_repositories "
                "WHERE generation_id=?2;",
                &copy_statement,
                error
            )) {
            return FALSE;
        }

        sqlite3_bind_int64 (
            copy_statement,
            1,
            generation_id
        );
        sqlite3_bind_int64 (
            copy_statement,
            2,
            active_generation_id
        );

        if (sqlite3_step (copy_statement) !=
            SQLITE_DONE) {
            sqlite3_finalize (copy_statement);
            set_sqlite_error (
                db,
                error,
                ATM_CONTROL_STATE_ERROR_SQLITE,
                "Could not copy active repository generation"
            );
            return FALSE;
        }

        sqlite3_finalize (copy_statement);
    }

    *out_generation_id = generation_id;
    return TRUE;
}

static gboolean
complete_and_activate_candidate (
    sqlite3 *db,
    gint64 previous_generation_id,
    gint64 candidate_generation_id,
    GError **error
)
{
    sqlite3_stmt *complete_statement = NULL;

    if (!prepare_statement (
            db,
            "UPDATE repository_generations "
            "SET lifecycle='COMPLETE' "
            "WHERE generation_id=?1 "
            "AND lifecycle='CANDIDATE';",
            &complete_statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        complete_statement,
        1,
        candidate_generation_id
    );

    if (sqlite3_step (complete_statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (complete_statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not complete copy-on-write repository generation"
        );
        return FALSE;
    }

    sqlite3_finalize (complete_statement);

    if (sqlite3_changes (db) != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Copy-on-write repository generation was not CANDIDATE."
        );
        return FALSE;
    }

    sqlite3_stmt *activate_statement = NULL;
    const char *sql =
        previous_generation_id == 0
            ? "UPDATE active_state "
              "SET active_repository_generation=?1 "
              "WHERE singleton_id=1 "
              "AND active_repository_generation IS NULL;"
            : "UPDATE active_state "
              "SET active_repository_generation=?1 "
              "WHERE singleton_id=1 "
              "AND active_repository_generation=?2;";

    if (!prepare_statement (
            db,
            sql,
            &activate_statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        activate_statement,
        1,
        candidate_generation_id
    );

    if (previous_generation_id != 0) {
        sqlite3_bind_int64 (
            activate_statement,
            2,
            previous_generation_id
        );
    }

    if (sqlite3_step (activate_statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (activate_statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not activate copy-on-write repository generation"
        );
        return FALSE;
    }

    sqlite3_finalize (activate_statement);

    if (sqlite3_changes (db) != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Active repository generation changed during copy-on-write publication."
        );
        return FALSE;
    }

    return TRUE;
}


static gboolean
generation_is_complete (
    sqlite3 *db,
    gint64 generation_id,
    GError **error
)
{
    if (generation_id <= 0) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Repository generation identifier must be positive."
        );
        return FALSE;
    }

    sqlite3_stmt *statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT lifecycle "
            "FROM repository_generations "
            "WHERE generation_id=?1;",
            &statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        statement,
        1,
        generation_id
    );

    int rc = sqlite3_step (statement);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Pinned repository generation does not exist."
        );
        return FALSE;
    }

    const char *lifecycle =
        (const char *) sqlite3_column_text (
            statement,
            0
        );
    gboolean complete =
        g_strcmp0 (
            lifecycle,
            "COMPLETE"
        ) == 0;

    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Pinned repository generation lookup returned multiple rows."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);

    if (!complete) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Pinned repository generation is not COMPLETE."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
load_repository_values_from_generation (
    sqlite3 *db,
    gint64 generation_id,
    const char *repository_id,
    gboolean *out_present,
    char **out_snapshot_sha,
    char **out_repository_version,
    char **out_snapshot_seal_sha256,
    GError **error
)
{
    *out_present = FALSE;
    *out_snapshot_sha = NULL;
    *out_repository_version = NULL;
    *out_snapshot_seal_sha256 = NULL;

    if (!generation_is_complete (
            db,
            generation_id,
            error
        )) {
        return FALSE;
    }

    sqlite3_stmt *statement = NULL;

    if (!prepare_statement (
            db,
            "SELECT snapshot_sha, "
            "repository_version, "
            "snapshot_seal_sha256 "
            "FROM generation_repositories "
            "WHERE generation_id=?1 "
            "AND repository_id=?2;",
            &statement,
            error
        )) {
        return FALSE;
    }

    sqlite3_bind_int64 (
        statement,
        1,
        generation_id
    );
    sqlite3_bind_text (
        statement,
        2,
        repository_id,
        -1,
        SQLITE_STATIC
    );

    int rc = sqlite3_step (statement);

    if (rc == SQLITE_DONE) {
        sqlite3_finalize (statement);
        return TRUE;
    }

    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        set_sqlite_error (
            db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not read pinned repository state"
        );
        return FALSE;
    }

    const char *snapshot_sha =
        (const char *) sqlite3_column_text (
            statement,
            0
        );
    const char *repository_version =
        (const char *) sqlite3_column_text (
            statement,
            1
        );

    if (!lower_hex_exact (
            snapshot_sha,
            40
        ) ||
        !nonempty (repository_version)) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_INTEGRITY,
            "Pinned repository state contains an invalid snapshot identity."
        );
        return FALSE;
    }

    *out_snapshot_sha =
        g_strdup (snapshot_sha);
    *out_repository_version =
        g_strdup (repository_version);

    if (sqlite3_column_type (
            statement,
            2
        ) != SQLITE_NULL) {
        const char *seal =
            (const char *) sqlite3_column_text (
                statement,
                2
            );

        if (!lower_hex_exact (
                seal,
                64
            )) {
            sqlite3_finalize (statement);
            g_clear_pointer (
                out_snapshot_sha,
                g_free
            );
            g_clear_pointer (
                out_repository_version,
                g_free
            );
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_INTEGRITY,
                "Pinned repository state contains an invalid snapshot seal."
            );
            return FALSE;
        }

        *out_snapshot_seal_sha256 =
            g_strdup (seal);
    }

    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_clear_pointer (
            out_snapshot_sha,
            g_free
        );
        g_clear_pointer (
            out_repository_version,
            g_free
        );
        g_clear_pointer (
            out_snapshot_seal_sha256,
            g_free
        );
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_SCHEMA,
            "Pinned repository state contains duplicate rows."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    *out_present = TRUE;
    return TRUE;
}

gboolean
atm_control_state_active_generation_id (
    const char *path,
    gint64 *out_generation_id,
    GError **error
)
{
    if (!nonempty (path) ||
        out_generation_id == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state active-generation read received invalid arguments."
        );
        return FALSE;
    }

    *out_generation_id = 0;
    AtmControlStateStore *store = NULL;

    if (!open_existing_control_state (
            path,
            &store,
            error
        )) {
        return FALSE;
    }

    gboolean ok = active_complete_generation (
        store->db,
        out_generation_id,
        error
    );

    atm_control_state_close (store);
    return ok;
}

gboolean
atm_control_state_load_repository_values_at_generation (
    const char *path,
    gint64 generation_id,
    const char *repository_id,
    gboolean *out_present,
    char **out_snapshot_sha,
    char **out_repository_version,
    char **out_snapshot_seal_sha256,
    GError **error
)
{
    if (!nonempty (path) ||
        generation_id <= 0 ||
        !legacy_repository_id_known (
            repository_id
        ) ||
        out_present == NULL ||
        out_snapshot_sha == NULL ||
        out_repository_version == NULL ||
        out_snapshot_seal_sha256 == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state pinned repository read received invalid arguments."
        );
        return FALSE;
    }

    AtmControlStateStore *store = NULL;

    if (!open_existing_control_state (
            path,
            &store,
            error
        )) {
        return FALSE;
    }

    gboolean ok =
        load_repository_values_from_generation (
            store->db,
            generation_id,
            repository_id,
            out_present,
            out_snapshot_sha,
            out_repository_version,
            out_snapshot_seal_sha256,
            error
        );

    atm_control_state_close (store);
    return ok;
}

gboolean
atm_control_state_load_repository_values (
    const char *path,
    const char *repository_id,
    gboolean *out_present,
    char **out_snapshot_sha,
    char **out_repository_version,
    char **out_snapshot_seal_sha256,
    GError **error
)
{
    if (!nonempty (path) ||
        !legacy_repository_id_known (
            repository_id
        ) ||
        out_present == NULL ||
        out_snapshot_sha == NULL ||
        out_repository_version == NULL ||
        out_snapshot_seal_sha256 == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state repository read received invalid arguments."
        );
        return FALSE;
    }

    *out_present = FALSE;
    *out_snapshot_sha = NULL;
    *out_repository_version = NULL;
    *out_snapshot_seal_sha256 = NULL;

    AtmControlStateStore *store = NULL;

    if (!open_existing_control_state (
            path,
            &store,
            error
        )) {
        return FALSE;
    }

    gint64 generation_id = 0;
    gboolean ok = FALSE;

    if (!active_complete_generation (
            store->db,
            &generation_id,
            error
        )) {
        goto done;
    }

    if (generation_id == 0) {
        ok = TRUE;
        goto done;
    }

    ok = load_repository_values_from_generation (
        store->db,
        generation_id,
        repository_id,
        out_present,
        out_snapshot_sha,
        out_repository_version,
        out_snapshot_seal_sha256,
        error
    );

done:
    atm_control_state_close (store);
    return ok;
}


static gboolean
set_current_values_impl (
    const char *path,
    gboolean enforce_expected_generation,
    gint64 expected_generation_id,
    const char *repository_id,
    const char *snapshot_sha,
    const char *repository_version,
    const char *snapshot_seal_sha256,
    gint64 *out_generation_id,
    GError **error
)
{
    if (!nonempty (path) ||
        (enforce_expected_generation &&
         expected_generation_id < 0) ||
        !legacy_repository_id_known (
            repository_id
        ) ||
        !lower_hex_exact (
            snapshot_sha,
            40
        ) ||
        !nonempty (repository_version) ||
        (snapshot_seal_sha256 != NULL &&
         !lower_hex_exact (
             snapshot_seal_sha256,
             64
         ))) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state set-current received invalid repository values."
        );
        return FALSE;
    }

    if (out_generation_id != NULL) {
        *out_generation_id = 0;
    }

    AtmControlStateStore *store = NULL;

    if (!open_existing_control_state (
            path,
            &store,
            error
        )) {
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        atm_control_state_close (store);
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *statement = NULL;
    gint64 previous_generation_id = 0;
    gint64 candidate_generation_id = 0;

    if (!active_complete_generation (
            store->db,
            &previous_generation_id,
            error
        )) {
        goto rollback;
    }

    if (enforce_expected_generation &&
        previous_generation_id !=
            expected_generation_id) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Repository generation changed from expected %" G_GINT64_FORMAT
            " to %" G_GINT64_FORMAT ".",
            expected_generation_id,
            previous_generation_id
        );
        goto rollback;
    }

    if (!create_candidate_generation (
            store->db,
            previous_generation_id,
            "runtime-cow:set-current",
            &candidate_generation_id,
            error
        )) {
        goto rollback;
    }

    if (!prepare_statement (
            store->db,
            "INSERT INTO generation_repositories("
            "generation_id, repository_id, "
            "snapshot_sha, repository_version, "
            "snapshot_seal_sha256"
            ") VALUES(?1, ?2, ?3, ?4, ?5) "
            "ON CONFLICT(generation_id, repository_id) "
            "DO UPDATE SET "
            "snapshot_sha=excluded.snapshot_sha, "
            "repository_version=excluded.repository_version, "
            "snapshot_seal_sha256=excluded.snapshot_seal_sha256;",
            &statement,
            error
        )) {
        goto rollback;
    }

    sqlite3_bind_int64 (
        statement,
        1,
        candidate_generation_id
    );
    sqlite3_bind_text (
        statement,
        2,
        repository_id,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        3,
        snapshot_sha,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        4,
        repository_version,
        -1,
        SQLITE_STATIC
    );

    if (snapshot_seal_sha256 == NULL) {
        sqlite3_bind_null (
            statement,
            5
        );
    } else {
        sqlite3_bind_text (
            statement,
            5,
            snapshot_seal_sha256,
            -1,
            SQLITE_STATIC
        );
    }

    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not write copy-on-write repository state"
        );
        goto rollback;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (!complete_and_activate_candidate (
            store->db,
            previous_generation_id,
            candidate_generation_id,
            error
        ) ||
        !exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto rollback;
    }

    ok = TRUE;

    if (out_generation_id != NULL) {
        *out_generation_id =
            candidate_generation_id;
    }

rollback:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }

    if (!ok) {
        sqlite3_exec (
            store->db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    atm_control_state_close (store);
    return ok;
}

gboolean
atm_control_state_set_current_values (
    const char *path,
    const char *repository_id,
    const char *snapshot_sha,
    const char *repository_version,
    const char *snapshot_seal_sha256,
    GError **error
)
{
    gint64 generation_id = 0;

    return set_current_values_impl (
        path,
        FALSE,
        0,
        repository_id,
        snapshot_sha,
        repository_version,
        snapshot_seal_sha256,
        &generation_id,
        error
    );
}

gboolean
atm_control_state_set_current_values_guarded (
    const char *path,
    gint64 expected_generation_id,
    const char *repository_id,
    const char *snapshot_sha,
    const char *repository_version,
    const char *snapshot_seal_sha256,
    gint64 *out_generation_id,
    GError **error
)
{
    if (out_generation_id == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Guarded set-current requires an output generation identifier."
        );
        return FALSE;
    }

    return set_current_values_impl (
        path,
        TRUE,
        expected_generation_id,
        repository_id,
        snapshot_sha,
        repository_version,
        snapshot_seal_sha256,
        out_generation_id,
        error
    );
}


static gboolean
set_snapshot_seal_values_impl (
    const char *path,
    gboolean enforce_expected_generation,
    gint64 expected_generation_id,
    const char *repository_id,
    const char *expected_snapshot_sha,
    const char *snapshot_seal_sha256,
    gint64 *out_generation_id,
    GError **error
)
{
    if (!nonempty (path) ||
        (enforce_expected_generation &&
         expected_generation_id <= 0) ||
        !legacy_repository_id_known (
            repository_id
        ) ||
        !lower_hex_exact (
            expected_snapshot_sha,
            40
        ) ||
        !lower_hex_exact (
            snapshot_seal_sha256,
            64
        )) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state set-seal received invalid repository values."
        );
        return FALSE;
    }

    if (out_generation_id != NULL) {
        *out_generation_id = 0;
    }

    AtmControlStateStore *store = NULL;

    if (!open_existing_control_state (
            path,
            &store,
            error
        )) {
        return FALSE;
    }

    if (!exec_sql (
            store->db,
            "BEGIN IMMEDIATE;",
            error
        )) {
        atm_control_state_close (store);
        return FALSE;
    }

    gboolean ok = FALSE;
    sqlite3_stmt *statement = NULL;
    gint64 previous_generation_id = 0;
    gint64 candidate_generation_id = 0;

    if (!active_complete_generation (
            store->db,
            &previous_generation_id,
            error
        )) {
        goto rollback;
    }

    if (previous_generation_id == 0) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "No active repository generation exists for snapshot-seal update."
        );
        goto rollback;
    }

    if (enforce_expected_generation &&
        previous_generation_id !=
            expected_generation_id) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Repository generation changed from expected %" G_GINT64_FORMAT
            " to %" G_GINT64_FORMAT ".",
            expected_generation_id,
            previous_generation_id
        );
        goto rollback;
    }

    if (!create_candidate_generation (
            store->db,
            previous_generation_id,
            "runtime-cow:set-seal",
            &candidate_generation_id,
            error
        )) {
        goto rollback;
    }

    if (!prepare_statement (
            store->db,
            "UPDATE generation_repositories "
            "SET snapshot_seal_sha256=?1 "
            "WHERE generation_id=?2 "
            "AND repository_id=?3 "
            "AND snapshot_sha=?4;",
            &statement,
            error
        )) {
        goto rollback;
    }

    sqlite3_bind_text (
        statement,
        1,
        snapshot_seal_sha256,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_int64 (
        statement,
        2,
        candidate_generation_id
    );
    sqlite3_bind_text (
        statement,
        3,
        repository_id,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        4,
        expected_snapshot_sha,
        -1,
        SQLITE_STATIC
    );

    if (sqlite3_step (statement) !=
        SQLITE_DONE) {
        set_sqlite_error (
            store->db,
            error,
            ATM_CONTROL_STATE_ERROR_SQLITE,
            "Could not write copy-on-write repository snapshot seal"
        );
        goto rollback;
    }

    if (sqlite3_changes (store->db) != 1) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Snapshot seal cannot be attached to a different or absent active snapshot."
        );
        goto rollback;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (!complete_and_activate_candidate (
            store->db,
            previous_generation_id,
            candidate_generation_id,
            error
        ) ||
        !exec_sql (
            store->db,
            "COMMIT;",
            error
        )) {
        goto rollback;
    }

    ok = TRUE;

    if (out_generation_id != NULL) {
        *out_generation_id =
            candidate_generation_id;
    }

rollback:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }

    if (!ok) {
        sqlite3_exec (
            store->db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    atm_control_state_close (store);
    return ok;
}

gboolean
atm_control_state_set_snapshot_seal_values (
    const char *path,
    const char *repository_id,
    const char *expected_snapshot_sha,
    const char *snapshot_seal_sha256,
    GError **error
)
{
    gint64 generation_id = 0;

    return set_snapshot_seal_values_impl (
        path,
        FALSE,
        0,
        repository_id,
        expected_snapshot_sha,
        snapshot_seal_sha256,
        &generation_id,
        error
    );
}

gboolean
atm_control_state_set_snapshot_seal_values_guarded (
    const char *path,
    gint64 expected_generation_id,
    const char *repository_id,
    const char *expected_snapshot_sha,
    const char *snapshot_seal_sha256,
    gint64 *out_generation_id,
    GError **error
)
{
    if (out_generation_id == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Guarded set-seal requires an output generation identifier."
        );
        return FALSE;
    }

    return set_snapshot_seal_values_impl (
        path,
        TRUE,
        expected_generation_id,
        repository_id,
        expected_snapshot_sha,
        snapshot_seal_sha256,
        out_generation_id,
        error
    );
}

gboolean
atm_control_state_publish_cutover (
    const char *control_path,
    const char *legacy_json_path,
    AtmControlStateCutoverDisposition *out_disposition,
    GError **error
)
{
    if (!nonempty (control_path) ||
        !nonempty (legacy_json_path) ||
        out_disposition == NULL) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_ARGUMENT,
            "Control-state cutover received invalid arguments."
        );
        return FALSE;
    }

    *out_disposition =
        ATM_CONTROL_STATE_CUTOVER_EMPTY;

    if (g_file_test (
            control_path,
            G_FILE_TEST_EXISTS
        )) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_CONFLICT,
            "Control-state cutover requires the authoritative database path to be absent."
        );
        return FALSE;
    }

    gboolean legacy_exists =
        g_file_test (
            legacy_json_path,
            G_FILE_TEST_EXISTS
        );
    AtmLegacyState *legacy_state = NULL;

    if (legacy_exists &&
        !legacy_state_parse (
            legacy_json_path,
            &legacy_state,
            error
        )) {
        return FALSE;
    }

    char *parent =
        g_path_get_dirname (control_path);

    if (g_mkdir_with_parents (
            parent,
            0700
        ) != 0) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not create control-state cutover directory: %s",
            g_strerror (errno)
        );
        legacy_state_free (legacy_state);
        g_free (parent);
        return FALSE;
    }

    char *candidate_path =
        g_build_filename (
            parent,
            ".control-state-cutover-XXXXXX",
            NULL
        );
    int candidate_fd =
        g_mkstemp (candidate_path);

    if (candidate_fd < 0) {
        g_set_error (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_IO,
            "Could not create control-state cutover candidate: %s",
            g_strerror (errno)
        );
        legacy_state_free (legacy_state);
        g_free (candidate_path);
        g_free (parent);
        return FALSE;
    }

    close (candidate_fd);

    AtmControlStateStore *store = NULL;
    gboolean published = FALSE;
    gboolean ok = FALSE;

    if (!atm_control_state_open (
            candidate_path,
            &store,
            error
        )) {
        goto done;
    }

    gint64 generation_id = 0;

    if (legacy_exists) {
        if (!atm_control_state_import_legacy_json (
                store,
                legacy_json_path,
                &generation_id,
                error
            )) {
            goto done;
        }

        if (generation_id != 1) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_CONFLICT,
                "Cutover legacy import did not create generation 1."
            );
            goto done;
        }
    }

    if (!record_cutover_state (
            store,
            legacy_exists,
            legacy_state != NULL
                ? legacy_state->schema_version
                : 0,
            error
        )) {
        goto done;
    }

    if (legacy_exists) {
        gboolean matches = FALSE;

        if (!generation_matches_state (
                store->db,
                1,
                legacy_state,
                &matches,
                error
            )) {
            goto done;
        }

        if (!matches) {
            g_set_error_literal (
                error,
                ATM_CONTROL_STATE_ERROR,
                ATM_CONTROL_STATE_ERROR_LEGACY_STATE,
                "Activated cutover generation is not semantically equivalent to legacy repository state."
            );
            goto done;
        }
    }

    if (!atm_control_state_validate (
            store,
            error
        ) ||
        !checkpoint_cutover_candidate (
            store,
            error
        )) {
        goto done;
    }

    atm_control_state_close (store);
    store = NULL;

    if (candidate_has_sidecars (
            candidate_path
        )) {
        g_set_error_literal (
            error,
            ATM_CONTROL_STATE_ERROR,
            ATM_CONTROL_STATE_ERROR_INTEGRITY,
            "Cutover candidate still has SQLite WAL sidecars after clean checkpoint and close."
        );
        goto done;
    }

    if (!fsync_regular_file (
            candidate_path,
            error
        )) {
        goto done;
    }

    if (!publish_no_replace (
            candidate_path,
            control_path,
            error
        )) {
        goto done;
    }

    published = TRUE;

    if (!fsync_directory (
            parent,
            error
        )) {
        goto done;
    }

    *out_disposition = legacy_exists
        ? ATM_CONTROL_STATE_CUTOVER_IMPORTED_LEGACY
        : ATM_CONTROL_STATE_CUTOVER_EMPTY;
    ok = TRUE;

done:
    if (store != NULL) {
        atm_control_state_close (store);
    }

    if (!published) {
        remove_cutover_candidate (
            candidate_path
        );
    } else {
        g_remove (candidate_path);
    }

    legacy_state_free (legacy_state);
    g_free (candidate_path);
    g_free (parent);
    return ok;
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

    static const char *required_triggers[] = {
        "trg_installation_immutable_update",
        "trg_installation_immutable_delete",
        "trg_repository_generations_no_complete_insert",
        "trg_repository_generations_complete_update",
        "trg_repository_generations_complete_delete",
        "trg_generation_repositories_complete_insert",
        "trg_generation_repositories_complete_update",
        "trg_generation_repositories_complete_delete",
        "trg_active_state_guard_update",
        "trg_active_state_no_delete",
        "trg_migration_ledger_append_only_update",
        "trg_migration_ledger_append_only_delete"
    };

    for (gsize i = 0;
         i < G_N_ELEMENTS (required_triggers);
         i++) {
        if (!required_trigger_exists (
                store->db,
                required_triggers[i],
                error
            )) {
            return FALSE;
        }
    }

    return validate_schema_id_value (
            store->db,
            ATM_CONTROL_STATE_SCHEMA_ID,
            error
        ) &&
        validate_integrity (
            store->db,
            error
        ) &&
        validate_persisted_generation_semantics (
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
        SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_NOFOLLOW,
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

    if (!ensure_control_state_schema (
            store,
            TRUE,
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
