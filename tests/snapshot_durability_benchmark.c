#include "archive_extract.h"
#include "repository_ingest.h"
#include "repository_manifest.h"
#include "snapshot_seal.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    STRATEGY_S3_CONTROL,
    STRATEGY_S1_TARGETED,
    STRATEGY_S2_SYNCFS
} Strategy;

typedef struct {
    guint64 file_fsync_calls;
    guint64 directory_fsync_calls;
    guint64 syncfs_calls;
} BarrierCounters;

static void
remove_tree_best_effort (
    const char *path
)
{
    GStatBuf st;

    if (path == NULL ||
        g_lstat (path, &st) != 0) {
        return;
    }

    if (!S_ISDIR (st.st_mode) ||
        S_ISLNK (st.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (
        path,
        0,
        &error
    );

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static gboolean
fsync_retry (
    int fd,
    const char *context,
    GError **error
)
{
    for (;;) {
        if (fsync (fd) == 0) {
            return TRUE;
        }

        if (errno == EINTR) {
            continue;
        }

        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "%s: %s",
            context,
            g_strerror (errno)
        );
        return FALSE;
    }
}

static gboolean
sync_tree_directory (
    int directory_fd,
    BarrierCounters *counters,
    GError **error
)
{
    int scan_fd = dup (directory_fd);
    DIR *directory = NULL;

    if (scan_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not duplicate directory descriptor: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    directory = fdopendir (scan_fd);
    if (directory == NULL) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not enumerate durability benchmark tree: %s",
            g_strerror (errno)
        );
        close (scan_fd);
        return FALSE;
    }

    struct dirent *item;

    while ((item = readdir (directory)) != NULL) {
        struct stat st;

        if (strcmp (item->d_name, ".") == 0 ||
            strcmp (item->d_name, "..") == 0) {
            continue;
        }

        if (fstatat (
                directory_fd,
                item->d_name,
                &st,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not inspect durability benchmark entry: %s",
                g_strerror (errno)
            );
            closedir (directory);
            return FALSE;
        }

        if (S_ISLNK (st.st_mode) ||
            (!S_ISREG (st.st_mode) &&
             !S_ISDIR (st.st_mode))) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_INVAL,
                "Durability benchmark tree contains an unsupported entry."
            );
            closedir (directory);
            return FALSE;
        }

        if (S_ISDIR (st.st_mode)) {
            int child_fd = openat (
                directory_fd,
                item->d_name,
                O_RDONLY | O_DIRECTORY |
                    O_NOFOLLOW | O_CLOEXEC
            );

            if (child_fd < 0) {
                g_set_error (
                    error,
                    G_FILE_ERROR,
                    g_file_error_from_errno (errno),
                    "Could not open durability benchmark directory: %s",
                    g_strerror (errno)
                );
                closedir (directory);
                return FALSE;
            }

            gboolean ok = sync_tree_directory (
                child_fd,
                counters,
                error
            );
            close (child_fd);

            if (!ok) {
                closedir (directory);
                return FALSE;
            }

            continue;
        }

        int file_fd = openat (
            directory_fd,
            item->d_name,
            O_RDONLY | O_NOFOLLOW | O_CLOEXEC
        );

        if (file_fd < 0) {
            g_set_error (
                error,
                G_FILE_ERROR,
                g_file_error_from_errno (errno),
                "Could not open durability benchmark file: %s",
                g_strerror (errno)
            );
            closedir (directory);
            return FALSE;
        }

        if (!fsync_retry (
                file_fd,
                "Could not fsync durability benchmark file",
                error
            )) {
            close (file_fd);
            closedir (directory);
            return FALSE;
        }

        counters->file_fsync_calls++;
        close (file_fd);
    }

    closedir (directory);

    if (!fsync_retry (
            directory_fd,
            "Could not fsync durability benchmark directory",
            error
        )) {
        return FALSE;
    }

    counters->directory_fsync_calls++;
    return TRUE;
}

