#include "control_state.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const char *TEST_VERSION = "0.1.0";
static const char *TEST_SEAL =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

typedef struct {
    gint64 page_size;
    gint64 page_count;
    gint64 freelist_count;
} DbStats;

static char *
control_path_for (
    const char *state_root
)
{
    return g_build_filename (
        state_root,
        "control-state.sqlite3",
        NULL
    );
}

static guint64
allocated_bytes (
    const char *path
)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0 ||
        st.st_blocks < 0) {
        return 0;
    }

    return (guint64) st.st_blocks * 512;
}

static gboolean
measure_available (
    const char *path,
    guint64 *out_bytes,
    guint64 *out_inodes,
    guint64 *out_fragment_size,
    GError **error
)
{
    struct statvfs fs;

    *out_bytes = 0;
    *out_inodes = 0;
    *out_fragment_size = 0;

    if (statvfs (path, &fs) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not read state filesystem capacity: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    guint64 unit =
        fs.f_frsize != 0
            ? (guint64) fs.f_frsize
            : (guint64) fs.f_bsize;

    if (unit == 0 ||
        (guint64) fs.f_bavail >
            G_MAXUINT64 / unit) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "State filesystem capacity arithmetic overflowed."
        );
        return FALSE;
    }

    *out_bytes =
        (guint64) fs.f_bavail * unit;
    *out_inodes =
        (guint64) fs.f_favail;
    *out_fragment_size = unit;
    return TRUE;
}

static gboolean
consume_capacity (
    const char *state_root,
    guint64 leave_bytes,
    char **out_filler_path,
    guint64 *out_filler_bytes,
    GError **error
)
{
    guint64 available = 0;
    guint64 inodes = 0;
    guint64 fragment_size = 0;
    int fd = -1;
    gboolean ok = FALSE;

    *out_filler_path = NULL;
    *out_filler_bytes = 0;

    if (!measure_available (
            state_root,
            &available,
            &inodes,
            &fragment_size,
            error
        )) {
        return FALSE;
    }

    if (available <= leave_bytes) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_NOSPC,
            "State filesystem has only %" G_GUINT64_FORMAT
            " bytes before filler, not enough to leave %"
            G_GUINT64_FORMAT " bytes.",
            available,
            leave_bytes
        );
        return FALSE;
    }

    guint64 request =
        available - leave_bytes;

    if (request > (guint64) G_MAXINT64) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "State filler request exceeds off_t range."
        );
        return FALSE;
    }

    char *filler =
        g_build_filename (
            state_root,
            "atm-c0-m2-capacity-filler.bin",
            NULL
        );

    fd = g_open (
        filler,
        O_RDWR |
        O_CREAT |
        O_EXCL |
        O_CLOEXEC |
        O_NOFOLLOW,
        0600
    );

    if (fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create state capacity filler: %s",
            g_strerror (errno)
        );
        g_free (filler);
        return FALSE;
    }

    int rc = posix_fallocate (
        fd,
        0,
        (off_t) request
    );

    if (rc != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (rc),
            "Could not allocate state capacity filler: %s",
            g_strerror (rc)
        );
        goto out;
    }

    if (close (fd) != 0) {
        fd = -1;
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not close state capacity filler: %s",
            g_strerror (errno)
        );
        goto out;
    }
    fd = -1;

    *out_filler_path = filler;
    filler = NULL;
    *out_filler_bytes = request;
    ok = TRUE;

out:
    if (fd >= 0) {
        close (fd);
    }
    if (!ok && filler != NULL) {
        g_remove (filler);
    }
    g_free (filler);
    return ok;
}

static void
format_sha (
    guint value,
    char out_sha[41]
)
{
    g_snprintf (
        out_sha,
        41,
        "%040u",
        value
    );
}

static gboolean
load_active (
    const char *control_path,
    const char *repository_id,
    gint64 *out_generation,
    char **out_sha,
    GError **error
)
{
    gboolean present = FALSE;
    char *version = NULL;
    char *seal = NULL;
    gboolean ok = FALSE;

    *out_generation = 0;
    *out_sha = NULL;

    if (!atm_control_state_active_generation_id (
            control_path,
            out_generation,
            error
        ) ||
        *out_generation <= 0 ||
        !atm_control_state_load_repository_values_at_generation (
            control_path,
            *out_generation,
            repository_id,
            &present,
            out_sha,
            &version,
            &seal,
            error
        ) ||
        !present ||
        *out_sha == NULL) {
        goto out;
    }

    ok = TRUE;

out:
    g_free (seal);
    g_free (version);
    return ok;
}

