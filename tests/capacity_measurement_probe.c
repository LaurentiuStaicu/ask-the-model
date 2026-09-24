#include "capacity_measurement.h"

#include <glib.h>
#include <json-glib/json-glib.h>

#include <sys/utsname.h>

static void
add_u64_member (
    JsonBuilder *builder,
    const char *name,
    guint64 value
)
{
    json_builder_set_member_name (builder, name);
    json_builder_add_int_value (
        builder,
        (gint64) MIN (value, (guint64) G_MAXINT64)
    );
}

static void
add_device_member (
    JsonBuilder *builder,
    guint64 device_id
)
{
    char *value = g_strdup_printf (
        "%" G_GUINT64_FORMAT,
        device_id
    );

    json_builder_set_member_name (
        builder,
        "st_dev"
    );
    json_builder_add_string_value (
        builder,
        value
    );
    g_free (value);
}

static gboolean
add_root_measurement (
    JsonBuilder *builder,
    const char *name,
    const char *path,
    GError **error
)
{
    AtmFilesystemCapacityMeasurement measurement;

    if (!atm_capacity_measure_filesystem (
            path,
            &measurement,
            error
        )) {
        return FALSE;
    }

    json_builder_set_member_name (
        builder,
        name
    );
    json_builder_begin_object (builder);

    add_device_member (
        builder,
        measurement.device_id
    );
    add_u64_member (
        builder,
        "available_bytes",
        measurement.available_bytes
    );
    add_u64_member (
        builder,
        "available_inodes",
        measurement.available_inodes
    );
    add_u64_member (
        builder,
        "fragment_size",
        measurement.fragment_size
    );

    json_builder_end_object (builder);
    return TRUE;
}

static gboolean
add_optional_file_measurement (
    JsonBuilder *builder,
    const char *name,
    const char *path,
    GError **error
)
{
    json_builder_set_member_name (
        builder,
        name
    );

    if (path == NULL || path[0] == '\0') {
        json_builder_add_null_value (builder);
        return TRUE;
    }

    AtmFileCapacityMeasurement measurement;

    if (!atm_capacity_measure_regular_file (
            path,
            &measurement,
            error
        )) {
        return FALSE;
    }

    json_builder_begin_object (builder);
    add_u64_member (
        builder,
        "logical_bytes",
        measurement.logical_bytes
    );
    add_u64_member (
        builder,
        "allocated_bytes",
        measurement.allocated_bytes
    );
    json_builder_end_object (builder);
    return TRUE;
}

static gboolean
add_optional_snapshot_measurement (
    JsonBuilder *builder,
    const char *path,
    GError **error
)
{
    json_builder_set_member_name (
        builder,
        "snapshot"
    );

    if (path == NULL || path[0] == '\0') {
        json_builder_add_null_value (builder);
        return TRUE;
    }

    AtmTreeCapacityMeasurement measurement;

    if (!atm_capacity_measure_tree (
            path,
            &measurement,
            error
        )) {
        return FALSE;
    }

    json_builder_begin_object (builder);
    add_u64_member (
        builder,
        "logical_regular_bytes",
        measurement.logical_regular_bytes
    );
    add_u64_member (
        builder,
        "allocated_tree_bytes",
        measurement.allocated_tree_bytes
    );
    add_u64_member (
        builder,
        "entries",
        measurement.entries
    );
    add_u64_member (
        builder,
        "regular_files",
        measurement.regular_files
    );
    add_u64_member (
        builder,
        "directories",
        measurement.directories
    );
    json_builder_end_object (builder);
    return TRUE;
}

