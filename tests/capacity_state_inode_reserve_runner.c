#include "control_state.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const char *TEST_VERSION = "0.1.0";
static const char *OLD_SHA =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *NEW_SHA =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char *SEAL =
    "cccccccccccccccccccccccccccccccc"
    "cccccccccccccccccccccccccccccccc";

static char *
control_path_for (const char *state_root)
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

    if (statvfs (path, &fs) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not measure state filesystem: %s",
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
            "State capacity arithmetic overflowed."
        );
        return FALSE;
    }

    *out_bytes =
        (guint64) fs.f_bavail * unit;
    *out_inodes =
        (guint64) fs.f_favail;
    return TRUE;
}

static gboolean
load_active (
    const char *state_root,
    gint64 *out_generation,
    char **out_sha,
    GError **error
)
{
    char *path = control_path_for (state_root);
    gboolean present = FALSE;
    char *version = NULL;
    char *seal = NULL;
    gboolean ok = FALSE;

    *out_generation = 0;
    *out_sha = NULL;

    if (atm_control_state_active_generation_id (
            path,
            out_generation,
            error
        ) &&
        *out_generation > 0 &&
        atm_control_state_load_repository_values_at_generation (
            path,
            *out_generation,
            "rmd",
            &present,
            out_sha,
            &version,
            &seal,
            error
        ) &&
        present &&
        *out_sha != NULL) {
        ok = TRUE;
    }

    g_free (seal);
    g_free (version);
    g_free (path);
    return ok;
}

