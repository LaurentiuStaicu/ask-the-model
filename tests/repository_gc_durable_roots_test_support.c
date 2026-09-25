#include "repository_gc_durable_roots_test_support.h"

#include <sqlite3.h>
#include <errno.h>
#include <unistd.h>

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


gboolean
atm_c1_test_make_symlink (
    const char *target,
    const char *link_path,
    GError **error
)
{
    g_return_val_if_fail (target != NULL, FALSE);
    g_return_val_if_fail (link_path != NULL, FALSE);

    if (symlink (
            target,
            link_path
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create C1 test symlink: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    return TRUE;
}