static gboolean
run_targeted_barrier (
    const char *snapshot_root,
    BarrierCounters *counters,
    GError **error
)
{
    int root_fd = open (
        snapshot_root,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (root_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open benchmark snapshot root: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    gboolean ok = sync_tree_directory (
        root_fd,
        counters,
        error
    );

    close (root_fd);
    return ok;
}

static gboolean
run_syncfs_barrier (
    const char *snapshot_root,
    BarrierCounters *counters,
    GError **error
)
{
    int root_fd = open (
        snapshot_root,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (root_fd < 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not open benchmark snapshot root for syncfs: %s",
            g_strerror (errno)
        );
        return FALSE;
    }

    int rc;
    do {
        rc = syncfs (root_fd);
    } while (rc != 0 && errno == EINTR);

    if (rc != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "syncfs durability benchmark failed: %s",
            g_strerror (errno)
        );
        close (root_fd);
        return FALSE;
    }

    counters->syncfs_calls++;
    close (root_fd);
    return TRUE;
}

static const char *
strategy_name (
    Strategy strategy
)
{
    switch (strategy) {
        case STRATEGY_S3_CONTROL:
            return "S3_CONTROL";
        case STRATEGY_S1_TARGETED:
            return "S1_TARGETED_FSYNC";
        case STRATEGY_S2_SYNCFS:
            return "S2_SYNCFS";
        default:
            return "UNKNOWN";
    }
}

static gboolean
run_one (
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *archive_path,
    const char *work_root,
    Strategy strategy,
    guint iteration,
    JsonBuilder *builder,
    GError **error
)
{
    char *strategy_dir = g_strdup_printf (
        "%s/%s-%u",
        work_root,
        strategy_name (strategy),
        iteration
    );
    char *snapshot_root = g_build_filename (
        strategy_dir,
        "snapshot",
        NULL
    );

    remove_tree_best_effort (strategy_dir);

    if (g_mkdir_with_parents (
            strategy_dir,
            0700
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create durability benchmark root: %s",
            g_strerror (errno)
        );
        g_free (snapshot_root);
        g_free (strategy_dir);
        return FALSE;
    }

    AtmArchiveLimits limits = {
        .max_entries = ATM_INGEST_MAX_ENTRIES,
        .max_file_bytes = ATM_INGEST_MAX_FILE_BYTES,
        .max_total_bytes = ATM_INGEST_MAX_TOTAL_BYTES
    };

    guint64 entries = 0;
    guint64 extracted_bytes = 0;
    gint64 extraction_started = g_get_monotonic_time ();

    if (!atm_archive_extract_snapshot (
            archive_path,
            snapshot_root,
            &limits,
            &entries,
            &extracted_bytes,
            error
        )) {
        g_free (snapshot_root);
        remove_tree_best_effort (strategy_dir);
        g_free (strategy_dir);
        return FALSE;
    }

    gint64 extraction_elapsed_us =
        g_get_monotonic_time () -
        extraction_started;

    char *version = NULL;
    gint64 validation_started = g_get_monotonic_time ();

    if (!atm_repository_validate_snapshot (
            snapshot_root,
            repository_id,
            repository_acronym,
            repository_display_name,
            &version,
            error
        )) {
        g_free (snapshot_root);
        remove_tree_best_effort (strategy_dir);
        g_free (strategy_dir);
        return FALSE;
    }

    gint64 validation_elapsed_us =
        g_get_monotonic_time () -
        validation_started;

    char *seal_sha256 = NULL;
    guint64 seal_file_count = 0;
    guint64 seal_total_bytes = 0;
    gint64 seal_started = g_get_monotonic_time ();

    if (!atm_snapshot_seal_compute (
            snapshot_root,
            &seal_sha256,
            &seal_file_count,
            &seal_total_bytes,
            error
        )) {
        g_free (version);
        g_free (snapshot_root);
        remove_tree_best_effort (strategy_dir);
        g_free (strategy_dir);
        return FALSE;
    }

    gint64 seal_elapsed_us =
        g_get_monotonic_time () -
        seal_started;

    BarrierCounters counters = { 0 };
    gint64 barrier_started = g_get_monotonic_time ();
    gboolean barrier_ok = TRUE;

    if (strategy == STRATEGY_S1_TARGETED) {
        barrier_ok = run_targeted_barrier (
            snapshot_root,
            &counters,
            error
        );
    } else if (strategy == STRATEGY_S2_SYNCFS) {
        barrier_ok = run_syncfs_barrier (
            snapshot_root,
            &counters,
            error
        );
    }

    gint64 barrier_elapsed_us =
        g_get_monotonic_time () -
        barrier_started;

    if (!barrier_ok) {
        g_free (seal_sha256);
        g_free (version);
        g_free (snapshot_root);
        remove_tree_best_effort (strategy_dir);
        g_free (strategy_dir);
        return FALSE;
    }

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "strategy");
    json_builder_add_string_value (
        builder,
        strategy_name (strategy)
    );
    json_builder_set_member_name (builder, "iteration");
    json_builder_add_int_value (builder, iteration);
    json_builder_set_member_name (
        builder,
        "extraction_elapsed_us"
    );
    json_builder_add_int_value (
        builder,
        extraction_elapsed_us
    );
    json_builder_set_member_name (
        builder,
        "validation_elapsed_us"
    );
    json_builder_add_int_value (
        builder,
        validation_elapsed_us
    );
    json_builder_set_member_name (
        builder,
        "seal_elapsed_us"
    );
    json_builder_add_int_value (
        builder,
        seal_elapsed_us
    );
    json_builder_set_member_name (
        builder,
        "barrier_elapsed_us"
    );
    json_builder_add_int_value (
        builder,
        barrier_elapsed_us
    );
    json_builder_set_member_name (
        builder,
        "prepared_elapsed_us"
    );
    json_builder_add_int_value (
        builder,
        extraction_elapsed_us +
        validation_elapsed_us +
        seal_elapsed_us +
        barrier_elapsed_us
    );
    json_builder_set_member_name (
        builder,
        "archive_materialized_entries"
    );
    json_builder_add_int_value (builder, entries);
    json_builder_set_member_name (
        builder,
        "extracted_logical_bytes"
    );
    json_builder_add_int_value (
        builder,
        extracted_bytes
    );
    json_builder_set_member_name (
        builder,
        "seal_file_count"
    );
    json_builder_add_int_value (
        builder,
        seal_file_count
    );
    json_builder_set_member_name (
        builder,
        "seal_total_bytes"
    );
    json_builder_add_int_value (
        builder,
        seal_total_bytes
    );
    json_builder_set_member_name (
        builder,
        "file_fsync_calls"
    );
    json_builder_add_int_value (
        builder,
        counters.file_fsync_calls
    );
    json_builder_set_member_name (
        builder,
        "directory_fsync_calls"
    );
    json_builder_add_int_value (
        builder,
        counters.directory_fsync_calls
    );
    json_builder_set_member_name (
        builder,
        "syncfs_calls"
    );
    json_builder_add_int_value (
        builder,
        counters.syncfs_calls
    );
    json_builder_set_member_name (
        builder,
        "repository_version"
    );
    json_builder_add_string_value (
        builder,
        version
    );
    json_builder_set_member_name (
        builder,
        "seal_sha256"
    );
    json_builder_add_string_value (
        builder,
        seal_sha256
    );
    json_builder_end_object (builder);

    g_free (seal_sha256);
    g_free (version);
    g_free (snapshot_root);
    remove_tree_best_effort (strategy_dir);
    g_free (strategy_dir);
    return TRUE;
}

