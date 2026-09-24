#include "archive_extract.h"
#include "control_state.h"
#include "repository_ingest.h"
#include "repository_sources.h"
#include "repository_storage.h"
#include "retrieval_index.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

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

static gboolean
path_exists_any (
    const char *path
)
{
    GStatBuf st;
    return g_lstat (path, &st) == 0;
}

static gboolean
path_is_real_directory (
    const char *path
)
{
    GStatBuf st;

    return g_lstat (path, &st) == 0 &&
        S_ISDIR (st.st_mode) &&
        !S_ISLNK (st.st_mode);
}

static gboolean
path_is_real_regular_file (
    const char *path
)
{
    GStatBuf st;

    return g_lstat (path, &st) == 0 &&
        S_ISREG (st.st_mode) &&
        !S_ISLNK (st.st_mode);
}

static gboolean
make_directory (
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
        "Could not create '%s': %s",
        path,
        g_strerror (errno)
    );
    return FALSE;
}

static gboolean
seed_old_authority (
    const char *data_root,
    const char *state_root,
    const char *repository_id,
    const char *old_sha,
    GError **error
)
{
    char *control_path = NULL;
    char *old_snapshot = NULL;
    char *marker = NULL;
    gboolean ok = FALSE;

    if (!make_directory (
            data_root,
            error
        ) ||
        !make_directory (
            state_root,
            error
        )) {
        goto out;
    }

    old_snapshot =
        atm_repository_snapshot_path (
            data_root,
            repository_id,
            old_sha
        );

    if (old_snapshot == NULL ||
        !make_directory (
            old_snapshot,
            error
        )) {
        goto out;
    }

    marker = g_build_filename (
        old_snapshot,
        "ATM_ENOSPC_OLD_AUTHORITY.txt",
        NULL
    );

    if (!g_file_set_contents (
            marker,
            "old authority remains protected\n",
            -1,
            error
        )) {
        goto out;
    }

    control_path =
        control_path_for (
            state_root
        );

    if (!atm_control_state_set_current_values (
            control_path,
            repository_id,
            old_sha,
            TEST_VERSION,
            TEST_SEAL,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    g_free (marker);
    g_free (old_snapshot);
    g_free (control_path);
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
        )) {
        goto out;
    }

    if (*out_generation_id <= 0 ||
        !atm_control_state_load_repository_values_at_generation (
            control_path,
            *out_generation_id,
            repository_id,
            &present,
            out_sha,
            &version,
            &seal,
            error
        )) {
        goto out;
    }

    if (!present || *out_sha == NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Expected seeded repository authority is absent."
        );
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
error_mentions_enospc (
    const GError *error
)
{
    if (error == NULL ||
        error->message == NULL) {
        return FALSE;
    }

    const char *needle =
        g_strerror (ENOSPC);

    return needle != NULL &&
        needle[0] != '\0' &&
        strstr (
            error->message,
            needle
        ) != NULL;
}

static gboolean
error_mentions_sqlite_full (
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
            "disk is full"
        ) != NULL;
}

static void
emit_result (
    const char *scenario,
    const char *classification,
    gboolean qualified,
    gint64 generation_before,
    gint64 generation_after,
    const char *old_sha,
    const char *active_sha_after,
    const GError *operation_error,
    gboolean old_snapshot_exists,
    gboolean new_snapshot_exists,
    gboolean extraction_staging_exists,
    gboolean final_index_exists,
    gboolean index_staging_exists
)
{
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
        "old_snapshot_exists"
    );
    json_builder_add_boolean_value (
        builder,
        old_snapshot_exists
    );

    json_builder_set_member_name (
        builder,
        "new_snapshot_exists"
    );
    json_builder_add_boolean_value (
        builder,
        new_snapshot_exists
    );

    json_builder_set_member_name (
        builder,
        "extraction_staging_exists"
    );
    json_builder_add_boolean_value (
        builder,
        extraction_staging_exists
    );

    json_builder_set_member_name (
        builder,
        "final_index_exists"
    );
    json_builder_add_boolean_value (
        builder,
        final_index_exists
    );

    json_builder_set_member_name (
        builder,
        "index_staging_exists"
    );
    json_builder_add_boolean_value (
        builder,
        index_staging_exists
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
}