static gboolean
count_candidates (
    const char *path,
    gint64 *out_count,
    GError **error
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    *out_count = -1;

    if (sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not open Control DB for candidate count: %s",
            db != NULL ? sqlite3_errmsg (db) : "unknown SQLite error"
        );
        goto out;
    }

    if (sqlite3_prepare_v2 (
            db,
            "SELECT COUNT(*) FROM repository_generations "
            "WHERE lifecycle='CANDIDATE';",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK ||
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

    *out_count = sqlite3_column_int64 (statement, 0);
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
validate_store (
    const char *path,
    GError **error
)
{
    AtmControlStateStore *store = NULL;

    if (!atm_control_state_open (
            path,
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
seed_state (
    const char *state_root,
    GError **error
)
{
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

    char *path = control_path_for (state_root);
    AtmControlStateStore *store = NULL;

    if (!atm_control_state_open (
            path,
            &store,
            error
        )) {
        g_free (path);
        return FALSE;
    }

    atm_control_state_close (store);

    gboolean ok =
        atm_control_state_set_current_values (
            path,
            "rmd",
            OLD_SHA,
            TEST_VERSION,
            SEAL,
            error
        );

    g_free (path);
    return ok;
}

static gboolean
consume_inodes (
    const char *state_root,
    guint64 leave_inodes,
    char **out_fill_dir,
    guint64 *out_created_files,
    GError **error
)
{
    char *fill_dir =
        g_build_filename (
            state_root,
            "atm-c0p-m7-inode-fill",
            NULL
        );

    if (g_mkdir (fill_dir, 0700) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create inode filler directory: %s",
            g_strerror (errno)
        );
        g_free (fill_dir);
        return FALSE;
    }

    guint64 bytes = 0;
    guint64 inodes = 0;
    guint64 created = 0;

    while (TRUE) {
        if (!measure_available (
                state_root,
                &bytes,
                &inodes,
                error
            )) {
            goto fail;
        }

        if (inodes <= leave_inodes) {
            break;
        }

        char *name =
            g_strdup_printf (
                "f-%" G_GUINT64_FORMAT,
                created
            );
        char *path =
            g_build_filename (
                fill_dir,
                name,
                NULL
            );

        int fd = g_open (
            path,
            O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

        g_free (path);
        g_free (name);

        if (fd < 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not consume inode capacity: %s",
                g_strerror (errno)
            );
            goto fail;
        }

        close (fd);
        created++;
    }

    if (inodes != leave_inodes) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not reach requested inode headroom: requested=%"
            G_GUINT64_FORMAT " actual=%" G_GUINT64_FORMAT,
            leave_inodes,
            inodes
        );
        goto fail;
    }

    *out_fill_dir = fill_dir;
    *out_created_files = created;
    return TRUE;

fail:
    if (fill_dir != NULL) {
        GDir *dir = g_dir_open (fill_dir, 0, NULL);
        if (dir != NULL) {
            const char *name;
            while ((name = g_dir_read_name (dir)) != NULL) {
                char *path =
                    g_build_filename (
                        fill_dir,
                        name,
                        NULL
                    );
                g_remove (path);
                g_free (path);
            }
            g_dir_close (dir);
        }
        g_rmdir (fill_dir);
    }
    g_free (fill_dir);
    return FALSE;
}

static void
remove_fill_dir (
    const char *fill_dir
)
{
    if (fill_dir == NULL) {
        return;
    }

    GDir *dir = g_dir_open (fill_dir, 0, NULL);
    if (dir != NULL) {
        const char *name;
        while ((name = g_dir_read_name (dir)) != NULL) {
            char *path =
                g_build_filename (
                    fill_dir,
                    name,
                    NULL
                );
            g_remove (path);
            g_free (path);
        }
        g_dir_close (dir);
    }

    g_rmdir (fill_dir);
}

static void
emit_result (
    guint64 leave_inodes,
    guint64 available_bytes_before,
    guint64 available_inodes_before,
    guint64 filler_files,
    gboolean mutation_ok,
    gboolean invariant_ok,
    gint64 generation_before,
    gint64 generation_after,
    gint64 attempted_generation,
    gint64 candidates,
    const char *active_before,
    const char *active_after,
    const GError *operation_error
)
{
    JsonBuilder *builder = json_builder_new ();

    json_builder_begin_object (builder);

#define ADD_U64(key, value)     G_STMT_START {         json_builder_set_member_name (builder, key);         json_builder_add_int_value (             builder,             (gint64) MIN ((guint64) (value), (guint64) G_MAXINT64)         );     } G_STMT_END

    json_builder_set_member_name (builder, "schema_version");
    json_builder_add_int_value (builder, 1);

    ADD_U64 ("leave_inodes", leave_inodes);
    ADD_U64 ("available_bytes_before_operation", available_bytes_before);
    ADD_U64 ("available_inodes_before_operation", available_inodes_before);
    ADD_U64 ("filler_files", filler_files);

    json_builder_set_member_name (builder, "mutation_ok");
    json_builder_add_boolean_value (builder, mutation_ok);
    json_builder_set_member_name (builder, "invariant_ok");
    json_builder_add_boolean_value (builder, invariant_ok);

    json_builder_set_member_name (builder, "classification");
    json_builder_add_string_value (
        builder,
        mutation_ok
            ? "SUCCESS_ATOMIC"
            : invariant_ok
                ? "FAIL_CLOSED_OLD_AUTHORITY"
                : "INVARIANT_FAILURE"
    );

    json_builder_set_member_name (builder, "generation_before");
    json_builder_add_int_value (builder, generation_before);
    json_builder_set_member_name (builder, "generation_after");
    json_builder_add_int_value (builder, generation_after);
    json_builder_set_member_name (builder, "attempted_generation");
    json_builder_add_int_value (builder, attempted_generation);
    json_builder_set_member_name (builder, "candidate_generations");
    json_builder_add_int_value (builder, candidates);

    json_builder_set_member_name (builder, "active_sha_before");
    json_builder_add_string_value (builder, active_before);
    json_builder_set_member_name (builder, "active_sha_after");
    json_builder_add_string_value (builder, active_after);

    json_builder_set_member_name (builder, "operation_error");
    if (operation_error == NULL) {
        json_builder_add_null_value (builder);
    } else {
        json_builder_begin_object (builder);
        json_builder_set_member_name (builder, "domain");
        json_builder_add_string_value (
            builder,
            g_quark_to_string (operation_error->domain)
        );
        json_builder_set_member_name (builder, "code");
        json_builder_add_int_value (builder, operation_error->code);
        json_builder_set_member_name (builder, "message");
        json_builder_add_string_value (builder, operation_error->message);
        json_builder_end_object (builder);
    }

#undef ADD_U64

    json_builder_end_object (builder);

    JsonNode *root = json_builder_get_root (builder);
    JsonGenerator *generator = json_generator_new ();

    json_generator_set_root (generator, root);
    json_generator_set_pretty (generator, FALSE);

    char *json = json_generator_to_data (generator, NULL);
    g_print ("%s\n", json);

    g_free (json);
    g_object_unref (generator);
    json_node_free (root);
    g_object_unref (builder);
}

int
main (
    int argc,
    char **argv
)
{
    if (argc != 4 ||
        g_strcmp0 (argv[1], "--exercise") != 0) {
        g_printerr (
            "Usage: capacity-state-inode-reserve-runner "
            "--exercise STATE_ROOT LEAVE_INODES\n"
        );
        return 64;
    }

    char *end = NULL;
    guint64 leave_inodes =
        g_ascii_strtoull (
            argv[3],
            &end,
            10
        );

    if (end == NULL ||
        *end != '\0') {
        return 64;
    }

    GError *error = NULL;
    GError *operation_error = NULL;
    char *control_path = control_path_for (argv[2]);
    char *fill_dir = NULL;
    char *active_before = NULL;
    char *active_after = NULL;
    guint64 filler_files = 0;
    guint64 available_bytes = 0;
    guint64 available_inodes = 0;
    gint64 generation_before = 0;
    gint64 generation_after = 0;
    gint64 attempted_generation = 0;
    gint64 candidates = -1;

    if (!seed_state (argv[2], &error) ||
        !load_active (
            argv[2],
            &generation_before,
            &active_before,
            &error
        ) ||
        !consume_inodes (
            argv[2],
            leave_inodes,
            &fill_dir,
            &filler_files,
            &error
        ) ||
        !measure_available (
            argv[2],
            &available_bytes,
            &available_inodes,
            &error
        )) {
        g_printerr (
            "C0P-M7 setup failed: %s\n",
            error != NULL ? error->message : "unknown error"
        );
        remove_fill_dir (fill_dir);
        g_clear_error (&error);
        g_free (fill_dir);
        g_free (active_before);
        g_free (control_path);
        return 2;
    }

    gboolean mutation_ok =
        atm_control_state_set_current_values_guarded (
            control_path,
            generation_before,
            "rmd",
            NEW_SHA,
            TEST_VERSION,
            SEAL,
            &attempted_generation,
            &operation_error
        );

    remove_fill_dir (fill_dir);

    gboolean reload_ok =
        load_active (
            argv[2],
            &generation_after,
            &active_after,
            &error
        );
    gboolean candidates_ok =
        count_candidates (
            control_path,
            &candidates,
            &error
        );
    gboolean validate_ok =
        validate_store (
            control_path,
            &error
        );

    gboolean success =
        mutation_ok &&
        reload_ok &&
        candidates_ok &&
        validate_ok &&
        candidates == 0 &&
        generation_after == generation_before + 1 &&
        attempted_generation == generation_after &&
        g_strcmp0 (active_after, NEW_SHA) == 0;

    gboolean fail_closed =
        !mutation_ok &&
        reload_ok &&
        candidates_ok &&
        validate_ok &&
        candidates == 0 &&
        generation_after == generation_before &&
        attempted_generation == 0 &&
        g_strcmp0 (active_after, active_before) == 0;

    gboolean invariant_ok =
        success || fail_closed;

    emit_result (
        leave_inodes,
        available_bytes,
        available_inodes,
        filler_files,
        mutation_ok,
        invariant_ok,
        generation_before,
        reload_ok ? generation_after : 0,
        attempted_generation,
        candidates,
        active_before != NULL ? active_before : "",
        active_after != NULL ? active_after : "",
        operation_error
    );

    if (!invariant_ok && error != NULL) {
        g_printerr (
            "C0P-M7 invariant failure: %s\n",
            error->message
        );
    }

    g_clear_error (&error);
    g_clear_error (&operation_error);
    g_free (fill_dir);
    g_free (active_after);
    g_free (active_before);
    g_free (control_path);

    return invariant_ok ? 0 : 3;
}
