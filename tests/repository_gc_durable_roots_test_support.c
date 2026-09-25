#include "repository_gc_durable_roots_test_support.h"
#include "repository_generation_lease.h"

#include <sqlite3.h>

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
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not build C1-I1 empty COMPLETE generation fixture: %s",
            message != NULL ? message : sqlite3_errmsg (db)
        );
        sqlite3_free (message);
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_c1_test_publish_empty_complete_generation (
    const char *control_path,
    GError **error
)
{
    g_return_val_if_fail (control_path != NULL, FALSE);

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2 (
        control_path,
        &db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_NOFOLLOW,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not open C1-I1 Control DB fixture: %s",
            db != NULL ? sqlite3_errmsg (db) : "open failed"
        );
        if (db != NULL) {
            sqlite3_close (db);
        }
        return FALSE;
    }

    gboolean ok =
        exec_sql (
            db,
            "PRAGMA foreign_keys=ON;"
            "BEGIN IMMEDIATE;"
            "INSERT INTO repository_generations("
            "generation_id,lifecycle,origin"
            ") VALUES(2,'CANDIDATE','c1-i1-empty-complete-test');"
            "UPDATE repository_generations "
            "SET lifecycle='COMPLETE' "
            "WHERE generation_id=2 "
            "AND lifecycle='CANDIDATE';"
            "UPDATE active_state "
            "SET active_repository_generation=2 "
            "WHERE singleton_id=1;"
            "COMMIT;",
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

    sqlite3_close (db);
    return ok;
}


gint
atm_c1_test_acquire_shared_generation_lease (
    const char *state_root,
    gint64 generation_id,
    GError **error
)
{
    gint lease_fd = -1;
    gboolean contended = FALSE;

    if (!atm_repository_generation_lease_try_acquire_shared (
            state_root,
            generation_id,
            &lease_fd,
            &contended,
            error
        )) {
        return -1;
    }

    if (contended || lease_fd < 0) {
        if (lease_fd >= 0) {
            atm_repository_generation_lease_release (
                lease_fd
            );
        }

        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not acquire C1-I2 shared generation lease fixture."
        );
        return -1;
    }

    return lease_fd;
}

void
atm_c1_test_release_generation_lease (
    gint lease_fd
)
{
    if (lease_fd >= 0) {
        atm_repository_generation_lease_release (
            lease_fd
        );
    }
}