static gboolean
collect_paths (
    const char *data_root,
    const char *cache_root,
    const char *repository_id,
    const char *old_sha,
    const char *new_sha,
    gboolean *out_old_snapshot_exists,
    gboolean *out_new_snapshot_exists,
    gboolean *out_extraction_staging_exists,
    gboolean *out_final_index_exists,
    gboolean *out_index_staging_exists
)
{
    char *old_snapshot =
        atm_repository_snapshot_path (
            data_root,
            repository_id,
            old_sha
        );
    char *new_snapshot =
        atm_repository_snapshot_path (
            data_root,
            repository_id,
            new_sha
        );
    char *extraction_staging =
        atm_repository_extraction_staging_path (
            data_root,
            repository_id,
            new_sha
        );
    char *final_index =
        cache_root != NULL
            ? atm_retrieval_index_path (
                cache_root,
                repository_id,
                new_sha
            )
            : NULL;
    char *index_staging =
        cache_root != NULL
            ? atm_retrieval_index_staging_path (
                cache_root,
                repository_id,
                new_sha
            )
            : NULL;

    if (old_snapshot == NULL ||
        new_snapshot == NULL ||
        extraction_staging == NULL ||
        (cache_root != NULL &&
         (final_index == NULL ||
          index_staging == NULL))) {
        g_free (index_staging);
        g_free (final_index);
        g_free (extraction_staging);
        g_free (new_snapshot);
        g_free (old_snapshot);
        return FALSE;
    }

    *out_old_snapshot_exists =
        path_is_real_directory (
            old_snapshot
        );
    *out_new_snapshot_exists =
        path_is_real_directory (
            new_snapshot
        );
    *out_extraction_staging_exists =
        path_exists_any (
            extraction_staging
        );
    *out_final_index_exists =
        final_index != NULL &&
        path_is_real_regular_file (
            final_index
        );
    *out_index_staging_exists =
        index_staging != NULL &&
        path_exists_any (
            index_staging
        );

    g_free (index_staging);
    g_free (final_index);
    g_free (extraction_staging);
    g_free (new_snapshot);
    g_free (old_snapshot);
    return TRUE;
}