static gboolean
count_candidates (
    const char *control_path,
    gint64 *out_count,
    GError **error
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    *out_count = -1;

    int rc = sqlite3_open_v2 (
        control_path,
        &db,
        SQLITE_OPEN_READONLY,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not open Control DB for candidate count: %s",
            db != NULL
                ? sqlite3_errmsg (db)
                : "unknown SQLite error"
        );
        goto out;
    }

    rc = sqlite3_prepare_v2 (
        db,
        "SELECT COUNT(*) "
        "FROM repository_generations "
        "WHERE lifecycle='CANDIDATE';",
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK ||
        sqlite3_step (statement) != SQLITE_ROW) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not count candidate generations: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    *out_count =
        sqlite3_column_int64 (
            statement,
            0
        );
    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }
    if (db != NULL) {
        sqlite3_close (db);
    }
    return ok;
}

static gboolean
sqlite_scalar_i64 (
    sqlite3 *db,
    const char *sql,
    gint64 *out_value,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    int rc = sqlite3_prepare_v2 (
        db,
        sql,
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK ||
        sqlite3_step (statement) != SQLITE_ROW) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not read SQLite diagnostic '%s': %s",
            sql,
            sqlite3_errmsg (db)
        );
        goto out;
    }

    *out_value =
        sqlite3_column_int64 (
            statement,
            0
        );
    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }
    return ok;
}

