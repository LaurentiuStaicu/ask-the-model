#include "capacity_measurement.h"
#include "repository_ingest.h"
#include "repository_storage.h"
#include "repository_sources.h"
#include "retrieval_index.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <errno.h>
#include <sys/stat.h>

typedef struct {
    const char *root;
    gint stop;
    GMutex mutex;
    guint64 peak_allocated_bytes;
} PeakSampler;

static guint64
allocated_bytes_best_effort (
    const char *path
)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0) {
        return 0;
    }

    if (S_ISLNK (st.st_mode) ||
        (!S_ISREG (st.st_mode) &&
         !S_ISDIR (st.st_mode))) {
        return 0;
    }

    guint64 total =
        st.st_blocks > 0
            ? (guint64) st.st_blocks * 512
            : 0;

    if (!S_ISDIR (st.st_mode)) {
        return total;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (
        path,
        0,
        &error
    );

    if (directory == NULL) {
        g_clear_error (&error);
        return total;
    }

    const char *name;
    while ((name = g_dir_read_name (directory)) != NULL) {
        char *child = g_build_filename (
            path,
            name,
            NULL
        );
        total += allocated_bytes_best_effort (child);
        g_free (child);
    }

    g_dir_close (directory);
    return total;
}

static gpointer
peak_sampler_thread (
    gpointer user_data
)
{
    PeakSampler *sampler = user_data;

    while (!g_atomic_int_get (&sampler->stop)) {
        guint64 current =
            allocated_bytes_best_effort (
                sampler->root
            );

        g_mutex_lock (&sampler->mutex);
        if (current > sampler->peak_allocated_bytes) {
            sampler->peak_allocated_bytes = current;
        }
        g_mutex_unlock (&sampler->mutex);

        g_usleep (1000);
    }

    guint64 final =
        allocated_bytes_best_effort (
            sampler->root
        );

    g_mutex_lock (&sampler->mutex);
    if (final > sampler->peak_allocated_bytes) {
        sampler->peak_allocated_bytes = final;
    }
    g_mutex_unlock (&sampler->mutex);

    return NULL;
}

static GThread *
peak_sampler_start (
    PeakSampler *sampler,
    const char *root
)
{
    memset (sampler, 0, sizeof (*sampler));
    sampler->root = root;
    g_mutex_init (&sampler->mutex);

    return g_thread_new (
        "capacity-peak-sampler",
        peak_sampler_thread,
        sampler
    );
}

static guint64
peak_sampler_stop (
    PeakSampler *sampler,
    GThread *thread
)
{
    guint64 peak;

    g_atomic_int_set (
        &sampler->stop,
        TRUE
    );
    g_thread_join (thread);

    g_mutex_lock (&sampler->mutex);
    peak = sampler->peak_allocated_bytes;
    g_mutex_unlock (&sampler->mutex);
    g_mutex_clear (&sampler->mutex);

    return peak;
}

static void
add_u64 (
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
        (gint64) MIN (
            value,
            (guint64) G_MAXINT64
        )
    );
}

static void
add_fs (
    JsonBuilder *builder,
    const char *name,
    const char *path
)
{
    AtmFilesystemCapacityMeasurement m;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_measure_filesystem (
            path,
            &m,
            &error
        )
    );
    g_assert_no_error (error);

    char *device = g_strdup_printf (
        "%" G_GUINT64_FORMAT,
        m.device_id
    );

    json_builder_set_member_name (
        builder,
        name
    );
    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "st_dev"
    );
    json_builder_add_string_value (
        builder,
        device
    );
    add_u64 (
        builder,
        "available_bytes_before",
        m.available_bytes
    );
    add_u64 (
        builder,
        "available_inodes_before",
        m.available_inodes
    );
    add_u64 (
        builder,
        "fragment_size",
        m.fragment_size
    );

    json_builder_end_object (builder);
    g_free (device);
}

static void
add_file (
    JsonBuilder *builder,
    const char *name,
    const char *path
)
{
    AtmFileCapacityMeasurement m;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_measure_regular_file (
            path,
            &m,
            &error
        )
    );
    g_assert_no_error (error);

    json_builder_set_member_name (
        builder,
        name
    );
    json_builder_begin_object (builder);
    add_u64 (
        builder,
        "logical_bytes",
        m.logical_bytes
    );
    add_u64 (
        builder,
        "allocated_bytes",
        m.allocated_bytes
    );
    json_builder_end_object (builder);
}

static void
add_tree (
    JsonBuilder *builder,
    const char *name,
    const char *path
)
{
    AtmTreeCapacityMeasurement m;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_measure_tree (
            path,
            &m,
            &error
        )
    );
    g_assert_no_error (error);

    json_builder_set_member_name (
        builder,
        name
    );
    json_builder_begin_object (builder);
    add_u64 (
        builder,
        "logical_regular_bytes",
        m.logical_regular_bytes
    );
    add_u64 (
        builder,
        "allocated_tree_bytes",
        m.allocated_tree_bytes
    );
    add_u64 (
        builder,
        "entries",
        m.entries
    );
    add_u64 (
        builder,
        "regular_files",
        m.regular_files
    );
    add_u64 (
        builder,
        "directories",
        m.directories
    );
    json_builder_end_object (builder);
}