static int
run_data_enospc (
    const char *data_root,
    const char *state_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *new_sha,
    const char *old_sha
)
{
    GError *error = NULL;
    GError *operation_error = NULL;
    gint64 generation_before = 0;
    gint64 generation_after = 0;
    char *active_before = NULL;
    char *active_after = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    gboolean old_snapshot_exists = FALSE;
    gboolean new_snapshot_exists = FALSE;
    gboolean extraction_staging_exists = FALSE;
    gboolean final_index_exists = FALSE;
    gboolean index_staging_exists = FALSE;
    gboolean ingest_ok;
    gboolean qualified;

    if (!seed_old_authority (
            data_root,
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
        )) {
        g_printerr (
            "ENOSPC seed failed: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_free (active_before);
        return 2;
    }

    ingest_ok =
        atm_repository_ingest_archive (
            data_root,
            archive_path,
            repository_id,
            repository_acronym,
            repository_display_name,
            new_sha,
            &version,
            &snapshot_path,
            &entries,
            &total_bytes,
            &operation_error
        );

    if (!load_active (
            state_root,
            repository_id,
            &generation_after,
            &active_after,
            &error
        ) ||
        !collect_paths (
            data_root,
            NULL,
            repository_id,
            old_sha,
            new_sha,
            &old_snapshot_exists,
            &new_snapshot_exists,
            &extraction_staging_exists,
            &final_index_exists,
            &index_staging_exists
        )) {
        g_printerr (
            "ENOSPC verification setup failed: %s\n",
            error != NULL
                ? error->message
                : "path calculation failure"
        );
        g_clear_error (&error);
        g_clear_error (&operation_error);
        g_free (snapshot_path);
        g_free (version);
        g_free (active_after);
        g_free (active_before);
        return 3;
    }

    qualified =
        !ingest_ok &&
        error_mentions_enospc (
            operation_error
        ) &&
        generation_before ==
            generation_after &&
        g_strcmp0 (
            active_before,
            old_sha
        ) == 0 &&
        g_strcmp0 (
            active_after,
            old_sha
        ) == 0 &&
        old_snapshot_exists &&
        !new_snapshot_exists &&
        !extraction_staging_exists;

    emit_result (
        "data-enospc",
        qualified
            ? "FAIL_CLOSED_OLD_AUTHORITY"
            : "UNQUALIFIED",
        qualified,
        generation_before,
        generation_after,
        old_sha,
        active_after,
        operation_error,
        old_snapshot_exists,
        new_snapshot_exists,
        extraction_staging_exists,
        final_index_exists,
        index_staging_exists
    );

    g_clear_error (&operation_error);
    g_free (snapshot_path);
    g_free (version);
    g_free (active_after);
    g_free (active_before);
    return qualified ? 0 : 4;
}

static int
run_index_enospc (
    const char *data_root,
    const char *cache_root,
    const char *state_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *new_sha,
    const char *old_sha
)
{
    GError *error = NULL;
    GError *operation_error = NULL;
    gint64 generation_before = 0;
    gint64 generation_after = 0;
    char *active_before = NULL;
    char *active_after = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    char *index_path = NULL;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    AtmSourceCatalog *catalog = NULL;
    gboolean old_snapshot_exists = FALSE;
    gboolean new_snapshot_exists = FALSE;
    gboolean extraction_staging_exists = FALSE;
    gboolean final_index_exists = FALSE;
    gboolean index_staging_exists = FALSE;
    gboolean index_ok = FALSE;
    gboolean qualified = FALSE;

    if (!seed_old_authority (
            data_root,
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
        )) {
        g_printerr (
            "ENOSPC seed failed: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        g_free (active_before);
        return 2;
    }

    if (!atm_repository_ingest_archive (
            data_root,
            archive_path,
            repository_id,
            repository_acronym,
            repository_display_name,
            new_sha,
            &version,
            &snapshot_path,
            &entries,
            &total_bytes,
            &error
        ) ||
        !atm_repository_source_catalog_build (
            snapshot_path,
            repository_id,
            &catalog,
            &error
        )) {
        g_printerr (
            "ENOSPC index prerequisite failed: %s\n",
            error != NULL
                ? error->message
                : "unknown error"
        );
        g_clear_error (&error);
        atm_source_catalog_free (catalog);
        g_free (snapshot_path);
        g_free (version);
        g_free (active_before);
        return 3;
    }

    AtmRetrievalIndexMetadata metadata = {
        .repository_id = repository_id,
        .repository_version = version,
        .snapshot_sha = new_sha,
        .manifest_schema_version = 1,
        .manifest_sha256 =
            catalog->manifest_sha256,
        .created_at_utc =
            "1970-01-01T00:00:00Z"
    };

    index_ok =
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_path,
            &metadata,
            catalog,
            &index_path,
            &operation_error
        );

    if (!load_active (
            state_root,
            repository_id,
            &generation_after,
            &active_after,
            &error
        ) ||
        !collect_paths (
            data_root,
            cache_root,
            repository_id,
            old_sha,
            new_sha,
            &old_snapshot_exists,
            &new_snapshot_exists,
            &extraction_staging_exists,
            &final_index_exists,
            &index_staging_exists
        )) {
        g_printerr (
            "ENOSPC index verification failed: %s\n",
            error != NULL
                ? error->message
                : "path calculation failure"
        );
        g_clear_error (&error);
        g_clear_error (&operation_error);
        atm_source_catalog_free (catalog);
        g_free (index_path);
        g_free (snapshot_path);
        g_free (version);
        g_free (active_after);
        g_free (active_before);
        return 4;
    }

    qualified =
        !index_ok &&
        operation_error != NULL &&
        operation_error->domain ==
            ATM_RETRIEVAL_INDEX_ERROR &&
        operation_error->code ==
            ATM_RETRIEVAL_INDEX_ERROR_SQLITE &&
        error_mentions_sqlite_full (
            operation_error
        ) &&
        generation_before ==
            generation_after &&
        g_strcmp0 (
            active_before,
            old_sha
        ) == 0 &&
        g_strcmp0 (
            active_after,
            old_sha
        ) == 0 &&
        old_snapshot_exists &&
        new_snapshot_exists &&
        !extraction_staging_exists &&
        !final_index_exists &&
        !index_staging_exists;

    emit_result (
        "index-enospc",
        qualified
            ? "RECOVERABLE_UNREFERENCED_SNAPSHOT"
            : "UNQUALIFIED",
        qualified,
        generation_before,
        generation_after,
        old_sha,
        active_after,
        operation_error,
        old_snapshot_exists,
        new_snapshot_exists,
        extraction_staging_exists,
        final_index_exists,
        index_staging_exists
    );

    g_clear_error (&operation_error);
    atm_source_catalog_free (catalog);
    g_free (index_path);
    g_free (snapshot_path);
    g_free (version);
    g_free (active_after);
    g_free (active_before);
    return qualified ? 0 : 5;
}

static int
run_verify (
    const char *scenario,
    const char *data_root,
    const char *cache_root,
    const char *state_root,
    const char *repository_id,
    const char *new_sha,
    const char *old_sha,
    gboolean expect_new_snapshot
)
{
    GError *error = NULL;
    gint64 generation = 0;
    char *active_sha = NULL;
    gboolean old_snapshot_exists = FALSE;
    gboolean new_snapshot_exists = FALSE;
    gboolean extraction_staging_exists = FALSE;
    gboolean final_index_exists = FALSE;
    gboolean index_staging_exists = FALSE;
    gboolean qualified;

    if (!load_active (
            state_root,
            repository_id,
            &generation,
            &active_sha,
            &error
        ) ||
        !collect_paths (
            data_root,
            cache_root,
            repository_id,
            old_sha,
            new_sha,
            &old_snapshot_exists,
            &new_snapshot_exists,
            &extraction_staging_exists,
            &final_index_exists,
            &index_staging_exists
        )) {
        g_printerr (
            "ENOSPC restart verification failed: %s\n",
            error != NULL
                ? error->message
                : "path calculation failure"
        );
        g_clear_error (&error);
        g_free (active_sha);
        return 2;
    }

    qualified =
        generation > 0 &&
        g_strcmp0 (
            active_sha,
            old_sha
        ) == 0 &&
        old_snapshot_exists &&
        new_snapshot_exists ==
            expect_new_snapshot &&
        !extraction_staging_exists &&
        !final_index_exists &&
        !index_staging_exists;

    emit_result (
        scenario,
        qualified
            ? "RESTART_OLD_AUTHORITY_VALID"
            : "UNQUALIFIED",
        qualified,
        generation,
        generation,
        old_sha,
        active_sha,
        NULL,
        old_snapshot_exists,
        new_snapshot_exists,
        extraction_staging_exists,
        final_index_exists,
        index_staging_exists
    );

    g_free (active_sha);
    return qualified ? 0 : 3;
}

int
main (
    int argc,
    char **argv
)
{
    if (argc == 10 &&
        g_strcmp0 (
            argv[1],
            "--data-enospc"
        ) == 0) {
        return run_data_enospc (
            argv[2],
            argv[3],
            argv[4],
            argv[5],
            argv[6],
            argv[7],
            argv[8],
            argv[9]
        );
    }

    if (argc == 11 &&
        g_strcmp0 (
            argv[1],
            "--index-enospc"
        ) == 0) {
        return run_index_enospc (
            argv[2],
            argv[3],
            argv[4],
            argv[5],
            argv[6],
            argv[7],
            argv[8],
            argv[9],
            argv[10]
        );
    }

    if (argc == 8 &&
        g_strcmp0 (
            argv[1],
            "--verify-data"
        ) == 0) {
        return run_verify (
            "data-enospc-restart",
            argv[2],
            NULL,
            argv[3],
            argv[4],
            argv[5],
            argv[6],
            FALSE
        );
    }

    if (argc == 9 &&
        g_strcmp0 (
            argv[1],
            "--verify-index"
        ) == 0) {
        return run_verify (
            "index-enospc-restart",
            argv[2],
            argv[3],
            argv[4],
            argv[5],
            argv[6],
            argv[7],
            TRUE
        );
    }

    g_printerr (
        "Usage:\n"
        "  capacity-enospc-runner --data-enospc "
        "DATA STATE ARCHIVE ID ACRONYM DISPLAY NEW_SHA OLD_SHA\n"
        "  capacity-enospc-runner --index-enospc "
        "DATA CACHE STATE ARCHIVE ID ACRONYM DISPLAY NEW_SHA OLD_SHA\n"
        "  capacity-enospc-runner --verify-data "
        "DATA STATE ID NEW_SHA OLD_SHA UNUSED\n"
        "  capacity-enospc-runner --verify-index "
        "DATA CACHE STATE ID NEW_SHA OLD_SHA UNUSED\n"
    );
    return 64;
}