int
main (
    int argc,
    char **argv
)
{
    char *atm_source_commit = NULL;
    char *repository_id = NULL;
    char *repository_sha = NULL;
    char *scenario = NULL;
    char *checkpoint = NULL;
    char *data_root = NULL;
    char *cache_root = NULL;
    char *state_root = NULL;
    char *archive_path = NULL;
    char *snapshot_path = NULL;
    char *index_path = NULL;

    GOptionEntry entries[] = {
        { "atm-source-commit", 0, 0, G_OPTION_ARG_STRING,
          &atm_source_commit, "Exact AtM source commit", "SHA" },
        { "repository-id", 0, 0, G_OPTION_ARG_STRING,
          &repository_id, "Repository identifier", "ID" },
        { "repository-sha", 0, 0, G_OPTION_ARG_STRING,
          &repository_sha, "Exact repository SHA", "SHA" },
        { "scenario", 0, 0, G_OPTION_ARG_STRING,
          &scenario, "Measurement scenario", "NAME" },
        { "checkpoint", 0, 0, G_OPTION_ARG_STRING,
          &checkpoint, "Measurement checkpoint", "NAME" },
        { "data-root", 0, 0, G_OPTION_ARG_FILENAME,
          &data_root, "AtM data root", "PATH" },
        { "cache-root", 0, 0, G_OPTION_ARG_FILENAME,
          &cache_root, "AtM cache root", "PATH" },
        { "state-root", 0, 0, G_OPTION_ARG_FILENAME,
          &state_root, "AtM state root", "PATH" },
        { "archive", 0, 0, G_OPTION_ARG_FILENAME,
          &archive_path, "Archive artifact path", "PATH" },
        { "snapshot", 0, 0, G_OPTION_ARG_FILENAME,
          &snapshot_path, "Snapshot tree path", "PATH" },
        { "index", 0, 0, G_OPTION_ARG_FILENAME,
          &index_path, "Retrieval-index path", "PATH" },
        { NULL }
    };

    GError *error = NULL;
    GOptionContext *context =
        g_option_context_new (
            "- emit one deterministic C0-M1 capacity measurement checkpoint"
        );
    g_option_context_add_main_entries (
        context,
        entries,
        NULL
    );

    if (!g_option_context_parse (
            context,
            &argc,
            &argv,
            &error
        )) {
        g_printerr (
            "capacity measurement arguments: %s\n",
            error->message
        );
        g_clear_error (&error);
        g_option_context_free (context);
        return 2;
    }

    if (atm_source_commit == NULL ||
        repository_id == NULL ||
        repository_sha == NULL ||
        scenario == NULL ||
        checkpoint == NULL ||
        data_root == NULL ||
        cache_root == NULL ||
        state_root == NULL) {
        g_printerr (
            "capacity measurement requires source commit, repository identity, scenario, checkpoint, and all three roots\n"
        );
        g_option_context_free (context);
        return 2;
    }

    gint64 started_us = g_get_monotonic_time ();
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
        scenario
    );

    json_builder_set_member_name (
        builder,
        "checkpoint"
    );
    json_builder_add_string_value (
        builder,
        checkpoint
    );

    json_builder_set_member_name (
        builder,
        "roots"
    );
    json_builder_begin_object (builder);

    if (!add_root_measurement (
            builder,
            "data",
            data_root,
            &error
        ) ||
        !add_root_measurement (
            builder,
            "cache",
            cache_root,
            &error
        ) ||
        !add_root_measurement (
            builder,
            "state",
            state_root,
            &error
        )) {
        g_printerr (
            "capacity measurement root failure: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_object_unref (builder);
        g_option_context_free (context);
        return 3;
    }

    json_builder_end_object (builder);

    if (!add_optional_file_measurement (
            builder,
            "archive",
            archive_path,
            &error
        ) ||
        !add_optional_snapshot_measurement (
            builder,
            snapshot_path,
            &error
        ) ||
        !add_optional_file_measurement (
            builder,
            "index",
            index_path,
            &error
        )) {
        g_printerr (
            "capacity measurement artifact failure: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_object_unref (builder);
        g_option_context_free (context);
        return 4;
    }

    struct utsname uts;
    json_builder_set_member_name (
        builder,
        "runtime"
    );
    json_builder_begin_object (builder);

    if (uname (&uts) == 0) {
        json_builder_set_member_name (
            builder,
            "sysname"
        );
        json_builder_add_string_value (
            builder,
            uts.sysname
        );
        json_builder_set_member_name (
            builder,
            "release"
        );
        json_builder_add_string_value (
            builder,
            uts.release
        );
        json_builder_set_member_name (
            builder,
            "machine"
        );
        json_builder_add_string_value (
            builder,
            uts.machine
        );
    }

    char *glib_version = g_strdup_printf (
        "%u.%u.%u",
        glib_major_version,
        glib_minor_version,
        glib_micro_version
    );
    json_builder_set_member_name (
        builder,
        "glib"
    );
    json_builder_add_string_value (
        builder,
        glib_version
    );
    g_free (glib_version);
    json_builder_end_object (builder);

    json_builder_set_member_name (
        builder,
        "elapsed_ms"
    );
    json_builder_begin_object (builder);
    add_u64_member (
        builder,
        "probe",
        (guint64) (
            (g_get_monotonic_time () - started_us) /
            1000
        )
    );
    json_builder_end_object (builder);

    json_builder_end_object (builder);

    JsonNode *root = json_builder_get_root (builder);
    JsonGenerator *generator = json_generator_new ();
    json_generator_set_root (
        generator,
        root
    );
    json_generator_set_pretty (
        generator,
        TRUE
    );

    char *json = json_generator_to_data (
        generator,
        NULL
    );
    g_print ("%s\n", json);

    g_free (json);
    g_object_unref (generator);
    json_node_free (root);
    g_object_unref (builder);
    g_option_context_free (context);
    return 0;
}
