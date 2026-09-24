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

static char *
sha_for_generation (
    guint64 generation
)
{
    char tail[17];
    char *sha = g_malloc0 (41);

    memset (
        sha,
        '0',
        40
    );
    sha[40] = '\0';

    g_snprintf (
        tail,
        sizeof tail,
        "%016" G_GINT64_MODIFIER "x",
        generation
    );

    memcpy (
        sha + 24,
        tail,
        16
    );

    return sha;
}

static gboolean
measure_available (
    const char *path,
    guint64 *out_bytes,
    guint64 *out_inodes,
    GError **error
)
{
    struct statvfs fs;

    *out_bytes = 0;
    *out_inodes = 0;

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
        (guint64) fs.f_bavail *
        unit;
    *out_inodes =
        (guint64) fs.f_favail;
    return TRUE;
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
count_candidate_generations (
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
            "Could not open Control DB read-only: %s",
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
load_active (
    const char *state_root,
    const char *repository_id,
    gint64 *out_generation_id,
    char **out_sha,
    GError **error
)
{
    char *control_path =
        control_path_for (
            state_root
        );
    gboolean present = FALSE;
    char *version = NULL;
    char *seal = NULL;
    gboolean ok = FALSE;

    *out_generation_id = 0;
    *out_sha = NULL;

    if (!atm_control_state_active_generation_id (
            control_path,
            out_generation_id,
            error
        ) ||
        *out_generation_id <= 0 ||
        !atm_control_state_load_repository_values_at_generation (
            control_path,
            *out_generation_id,
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
    g_free (control_path);
    return ok;
}

static gboolean
validate_control_store (
    const char *control_path,
    GError **error
)
{
    AtmControlStateStore *store = NULL;
    gboolean ok = FALSE;

    if (!atm_control_state_open (
            control_path,
            &store,
            error
        )) {
        return FALSE;
    }

    ok = atm_control_state_validate (
        store,
        error
    );

    atm_control_state_close (store);
    return ok;
}

static gboolean
seed_history (
    const char *state_root,
    const char *repository_id,
    guint64 history_generations,
    GError **error
)
{
    if (history_generations == 0 ||
        history_generations >
            (guint64) G_MAXINT64) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "History generation count is invalid."
        );
        return FALSE;
    }

    if (g_mkdir_with_parents (
            state_root,
            0700
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create state root: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    char *control_path =
        control_path_for (
            state_root
        );
    AtmControlStateStore *store = NULL;

    if (!atm_control_state_open (
            control_path,
            &store,
            error
        )) {
        g_free (control_path);
        return FALSE;
    }

    atm_control_state_close (store);
    store = NULL;

    char *sha =
        sha_for_generation (1);

    gboolean ok =
        atm_control_state_set_current_values (
            control_path,
            repository_id,
            sha,
            TEST_VERSION,
            TEST_SEAL,
            error
        );

    g_free (sha);

    if (!ok) {
        g_free (control_path);
        return FALSE;
    }

    for (guint64 generation = 2;
         generation <= history_generations;
         generation++) {
        gint64 current_generation = 0;
        gint64 new_generation = 0;
        char *active_sha = NULL;

        if (!load_active (
                state_root,
                repository_id,
                &current_generation,
                &active_sha,
                error
            )) {
            g_free (active_sha);
            g_free (control_path);
            return FALSE;
        }

        g_free (active_sha);

        if ((guint64) current_generation !=
            generation - 1) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "Control DB history seed generation drifted."
            );
            g_free (control_path);
            return FALSE;
        }

        sha =
            sha_for_generation (
                generation
            );

        ok =
            atm_control_state_set_current_values_guarded (
                control_path,
                current_generation,
                repository_id,
                sha,
                TEST_VERSION,
                TEST_SEAL,
                &new_generation,
                error
            );

        g_free (sha);

        if (!ok ||
            (guint64) new_generation !=
                generation) {
            if (ok) {
                g_set_error_literal (
                    error,
                    G_FILE_ERROR,
                    G_FILE_ERROR_FAILED,
                    "Control DB history seed publication drifted."
                );
            }
            g_free (control_path);
            return FALSE;
        }
    }

    ok =
        validate_control_store (
            control_path,
            error
        );

    g_free (control_path);
    return ok;
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
    int fd = -1;
    gboolean ok = FALSE;

    *out_filler_path = NULL;
    *out_filler_bytes = 0;

    if (!measure_available (
            state_root,
            &available,
            &inodes,
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
            "atm-c0-m2-state-filler.bin",
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
            "Could not create state filler: %s",
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
            "Could not allocate state filler: %s",
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
            "Could not close state filler: %s",
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
add_error_json (
    JsonBuilder *builder,
    const GError *error
)
{
    json_builder_set_member_name (
        builder,
        "operation_error"
    );

    if (error == NULL) {
        json_builder_add_null_value (
            builder
        );
        return;
    }

    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "domain"
    );
    json_builder_add_string_value (
        builder,
        g_quark_to_string (
            error->domain
        )
    );

    json_builder_set_member_name (
        builder,
        "code"
    );
    json_builder_add_int_value (
        builder,
        error->code
    );

    json_builder_set_member_name (
        builder,
        "message"
    );
    json_builder_add_string_value (
        builder,
        error->message
    );

    json_builder_end_object (builder);
}

static void
emit_result (
    const char *state_root,
    guint64 history_generations,
    guint64 leave_target_bytes,
    guint64 available_bytes_before_operation,
    guint64 available_inodes_before_operation,
    guint64 filler_bytes,
    guint64 control_db_allocated_before_operation,
    gint64 generation_before,
    gint64 generation_after,
    gint64 attempted_generation,
    gint64 candidate_generations,
    const char *active_sha_before,
    const char *active_sha_after,
    const char *expected_new_sha,
    gboolean mutation_ok,
    gboolean invariant_ok,
    const char *classification,
    const GError *operation_error
)
{
    char *control_path =
        control_path_for (
            state_root
        );
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

#define ADD_U64(name, value)     G_STMT_START {         json_builder_set_member_name (builder, name);         json_builder_add_int_value (             builder,             (gint64) MIN (                 (guint64) (value),                 (guint64) G_MAXINT64             )         );     } G_STMT_END

    json_builder_set_member_name (
        builder,
        "schema_version"
    );
    json_builder_add_int_value (
        builder,
        1
    );

    ADD_U64 (
        "history_generations",
        history_generations
    );
    ADD_U64 (
        "leave_target_bytes",
        leave_target_bytes
    );
    ADD_U64 (
        "available_bytes_before_operation",
        available_bytes_before_operation
    );
    ADD_U64 (
        "available_inodes_before_operation",
        available_inodes_before_operation
    );
    ADD_U64 (
        "filler_allocated_bytes",
        filler_bytes
    );
    ADD_U64 (
        "control_db_allocated_before_operation",
        control_db_allocated_before_operation
    );

    json_builder_set_member_name (
        builder,
        "generation_before"
    );
    json_builder_add_int_value (
        builder,
        generation_before
    );

    json_builder_set_member_name (
        builder,
        "generation_after"
    );
    json_builder_add_int_value (
        builder,
        generation_after
    );

    json_builder_set_member_name (
        builder,
        "attempted_generation"
    );
    json_builder_add_int_value (
        builder,
        attempted_generation
    );

    json_builder_set_member_name (
        builder,
        "candidate_generations"
    );
    json_builder_add_int_value (
        builder,
        candidate_generations
    );

    json_builder_set_member_name (
        builder,
        "active_sha_before"
    );
    json_builder_add_string_value (
        builder,
        active_sha_before
    );

    json_builder_set_member_name (
        builder,
        "active_sha_after"
    );
    json_builder_add_string_value (
        builder,
        active_sha_after
    );

    json_builder_set_member_name (
        builder,
        "expected_new_sha"
    );
    json_builder_add_string_value (
        builder,
        expected_new_sha
    );

    json_builder_set_member_name (
        builder,
        "mutation_ok"
    );
    json_builder_add_boolean_value (
        builder,
        mutation_ok
    );

    json_builder_set_member_name (
        builder,
        "invariant_ok"
    );
    json_builder_add_boolean_value (
        builder,
        invariant_ok
    );

    json_builder_set_member_name (
        builder,
        "classification"
    );
    json_builder_add_string_value (
        builder,
        classification
    );

    ADD_U64 (
        "control_db_allocated_after_operation",
        allocated_bytes (
            control_path
        )
    );
    ADD_U64 (
        "wal_allocated_after_operation",
        allocated_bytes (
            wal_path
        )
    );
    ADD_U64 (
        "shm_allocated_after_operation",
        allocated_bytes (
            shm_path
        )
    );

    add_error_json (
        builder,
        operation_error
    );

#undef ADD_U64

    json_builder_end_object (builder);

    JsonNode *root =
        json_builder_get_root (builder);
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

    g_print ("%s\n", json);

    g_free (json);
    g_object_unref (generator);
    json_node_free (root);
    g_object_unref (builder);
    g_free (shm_path);
    g_free (wal_path);
    g_free (control_path);
}

static int
run_exercise (
    const char *state_root,
    const char *repository_id,
    guint64 history_generations,
    guint64 leave_bytes
)
{
    GError *error = NULL;
    GError *operation_error = NULL;
    char *control_path =
        control_path_for (
            state_root
        );
    char *active_before = NULL;
    char *active_after = NULL;
    char *new_sha = NULL;
    char *filler_path = NULL;
    guint64 filler_bytes = 0;
    guint64 available_bytes = 0;
    guint64 available_inodes = 0;
    guint64 db_allocated_before = 0;
    gint64 generation_before = 0;
    gint64 generation_after = 0;
    gint64 attempted_generation = 0;
    gint64 candidate_generations = -1;
    gboolean mutation_ok = FALSE;
    gboolean invariant_ok = FALSE;
    gboolean reload_ok = FALSE;
    gboolean candidates_ok = FALSE;
    gboolean validate_ok = FALSE;

    if (!seed_history (
            state_root,
            repository_id,
            history_generations,
            &error
        ) ||
        !load_active (
            state_root,
            repository_id,
            &generation_before,
            &active_before,
            &error
        )) {
        g_printerr (
            "C0-M2 history setup failed: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_free (active_before);
        g_free (control_path);
        return 2;
    }

    db_allocated_before =
        allocated_bytes (
            control_path
        );

    if (!consume_capacity (
            state_root,
            leave_bytes,
            &filler_path,
            &filler_bytes,
            &error
        ) ||
        !measure_available (
            state_root,
            &available_bytes,
            &available_inodes,
            &error
        )) {
        g_printerr (
            "C0-M2 filler setup failed: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_remove (filler_path);
        g_free (filler_path);
        g_free (active_before);
        g_free (control_path);
        return 3;
    }

    new_sha =
        sha_for_generation (
            history_generations + 1
        );

    mutation_ok =
        atm_control_state_set_current_values_guarded (
            control_path,
            generation_before,
            repository_id,
            new_sha,
            TEST_VERSION,
            TEST_SEAL,
            &attempted_generation,
            &operation_error
        );

    g_remove (filler_path);

    reload_ok =
        load_active (
            state_root,
            repository_id,
            &generation_after,
            &active_after,
            &error
        );
    candidates_ok =
        count_candidate_generations (
            control_path,
            &candidate_generations,
            &error
        );
    validate_ok =
        validate_control_store (
            control_path,
            &error
        );

    gboolean success_invariant =
        mutation_ok &&
        reload_ok &&
        candidates_ok &&
        validate_ok &&
        candidate_generations == 0 &&
        generation_after ==
            generation_before + 1 &&
        attempted_generation ==
            generation_after &&
        g_strcmp0 (
            active_after,
            new_sha
        ) == 0;

    gboolean failure_invariant =
        !mutation_ok &&
        operation_error != NULL &&
        reload_ok &&
        candidates_ok &&
        validate_ok &&
        candidate_generations == 0 &&
        generation_after ==
            generation_before &&
        attempted_generation == 0 &&
        g_strcmp0 (
            active_after,
            active_before
        ) == 0;

    invariant_ok =
        success_invariant ||
        failure_invariant;

    const char *classification =
        success_invariant
            ? "SUCCESS_ATOMIC"
            : failure_invariant
                ? "FAIL_CLOSED_OLD_AUTHORITY"
                : "INVARIANT_FAILURE";

    emit_result (
        state_root,
        history_generations,
        leave_bytes,
        available_bytes,
        available_inodes,
        filler_bytes,
        db_allocated_before,
        generation_before,
        reload_ok
            ? generation_after
            : 0,
        attempted_generation,
        candidate_generations,
        active_before,
        active_after != NULL
            ? active_after
            : "",
        new_sha,
        mutation_ok,
        invariant_ok,
        classification,
        operation_error
    );

    if (!invariant_ok && error != NULL) {
        g_printerr (
            "C0-M2 invariant verification failed: %s\n",
            error->message
        );
    }

    g_clear_error (&error);
    g_clear_error (&operation_error);
    g_free (filler_path);
    g_free (new_sha);
    g_free (active_after);
    g_free (active_before);
    g_free (control_path);
    return invariant_ok ? 0 : 4;
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
            "Usage: capacity-state-reserve-scaling-runner "
            "--exercise STATE_ROOT REPOSITORY_ID "
            "HISTORY_GENERATIONS LEAVE_BYTES\n"
        );
        return 64;
    }

    char *history_end = NULL;
    char *leave_end = NULL;

    guint64 history_generations =
        g_ascii_strtoull (
            argv[4],
            &history_end,
            10
        );
    guint64 leave_bytes =
        g_ascii_strtoull (
            argv[5],
            &leave_end,
            10
        );

    if (history_end == NULL ||
        *history_end != '\0' ||
        history_generations == 0 ||
        leave_end == NULL ||
        *leave_end != '\0') {
        g_printerr (
            "Invalid history or leave-bytes value.\n"
        );
        return 64;
    }

    return run_exercise (
        argv[2],
        argv[3],
        history_generations,
        leave_bytes
    );
}