static gboolean
read_db_stats (
    const char *control_path,
    DbStats *out_stats,
    GError **error
)
{
    sqlite3 *db = NULL;
    gboolean ok = FALSE;

    *out_stats = (DbStats) { 0 };

    int rc = sqlite3_open_v2 (
        control_path,
        &db,
        SQLITE_OPEN_READONLY,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not open Control DB for diagnostics: %s",
            db != NULL
                ? sqlite3_errmsg (db)
                : "unknown SQLite error"
        );
        goto out;
    }

    if (!sqlite_scalar_i64 (
            db,
            "PRAGMA page_size;",
            &out_stats->page_size,
            error
        ) ||
        !sqlite_scalar_i64 (
            db,
            "PRAGMA page_count;",
            &out_stats->page_count,
            error
        ) ||
        !sqlite_scalar_i64 (
            db,
            "PRAGMA freelist_count;",
            &out_stats->freelist_count,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    if (db != NULL) {
        sqlite3_close (db);
    }
    return ok;
}

static gboolean
validate_control_db (
    const char *control_path,
    GError **error
)
{
    AtmControlStateStore *store = NULL;

    if (!atm_control_state_open (
            control_path,
            &store,
            error
        )) {
        return FALSE;
    }

    gboolean ok =
        atm_control_state_validate (
            store,
            error
        );

    atm_control_state_close (store);
    return ok;
}

static gboolean
build_history (
    const char *control_path,
    const char *repository_id,
    guint generations,
    gint64 *out_generation,
    char **out_sha,
    GError **error
)
{
    g_return_val_if_fail (
        generations > 0,
        FALSE
    );

    char sha[41];
    format_sha (
        1,
        sha
    );

    if (!atm_control_state_set_current_values (
            control_path,
            repository_id,
            sha,
            TEST_VERSION,
            TEST_SEAL,
            error
        )) {
        return FALSE;
    }

    gint64 generation = 1;

    for (guint i = 2;
         i <= generations;
         i++) {
        char next_sha[41];
        format_sha (
            i,
            next_sha
        );

        gint64 new_generation = 0;

        if (!atm_control_state_set_current_values_guarded (
                control_path,
                generation,
                repository_id,
                next_sha,
                TEST_VERSION,
                TEST_SEAL,
                &new_generation,
                error
            )) {
            return FALSE;
        }

        generation = new_generation;
    }

    if (!load_active (
            control_path,
            repository_id,
            out_generation,
            out_sha,
            error
        )) {
        return FALSE;
    }

    return
        *out_generation == (gint64) generations;
}

static const char *
classify_operation_error (
    const GError *error
)
{
    if (error == NULL ||
        error->message == NULL) {
        return "NONE";
    }

    if (strstr (
            error->message,
            "database or disk is full"
        ) != NULL) {
        return "SQLITE_FULL";
    }

    if (strstr (
            error->message,
            "No space left on device"
        ) != NULL) {
        return "ENOSPC";
    }

    return "OTHER_ERROR";
}

static void
add_u64_json (
    JsonBuilder *builder,
    const char *name,
    guint64 value
)
{
    json_builder_set_member_name (
        builder,
        name
    );
    json_builder_add_int_value (
        builder,
        value > (guint64) G_MAXINT64
            ? G_MAXINT64
            : (gint64) value
    );
}

static void
emit_result (
    guint history_generations,
    guint64 leave_target_bytes,
    guint64 available_bytes_before_filler,
    guint64 available_inodes_before_filler,
    guint64 available_bytes_before_operation,
    guint64 available_inodes_before_operation,
    guint64 fragment_size,
    guint64 filler_bytes,
    guint64 control_db_allocated_bytes_before,
    gint64 generation_before,
    gint64 generation_after,
    const char *sha_before,
    const char *sha_after,
    gboolean operation_succeeded,
    gboolean integrity_qualified,
    gint64 candidate_generations,
    const DbStats *stats_before,
    const DbStats *stats_after,
    const char *control_path,
    const GError *operation_error
)
{
    char *wal_path =
        g_strconcat (
            control_path,
            "-wal",
            NULL
        );
    char *shm_path =
        g_strconcat (
            control_path,
            "-shm",
            NULL
        );

    JsonBuilder *builder =
        json_builder_new ();

    json_builder_begin_object (builder);


    json_builder_set_member_name (
        builder,
        "sha_before"
    );
    json_builder_add_string_value (
        builder,
        sha_before
    );

    json_builder_set_member_name (
        builder,
        "sha_after"
    );
    json_builder_add_string_value (
        builder,
        sha_after
    );

    json_builder_set_member_name (
        builder,
        "operation_succeeded"
    );
    json_builder_add_boolean_value (
        builder,
        operation_succeeded
    );

    json_builder_set_member_name (
        builder,
        "integrity_qualified"
    );
    json_builder_add_boolean_value (
        builder,
        integrity_qualified
    );

    json_builder_set_member_name (
        builder,
        "outcome"
    );
    json_builder_add_string_value (
        builder,
        operation_succeeded
            ? "SUCCESS"
            : classify_operation_error (
                operation_error
            )
    );

    json_builder_set_member_name (
        builder,
        "operation_error"
    );
    if (operation_error == NULL) {
        json_builder_add_null_value (
            builder
        );
    } else {
        json_builder_begin_object (
            builder
        );
        json_builder_set_member_name (
            builder,
            "domain"
        );
        json_builder_add_string_value (
            builder,
            g_quark_to_string (
                operation_error->domain
            )
        );
        json_builder_set_member_name (
            builder,
            "code"
        );
        json_builder_add_int_value (
            builder,
            operation_error->code
        );
        json_builder_set_member_name (
            builder,
            "message"
        );
        json_builder_add_string_value (
            builder,
            operation_error->message
        );
        json_builder_end_object (
            builder
        );
    }

    json_builder_end_object (
        builder
    );

    JsonNode *root =
        json_builder_get_root (
            builder
        );
    JsonGenerator *generator =
        json_generator_new ();

    json_generator_set_root (
        generator,
        root
    );
    json_generator_set_pretty (
        generator,
        FALSE
    );

    char *json =
        json_generator_to_data (
            generator,
            NULL
        );

    g_print (
        "%s\n",
        json
    );

    g_free (json);
    g_object_unref (generator);
    json_node_free (root);
    g_object_unref (builder);
    g_free (shm_path);
    g_free (wal_path);
}

static int
run_exercise (
    const char *state_root,
    const char *repository_id,
    guint history_generations,
    guint64 leave_target_bytes
)
{
    GError *error = NULL;
    GError *operation_error = NULL;
    char *control_path =
        control_path_for (
            state_root
        );
    char *filler_path = NULL;
    char *sha_before = NULL;
    char *sha_after = NULL;
    guint64 filler_bytes = 0;
    guint64 available_bytes_before_filler = 0;
    guint64 available_inodes_before_filler = 0;
    guint64 available_bytes = 0;
    guint64 available_inodes = 0;
    guint64 fragment_size = 0;
    guint64 pre_filler_fragment_size = 0;
    guint64 control_db_allocated_bytes_before = 0;
    gint64 generation_before = 0;
    gint64 generation_after = 0;
    gint64 candidate_generations = -1;
    DbStats stats_before;
    DbStats stats_after;

    if (g_mkdir_with_parents (
            state_root,
            0700
        ) != 0) {
        g_printerr (
            "Could not create state root: %s\n",
            g_strerror (errno)
        );
        g_free (control_path);
        return 2;
    }

    AtmControlStateStore *store = NULL;
    if (!atm_control_state_open (
            control_path,
            &store,
            &error
        )) {
        g_printerr (
            "Could not initialize Control DB: %s\n",
            error->message
        );
        g_clear_error (&error);
        g_free (control_path);
        return 3;
    }
    atm_control_state_close (store);

    if (!build_history (
            control_path,
            repository_id,
            history_generations,
            &generation_before,
            &sha_before,
            &error
        ) ||
        !validate_control_db (
            control_path,
            &error
        ) ||
        !read_db_stats (
            control_path,
            &stats_before,
            &error
        ) ||
        !measure_available (
            state_root,
            &available_bytes_before_filler,
            &available_inodes_before_filler,
            &pre_filler_fragment_size,
            &error
        ) ||
        !consume_capacity (
            state_root,
            leave_target_bytes,
            &filler_path,
            &filler_bytes,
            &error
        ) ||
        !measure_available (
            state_root,
            &available_bytes,
            &available_inodes,
            &fragment_size,
            &error
        )) {
        g_printerr (
            "C0-M2 setup failed: %s\n",
            error != NULL
                ? error->message
                : "unknown setup error"
        );
        g_clear_error (&error);
        g_free (filler_path);
        g_free (sha_before);
        g_free (control_path);
        return 4;
    }

    control_db_allocated_bytes_before =
        allocated_bytes (
            control_path
        );

    if (pre_filler_fragment_size !=
        fragment_size) {
        g_printerr (
            "C0-M2 filesystem fragment size changed within one case.\n"
        );
        g_remove (filler_path);
        g_free (filler_path);
        g_free (sha_before);
        g_free (control_path);
        return 4;
    }

    char next_sha[41];
    format_sha (
        history_generations + 1,
        next_sha
    );

    gint64 new_generation = 0;
    gboolean operation_succeeded =
        atm_control_state_set_current_values_guarded (
            control_path,
            generation_before,
            repository_id,
            next_sha,
            TEST_VERSION,
            TEST_SEAL,
            &new_generation,
            &operation_error
        );

    if (filler_path != NULL) {
        g_remove (
            filler_path
        );
    }

    if (!validate_control_db (
            control_path,
            &error
        ) ||
        !load_active (
            control_path,
            repository_id,
            &generation_after,
            &sha_after,
            &error
        ) ||
        !count_candidates (
            control_path,
            &candidate_generations,
            &error
        ) ||
        !read_db_stats (
            control_path,
            &stats_after,
            &error
        )) {
        g_printerr (
            "C0-M2 verification failed: %s\n",
            error != NULL
                ? error->message
                : "unknown verification error"
        );
        g_clear_error (&error);
        g_clear_error (&operation_error);
        g_free (filler_path);
        g_free (sha_after);
        g_free (sha_before);
        g_free (control_path);
        return 5;
    }

    gboolean integrity_qualified =
        candidate_generations == 0;

    if (operation_succeeded) {
        integrity_qualified =
            integrity_qualified &&
            new_generation > generation_before &&
            generation_after == new_generation &&
            g_strcmp0 (
                sha_after,
                next_sha
            ) == 0;
    } else {
        integrity_qualified =
            integrity_qualified &&
            new_generation == 0 &&
            generation_after == generation_before &&
            g_strcmp0 (
                sha_after,
                sha_before
            ) == 0;
    }

    emit_result (
        history_generations,
        leave_target_bytes,
        available_bytes_before_filler,
        available_inodes_before_filler,
        available_bytes,
        available_inodes,
        fragment_size,
        filler_bytes,
        control_db_allocated_bytes_before,
        generation_before,
        generation_after,
        sha_before,
        sha_after,
        operation_succeeded,
        integrity_qualified,
        candidate_generations,
        &stats_before,
        &stats_after,
        control_path,
        operation_error
    );

    g_clear_error (&operation_error);
    g_free (filler_path);
    g_free (sha_after);
    g_free (sha_before);
    g_free (control_path);

    return integrity_qualified
        ? 0
        : 6;
}

int
main (
    int argc,
    char **argv
)
{
    if (argc != 6 ||
        g_strcmp0 (
            argv[1],
            "--exercise"
        ) != 0) {
        g_printerr (
            "Usage: capacity-state-headroom-runner "
            "--exercise STATE_ROOT REPOSITORY_ID "
            "HISTORY_GENERATIONS LEAVE_BYTES\n"
        );
        return 64;
    }

    guint64 history =
        g_ascii_strtoull (
            argv[4],
            NULL,
            10
        );
    guint64 leave =
        g_ascii_strtoull (
            argv[5],
            NULL,
            10
        );

    if (history == 0 ||
        history > G_MAXUINT ||
        leave == 0) {
        g_printerr (
            "Invalid history/leave arguments.\n"
        );
        return 64;
    }

    return run_exercise (
        argv[2],
        argv[3],
        (guint) history,
        leave
    );
}