static gboolean
make_root (
    const char *path,
    GError **error
)
{
    if (g_mkdir_with_parents (
            path,
            0700
        ) == 0) {
        return TRUE;
    }

    g_set_error (
        error,
        G_FILE_ERROR,
        g_file_error_from_errno (errno),
        "Could not create measurement root '%s': %s",
        path,
        g_strerror (errno)
    );
    return FALSE;
}

static int
run_measurement (
    const char *atm_source_commit,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *repository_sha,
    const char *archive_path,
    const char *work_root,
    const char *output_path
)
{
    char *data_root = g_build_filename (
        work_root,
        "data",
        NULL
    );
    char *cache_root = g_build_filename (
        work_root,
        "cache",
        NULL
    );
    char *state_root = g_build_filename (
        work_root,
        "state",
        NULL
    );
    char *version = NULL;
    char *snapshot_path = NULL;
    char *index_path = NULL;
    char *quarantine_path = NULL;
    AtmSourceCatalog *catalog = NULL;
    GError *error = NULL;
    guint64 entries = 0;
    guint64 extracted_logical_bytes = 0;

    if (!make_root (data_root, &error) ||
        !make_root (cache_root, &error) ||
        !make_root (state_root, &error)) {
        g_printerr (
            "C0-M1 root setup failed: %s\n",
            error->message
        );
        g_clear_error (&error);
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
        "scenario"
    );
    json_builder_add_string_value (
        builder,
        "fresh-and-same-sha-repair"
    );

    json_builder_set_member_name (
        builder,
        "sampling_interval_us"
    );
    json_builder_add_int_value (
        builder,
        1000
    );

    json_builder_set_member_name (
        builder,
        "roots"
    );
    json_builder_begin_object (builder);
    add_fs (
        builder,
        "data",
        data_root
    );
    add_fs (
        builder,
        "cache",
        cache_root
    );
    add_fs (
        builder,
        "state",
        state_root
    );
    json_builder_end_object (builder);

    add_file (
        builder,
        "archive",
        archive_path
    );

    gint64 ingest_started =
        g_get_monotonic_time ();
    PeakSampler fresh_data_sampler;
    GThread *fresh_data_thread =
        peak_sampler_start (
            &fresh_data_sampler,
            data_root
        );

    if (!atm_repository_ingest_archive (
            data_root,
            archive_path,
            repository_id,
            repository_acronym,
            repository_display_name,
            repository_sha,
            &version,
            &snapshot_path,
            &entries,
            &extracted_logical_bytes,
            &error
        )) {
        peak_sampler_stop (
            &fresh_data_sampler,
            fresh_data_thread
        );
        g_printerr (
            "C0-M1 ingest failed: %s\n",
            error->message
        );
        g_clear_error (&error);
        return 3;
    }

    guint64 fresh_data_peak =
        peak_sampler_stop (
            &fresh_data_sampler,
            fresh_data_thread
        );
    gint64 ingest_elapsed_ms =
        (g_get_monotonic_time () -
         ingest_started) / 1000;

    add_tree (
        builder,
        "snapshot",
        snapshot_path
    );

    json_builder_set_member_name (
        builder,
        "ingest_report"
    );
    json_builder_begin_object (builder);
    add_u64 (
        builder,
        "reported_entries",
        entries
    );
    add_u64 (
        builder,
        "reported_regular_logical_bytes",
        extracted_logical_bytes
    );
    add_u64 (
        builder,
        "fresh_data_root_peak_allocated_bytes",
        fresh_data_peak
    );
    add_u64 (
        builder,
        "elapsed_ms",
        (guint64) ingest_elapsed_ms
    );
    json_builder_end_object (builder);

    if (!atm_repository_source_catalog_build (
            snapshot_path,
            repository_id,
            &catalog,
            &error
        )) {
        g_printerr (
            "C0-M1 source catalog failed: %s\n",
            error->message
        );
        g_clear_error (&error);
        return 4;
    }

    AtmRetrievalIndexMetadata metadata = {
        .repository_id = repository_id,
        .repository_version = version,
        .snapshot_sha = repository_sha,
        .manifest_schema_version = 1,
        .manifest_sha256 =
            catalog->manifest_sha256,
        .created_at_utc =
            "1970-01-01T00:00:00Z"
    };

    gint64 index_started =
        g_get_monotonic_time ();
    PeakSampler cache_sampler;
    GThread *cache_thread =
        peak_sampler_start (
            &cache_sampler,
            cache_root
        );

    if (!atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_path,
            &metadata,
            catalog,
            &index_path,
            &error
        )) {
        peak_sampler_stop (
            &cache_sampler,
            cache_thread
        );
        g_printerr (
            "C0-M1 index build failed: %s\n",
            error->message
        );
        g_clear_error (&error);
        return 5;
    }

    guint64 cache_peak =
        peak_sampler_stop (
            &cache_sampler,
            cache_thread
        );
    gint64 index_elapsed_ms =
        (g_get_monotonic_time () -
         index_started) / 1000;

    if (!atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_path,
            repository_id,
            repository_sha,
            &error
        )) {
        g_printerr (
            "C0-M1 index validation failed: %s\n",
            error->message
        );
        g_clear_error (&error);
        return 6;
    }

    add_file (
        builder,
        "index",
        index_path
    );

    json_builder_set_member_name (
        builder,
        "index_build"
    );
    json_builder_begin_object (builder);
    add_u64 (
        builder,
        "cache_root_peak_allocated_bytes",
        cache_peak
    );
    add_u64 (
        builder,
        "elapsed_ms",
        (guint64) index_elapsed_ms
    );
    json_builder_end_object (builder);

    if (!atm_repository_quarantine_snapshot (
            data_root,
            repository_id,
            repository_sha,
            &quarantine_path,
            &error
        )) {
        g_printerr (
            "C0-M1 quarantine failed: %s\n",
            error->message
        );
        g_clear_error (&error);
        return 7;
    }

    g_clear_pointer (
        &snapshot_path,
        g_free
    );
    g_clear_pointer (
        &version,
        g_free
    );

    gint64 repair_started =
        g_get_monotonic_time ();
    PeakSampler repair_sampler;
    GThread *repair_thread =
        peak_sampler_start (
            &repair_sampler,
            data_root
        );

    if (!atm_repository_ingest_archive (
            data_root,
            archive_path,
            repository_id,
            repository_acronym,
            repository_display_name,
            repository_sha,
            &version,
            &snapshot_path,
            &entries,
            &extracted_logical_bytes,
            &error
        )) {
        peak_sampler_stop (
            &repair_sampler,
            repair_thread
        );
        g_printerr (
            "C0-M1 same-SHA repair ingest failed: %s\n",
            error->message
        );
        g_clear_error (&error);
        return 8;
    }

    guint64 repair_data_peak =
        peak_sampler_stop (
            &repair_sampler,
            repair_thread
        );
    gint64 repair_elapsed_ms =
        (g_get_monotonic_time () -
         repair_started) / 1000;

    add_tree (
        builder,
        "quarantined_snapshot",
        quarantine_path
    );
    add_tree (
        builder,
        "replacement_snapshot",
        snapshot_path
    );

    json_builder_set_member_name (
        builder,
        "same_sha_repair"
    );
    json_builder_begin_object (builder);
    add_u64 (
        builder,
        "data_root_peak_allocated_bytes",
        repair_data_peak
    );
    add_u64 (
        builder,
        "elapsed_ms",
        (guint64) repair_elapsed_ms
    );
    json_builder_end_object (builder);

    json_builder_set_member_name (
        builder,
        "phase_peaks"
    );
    json_builder_begin_array (builder);

    json_builder_begin_object (builder);
    json_builder_set_member_name (
        builder,
        "phase"
    );
    json_builder_add_string_value (
        builder,
        "fresh_snapshot_ingest"
    );
    add_u64 (
        builder,
        "data_root_allocated_peak",
        fresh_data_peak
    );
    json_builder_end_object (builder);

    json_builder_begin_object (builder);
    json_builder_set_member_name (
        builder,
        "phase"
    );
    json_builder_add_string_value (
        builder,
        "index_build_with_archive_retained"
    );
    add_u64 (
        builder,
        "cache_root_allocated_peak",
        cache_peak
    );
    json_builder_end_object (builder);

    json_builder_begin_object (builder);
    json_builder_set_member_name (
        builder,
        "phase"
    );
    json_builder_add_string_value (
        builder,
        "same_sha_repair_quarantine_plus_replacement"
    );
    add_u64 (
        builder,
        "data_root_allocated_peak",
        repair_data_peak
    );
    json_builder_end_object (builder);

    json_builder_end_array (builder);
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
        TRUE
    );

    gboolean wrote =
        json_generator_to_file (
            generator,
            output_path,
            &error
        );

    if (!wrote) {
        g_printerr (
            "C0-M1 JSON write failed: %s\n",
            error->message
        );
        g_clear_error (&error);
    }

    g_object_unref (generator);
    json_node_free (root);
    g_object_unref (builder);
    atm_source_catalog_free (
        catalog
    );
    g_free (quarantine_path);
    g_free (index_path);
    g_free (snapshot_path);
    g_free (version);
    g_free (state_root);
    g_free (cache_root);
    g_free (data_root);

    return wrote ? 0 : 9;
}

int
main (
    int argc,
    char **argv
)
{
    if (argc != 9) {
        g_printerr (
            "Usage: capacity-real-runner "
            "ATM_COMMIT REPO_ID ACRONYM DISPLAY_NAME "
            "REPO_SHA ARCHIVE WORK_ROOT OUTPUT_JSON\n"
        );
        return 64;
    }

    return run_measurement (
        argv[1],
        argv[2],
        argv[3],
        argv[4],
        argv[5],
        argv[6],
        argv[7],
        argv[8]
    );
}