int
main (
    int argc,
    char **argv
)
{
    if (argc != 9) {
        g_printerr (
            "Usage: %s ATM_SOURCE_COMMIT REPO_ID ACRONYM DISPLAY_NAME REPO_SHA ARCHIVE WORK_ROOT OUTPUT_JSON\n",
            argv[0]
        );
        return 2;
    }

    const char *atm_source_commit = argv[1];
    const char *repository_id = argv[2];
    const char *repository_acronym = argv[3];
    const char *repository_display_name = argv[4];
    const char *repository_sha = argv[5];
    const char *archive_path = argv[6];
    const char *work_root = argv[7];
    const char *output_path = argv[8];

    if (g_mkdir_with_parents (
            work_root,
            0700
        ) != 0) {
        g_printerr (
            "Could not create durability benchmark work root: %s\n",
            g_strerror (errno)
        );
        return 2;
    }

    JsonBuilder *builder = json_builder_new ();
    json_builder_begin_object (builder);
    json_builder_set_member_name (
        builder,
        "schema_version"
    );
    json_builder_add_int_value (builder, 1);
    json_builder_set_member_name (
        builder,
        "measurement_id"
    );
    json_builder_add_string_value (
        builder,
        "atm-a1-m1-durability-barrier-v1"
    );
    json_builder_set_member_name (
        builder,
        "atm_source_commit"
    );
    json_builder_add_string_value (
        builder,
        atm_source_commit
    );
    json_builder_set_member_name (
        builder,
        "repository_id"
    );
    json_builder_add_string_value (
        builder,
        repository_id
    );
    json_builder_set_member_name (
        builder,
        "repository_sha"
    );
    json_builder_add_string_value (
        builder,
        repository_sha
    );
    json_builder_set_member_name (
        builder,
        "runs"
    );
    json_builder_begin_array (builder);

    static const Strategy orders[3][3] = {
        {
            STRATEGY_S3_CONTROL,
            STRATEGY_S1_TARGETED,
            STRATEGY_S2_SYNCFS
        },
        {
            STRATEGY_S1_TARGETED,
            STRATEGY_S2_SYNCFS,
            STRATEGY_S3_CONTROL
        },
        {
            STRATEGY_S2_SYNCFS,
            STRATEGY_S3_CONTROL,
            STRATEGY_S1_TARGETED
        }
    };

    GError *error = NULL;

    for (guint iteration = 0;
         iteration < 3;
         iteration++) {
        for (guint order = 0;
             order < 3;
             order++) {
            if (!run_one (
                    repository_id,
                    repository_acronym,
                    repository_display_name,
                    archive_path,
                    work_root,
                    orders[iteration][order],
                    iteration + 1,
                    builder,
                    &error
                )) {
                g_printerr (
                    "A1-M1 benchmark failed: %s\n",
                    error != NULL
                        ? error->message
                        : "unknown error"
                );
                g_clear_error (&error);
                g_object_unref (builder);
                return 1;
            }
        }
    }

    json_builder_end_array (builder);
    json_builder_end_object (builder);

    JsonGenerator *generator =
        json_generator_new ();
    JsonNode *root =
        json_builder_get_root (builder);
    json_generator_set_root (
        generator,
        root
    );
    json_generator_set_pretty (
        generator,
        TRUE
    );

    gboolean ok =
        json_generator_to_file (
            generator,
            output_path,
            &error
        );

    if (!ok) {
        g_printerr (
            "Could not write durability benchmark JSON: %s\n",
            error->message
        );
        g_clear_error (&error);
    }

    json_node_free (root);
    g_object_unref (generator);
    g_object_unref (builder);
    return ok ? 0 : 1;
}
