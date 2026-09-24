#include "control_state.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const char *TEST_VERSION = "0.1.0";
static const char *OLD_SEAL =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *NEW_SEAL =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

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
        (guint64) fs.f_bavail * unit;
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
seed_old_authority (
    const char *state_root,
    const char *repository_id,
    const char *old_sha,
    GError **error
)
{
    char *control_path =
        control_path_for (
            state_root
        );
    AtmControlStateStore *store = NULL;
    gboolean ok = FALSE;

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
        goto out;
    }

    if (!atm_control_state_open (
            control_path,
            &store,
            error
        )) {
        goto out;
    }

    atm_control_state_close (store);
    store = NULL;

    if (!atm_control_state_set_current_values (
            control_path,
            repository_id,
            old_sha,
            TEST_VERSION,
            OLD_SEAL,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    if (store != NULL) {
        atm_control_state_close (store);
    }
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
    int rc;
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
            "atm-c0-e3-capacity-filler.bin",
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

    rc = posix_fallocate (
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
    if (!ok) {
        g_remove (filler);
    }
    g_free (filler);
    return ok;
}

static gboolean
error_is_capacity_failure (
    const GError *error
)
{
    if (error == NULL ||
        error->message == NULL) {
        return FALSE;
    }

    return strstr (
        error->message,
        "database or disk is full"
    ) != NULL ||
        strstr (
            error->message,
            "No space left on device"
        ) != NULL ||
        (error->domain ==
             ATM_CONTROL_STATE_ERROR &&
         (error->code ==
              ATM_CONTROL_STATE_ERROR_SQLITE ||
          error->code ==
              ATM_CONTROL_STATE_ERROR_IO));
}

static void
emit_result (
    const char *scenario,
    gboolean qualified,
    const char *classification,
    gint64 generation_before,
    gint64 generation_after,
    const char *old_sha,
    const char *active_sha_after,
    guint64 available_bytes_before_operation,
    guint64 available_inodes_before_operation,
    guint64 filler_bytes,
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
        "schema_version"
    );
    json_builder_add_int_value (
        builder,
        1
    );

    json_builder_set_member_name (
        builder,
        "scenario"
    );
    json_builder_add_string_value (
        builder,
        scenario
    );

    json_builder_set_member_name (
        builder,
        "classification"
    );
    json_builder_add_string_value (
        builder,
        classification
    );

    json_builder_set_member_name (
        builder,
        "qualified"
    );
    json_builder_add_boolean_value (
        builder,
        qualified
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
        "old_sha"
    );
    json_builder_add_string_value (
        builder,
        old_sha
    );

    json_builder_set_member_name (
        builder,
        "active_sha_after"
    );
    if (active_sha_after != NULL) {
        json_builder_add_string_value (
            builder,
            active_sha_after
        );
    } else {
        json_builder_add_null_value (
            builder
        );
    }

    json_builder_set_member_name (
        builder,
        "available_bytes_before_operation"
    );
    json_builder_add_int_value (
        builder,
        (gint64) MIN (
            available_bytes_before_operation,
            (guint64) G_MAXINT64
        )
    );

    json_builder_set_member_name (
        builder,
        "available_inodes_before_operation"
    );
    json_builder_add_int_value (
        builder,
        (gint64) MIN (
            available_inodes_before_operation,
            (guint64) G_MAXINT64
        )
    );

    json_builder_set_member_name (
        builder,
        "filler_allocated_bytes"
    );
    json_builder_add_int_value (
        builder,
        (gint64) MIN (
            filler_bytes,
            (guint64) G_MAXINT64
        )
    );

    json_builder_set_member_name (
        builder,
        "control_db_allocated_bytes"
    );
    json_builder_add_int_value (
        builder,
        (gint64) allocated_bytes (
            control_path
        )
    );

    json_builder_set_member_name (
        builder,
        "wal_allocated_bytes"
    );
    json_builder_add_int_value (
        builder,
        (gint64) allocated_bytes (
            wal_path
        )
    );

    json_builder_set_member_name (
        builder,
        "shm_allocated_bytes"
    );
    json_builder_add_int_value (
        builder,
        (gint64) allocated_bytes (
            shm_path
        )
    );

    json_builder_set_member_name (
        builder,
        "operation_error"
    );

    if (operation_error != NULL) {
        json_builder_begin_object (builder);

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

        json_builder_end_object (builder);
    } else {
        json_builder_add_null_value (
            builder
        );
    }

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
}

static int
run_exercise (
    const char *state_root,
    const char *repository_id,
    const char *old_sha,
    const char *new_sha,
    guint64 leave_bytes
)
{
    GError *error = NULL;
    GError *operation_error = NULL;
    char *active_before = NULL;
    char *active_after = NULL;
    char *filler_path = NULL;
    char *control_path =
        control_path_for (
            state_root
        );
    gint64 generation_before = 0;
    gint64 generation_after = 0;
    gint64 attempted_generation = 0;
    guint64 filler_bytes = 0;
    guint64 available_bytes = 0;
    guint64 available_inodes = 0;
    gboolean mutation_ok;
    gboolean qualified;

    if (!seed_old_authority (
            state_root,
            repository_id,
            old_sha,
            &error
        ) ||
        !load_active (
            state_root,
            repository_id,
            &generation_before,
            &active_before,
            &error
        ) ||
        !consume_capacity (
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
            "C0-E3 setup failed: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_remove (filler_path);
        g_free (filler_path);
        g_free (active_before);
        g_free (control_path);
        return 2;
    }

    mutation_ok =
        atm_control_state_set_current_values_guarded (
            control_path,
            generation_before,
            repository_id,
            new_sha,
            TEST_VERSION,
            NEW_SEAL,
            &attempted_generation,
            &operation_error
        );

    /*
     * The filler is qualification-owned, never application
     * state. Remove only this file before any post-failure
     * inspection so the database has room to reopen/rollback/
     * checkpoint if needed.
     */
    g_remove (filler_path);

    gboolean reload_ok =
        load_active (
            state_root,
            repository_id,
            &generation_after,
            &active_after,
            &error
        );

    qualified =
        !mutation_ok &&
        error_is_capacity_failure (
            operation_error
        ) &&
        reload_ok &&
        generation_after ==
            generation_before &&
        g_strcmp0 (
            active_before,
            old_sha
        ) == 0 &&
        g_strcmp0 (
            active_after,
            old_sha
        ) == 0 &&
        attempted_generation == 0;

    emit_result (
        "state-control-db-enospc",
        qualified,
        qualified
            ? "FAIL_CLOSED_OLD_AUTHORITY"
            : "UNQUALIFIED",
        generation_before,
        reload_ok
            ? generation_after
            : 0,
        old_sha,
        active_after,
        available_bytes,
        available_inodes,
        filler_bytes,
        control_path,
        operation_error
    );

    if (!reload_ok && error != NULL) {
        g_printerr (
            "C0-E3 same-process reload failed: %s\n",
            error->message
        );
    }

    g_clear_error (&error);
    g_clear_error (&operation_error);
    g_free (filler_path);
    g_free (active_after);
    g_free (active_before);
    g_free (control_path);
    return qualified ? 0 : 3;
}

static int
run_verify (
    const char *state_root,
    const char *repository_id,
    const char *old_sha
)
{
    GError *error = NULL;
    char *active_sha = NULL;
    char *control_path =
        control_path_for (
            state_root
        );
    AtmControlStateStore *store = NULL;
    gint64 generation = 0;
    gboolean qualified = FALSE;

    if (load_active (
            state_root,
            repository_id,
            &generation,
            &active_sha,
            &error
        ) &&
        generation > 0 &&
        g_strcmp0 (
            active_sha,
            old_sha
        ) == 0 &&
        atm_control_state_open (
            control_path,
            &store,
            &error
        ) &&
        atm_control_state_validate (
            store,
            &error
        )) {
        qualified = TRUE;
    }

    emit_result (
        "state-control-db-enospc-restart",
        qualified,
        qualified
            ? "RESTART_OLD_AUTHORITY_VALID"
            : "UNQUALIFIED",
        generation,
        generation,
        old_sha,
        active_sha,
        0,
        0,
        0,
        control_path,
        NULL
    );

    if (!qualified && error != NULL) {
        g_printerr (
            "C0-E3 restart verification failed: %s\n",
            error->message
        );
    }

    if (store != NULL) {
        atm_control_state_close (store);
    }
    g_clear_error (&error);
    g_free (active_sha);
    g_free (control_path);
    return qualified ? 0 : 4;
}

int
main (
    int argc,
    char **argv
)
{
    if (argc == 7 &&
        g_strcmp0 (
            argv[1],
            "--exercise"
        ) == 0) {
        char *end = NULL;
        guint64 leave_bytes =
            g_ascii_strtoull (
                argv[6],
                &end,
                10
            );

        if (end == NULL ||
            *end != '\0') {
            g_printerr (
                "Invalid leave-bytes value.\n"
            );
            return 64;
        }

        return run_exercise (
            argv[2],
            argv[3],
            argv[4],
            argv[5],
            leave_bytes
        );
    }

    if (argc == 5 &&
        g_strcmp0 (
            argv[1],
            "--verify"
        ) == 0) {
        return run_verify (
            argv[2],
            argv[3],
            argv[4]
        );
    }

    g_printerr (
        "Usage:\n"
        "  capacity-state-enospc-runner --exercise "
        "STATE_ROOT REPOSITORY_ID OLD_SHA NEW_SHA LEAVE_BYTES\n"
        "  capacity-state-enospc-runner --verify "
        "STATE_ROOT REPOSITORY_ID OLD_SHA\n"
    );

    return 64;
}
