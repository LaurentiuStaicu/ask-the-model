#include "repository_storage.h"
#include "retrieval_index.h"
#include "control_state.h"
#include "fault_injection_support.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>

#define CHECKPOINT_FD 3
#define CONTROL_FD 4

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";
static const char *TEST_SEAL =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static gboolean
path_stat (
    const char *path,
    GStatBuf *out_stat
)
{
    return g_lstat (path, out_stat) == 0;
}

static gboolean
path_is_real_directory (const char *path)
{
    GStatBuf stat_buffer;

    return path_stat (path, &stat_buffer) &&
        S_ISDIR (stat_buffer.st_mode) &&
        !S_ISLNK (stat_buffer.st_mode);
}

static gboolean
path_is_real_regular_file (const char *path)
{
    GStatBuf stat_buffer;

    return path_stat (path, &stat_buffer) &&
        S_ISREG (stat_buffer.st_mode) &&
        !S_ISLNK (stat_buffer.st_mode);
}

static gboolean
path_exists_any (const char *path)
{
    GStatBuf stat_buffer;

    return path_stat (path, &stat_buffer);
}

static char *
data_root_for (const char *root)
{
    return g_build_filename (root, "data", NULL);
}

static char *
cache_root_for (const char *root)
{
    return g_build_filename (root, "cache", NULL);
}

static char *
state_root_for (const char *root)
{
    return g_build_filename (root, "state", NULL);
}

static char *
control_path_for (const char *root)
{
    char *state_root = state_root_for (root);
    char *path = g_build_filename (
        state_root,
        "control-state.sqlite3",
        NULL
    );

    g_free (state_root);
    return path;
}

static gboolean
initialize_empty_control_state (
    const char *root,
    GError **error
)
{
    char *state_root = state_root_for (root);
    char *control_path = control_path_for (root);
    AtmControlStateStore *store = NULL;
    gboolean result = FALSE;

    if (g_mkdir_with_parents (state_root, 0700) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create test state root."
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

    result = TRUE;

out:
    if (store != NULL) {
        atm_control_state_close (store);
    }

    g_free (control_path);
    g_free (state_root);
    return result;
}

static gboolean
run_snapshot_promotion (
    const char *root,
    const char *checkpoint,
    GError **error
)
{
    char *data_root = NULL;
    char *control_path = NULL;
    char *staging = NULL;
    char *fixture = NULL;
    char *promoted = NULL;
    gboolean result = FALSE;

    if (!initialize_empty_control_state (
            root,
            error
        )) {
        goto out;
    }

    data_root = data_root_for (root);
    control_path = control_path_for (root);
    staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        TEST_SHA
    );
    fixture = g_build_filename (
        staging,
        "STATUS.md",
        NULL
    );

    if (g_mkdir_with_parents (staging, 0700) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create snapshot staging fixture."
        );
        goto out;
    }

    if (!g_file_set_contents (
            fixture,
            "# recovery fixture\n",
            -1,
            error
        )) {
        goto out;
    }

    atm_test_fault_configure (
        checkpoint,
        CHECKPOINT_FD,
        CONTROL_FD
    );

    if (!atm_repository_promote_snapshot (
            data_root,
            "ewd",
            TEST_SHA,
            staging,
            &promoted,
            error
        )) {
        goto out;
    }

    if (!atm_control_state_set_current_values (
            control_path,
            "ewd",
            TEST_SHA,
            "0.0-test",
            TEST_SEAL,
            error
        )) {
        goto out;
    }

    result = TRUE;

out:
    g_free (promoted);
    g_free (fixture);
    g_free (staging);
    g_free (control_path);
    g_free (data_root);
    return result;
}

static AtmRetrievalIndexMetadata
test_index_metadata (void)
{
    AtmRetrievalIndexMetadata metadata = {
        .repository_id = "ewd",
        .repository_version = "0.0-test",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .manifest_schema_version = 1,
        .manifest_sha256 =
            "0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef",
        .created_at_utc = "2026-09-24T00:00:00Z"
    };

    return metadata;
}

static gboolean
run_index_build (
    const char *root,
    const char *checkpoint,
    GError **error
)
{
    char *cache_root = cache_root_for (root);
    AtmRetrievalIndexMetadata metadata =
        test_index_metadata ();
    char *index_path = NULL;
    gboolean result;

    if (g_mkdir_with_parents (
            cache_root,
            0700
        ) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create index cache root."
        );
        g_free (cache_root);
        return FALSE;
    }

    atm_test_fault_configure (
        checkpoint,
        CHECKPOINT_FD,
        CONTROL_FD
    );

    result = atm_retrieval_index_create_empty (
        cache_root,
        &metadata,
        &index_path,
        error
    );

    g_free (index_path);
    g_free (cache_root);
    return result;
}

static gboolean
verify_snapshot (
    const char *root,
    GError **error
)
{
    char *data_root = data_root_for (root);
    char *control_path = control_path_for (root);
    char *final_path = atm_repository_snapshot_path (
        data_root,
        "ewd",
        TEST_SHA
    );
    char *staging_path =
        atm_repository_extraction_staging_path (
            data_root,
            "ewd",
            TEST_SHA
        );
    gint64 generation_id = 0;
    gboolean present = FALSE;
    char *snapshot_sha = NULL;
    char *repository_version = NULL;
    char *seal = NULL;
    const char *classification = NULL;
    const char *reason_code = NULL;
    gboolean qualified = FALSE;
    gboolean final_exists =
        path_exists_any (final_path);
    gboolean staging_exists =
        path_exists_any (staging_path);
    gboolean final_is_directory =
        path_is_real_directory (final_path);

    if (!atm_control_state_active_generation_id (
            control_path,
            &generation_id,
            error
        ) ||
        !atm_control_state_load_repository_values_at_generation (
            control_path,
            generation_id,
            "ewd",
            &present,
            &snapshot_sha,
            &repository_version,
            &seal,
            error
        )) {
        goto fail;
    }

    if (present) {
        if (final_is_directory &&
            g_strcmp0 (
                snapshot_sha,
                TEST_SHA
            ) == 0) {
            classification = "NEW_AUTHORITY_VALID";
            reason_code =
                "active_generation_matches_snapshot";
            qualified = TRUE;
        } else {
            classification = "INVALID_AUTHORITY";
            reason_code =
                "active_generation_snapshot_missing_or_mismatched";
            qualified = FALSE;
        }
    } else if (final_exists) {
        if (!final_is_directory) {
            classification = "INVALID_AUTHORITY";
            reason_code =
                "unexpected_final_snapshot_object";
            qualified = FALSE;
        } else {
            classification = "RECOVERY_REQUIRED";
            reason_code =
                "unreferenced_promoted_snapshot";
            qualified = TRUE;
        }
    } else {
        classification = "NO_AUTHORITY_VALID";
        reason_code = staging_exists
            ? "promotion_not_committed"
            : "no_repository_authority";
        qualified = TRUE;
    }

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"operation\":\"snapshot-promotion\","
        "\"classification\":\"%s\","
        "\"active_generation_id\":%" G_GINT64_FORMAT ","
        "\"active_repository_sha\":%s,"
        "\"expected_new_sha\":\"%s\","
        "\"snapshot_final_exists\":%s,"
        "\"snapshot_staging_exists\":%s,"
        "\"qualified\":%s,"
        "\"reason_code\":\"%s\""
        "}\n",
        classification,
        generation_id,
        snapshot_sha != NULL
            ? g_strdup_printf ("\"%s\"", snapshot_sha)
            : "null",
        TEST_SHA,
        final_exists ? "true" : "false",
        staging_exists ? "true" : "false",
        qualified ? "true" : "false",
        reason_code
    );

    g_free (seal);
    g_free (repository_version);
    g_free (snapshot_sha);
    g_free (staging_path);
    g_free (final_path);
    g_free (control_path);
    g_free (data_root);
    return TRUE;

fail:
    g_free (seal);
    g_free (repository_version);
    g_free (snapshot_sha);
    g_free (staging_path);
    g_free (final_path);
    g_free (control_path);
    g_free (data_root);
    return FALSE;
}

static gboolean
verify_index (
    const char *root,
    GError **error
)
{
    char *cache_root = cache_root_for (root);
    char *final_path = atm_retrieval_index_path (
        cache_root,
        "ewd",
        TEST_SHA
    );
    char *staging_path =
        atm_retrieval_index_staging_path (
            cache_root,
            "ewd",
            TEST_SHA
        );
    gboolean final_exists =
        path_exists_any (final_path);
    gboolean staging_exists =
        path_exists_any (staging_path);
    gboolean final_valid = FALSE;
    const char *classification = NULL;
    const char *reason_code = NULL;

    if (final_exists &&
        path_is_real_regular_file (final_path)) {
        GError *validation_error = NULL;

        final_valid =
            atm_retrieval_index_validate_identity (
                final_path,
                "ewd",
                TEST_SHA,
                &validation_error
            );

        g_clear_error (&validation_error);
    }

    if (final_valid) {
        classification =
            "DERIVED_CACHE_RECOVERABLE";
        reason_code = "valid_final_index";
    } else if (final_exists) {
        classification = "RECOVERY_REQUIRED";
        reason_code = "invalid_final_index";
    } else if (staging_exists) {
        classification = "RECOVERY_REQUIRED";
        reason_code = "abandoned_index_staging";
    } else {
        classification = "NO_AUTHORITY_VALID";
        reason_code = "no_index_cache";
    }

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"operation\":\"retrieval-index\","
        "\"classification\":\"%s\","
        "\"active_generation_id\":0,"
        "\"active_repository_sha\":null,"
        "\"expected_new_sha\":\"%s\","
        "\"snapshot_final_exists\":%s,"
        "\"snapshot_staging_exists\":%s,"
        "\"qualified\":%s,"
        "\"reason_code\":\"%s\""
        "}\n",
        classification,
        TEST_SHA,
        final_exists ? "true" : "false",
        staging_exists ? "true" : "false",
        final_valid ||
            (!final_exists && !staging_exists)
            ? "true"
            : "false",
        reason_code
    );

    g_free (staging_path);
    g_free (final_path);
    g_free (cache_root);
    return TRUE;
}

static int
report_error (GError *error)
{
    g_printerr (
        "recovery-fault-helper: %s\n",
        error != NULL
            ? error->message
            : "unknown error"
    );
    g_clear_error (&error);
    return 2;
}

int
main (int argc, char **argv)
{
    GError *error = NULL;
    gboolean ok = FALSE;

    if (argc == 4 &&
        g_strcmp0 (argv[1], "--snapshot") == 0) {
        ok = run_snapshot_promotion (
            argv[2],
            argv[3],
            &error
        );
    } else if (argc == 4 &&
               g_strcmp0 (argv[1], "--index") == 0) {
        ok = run_index_build (
            argv[2],
            argv[3],
            &error
        );
    } else if (argc == 3 &&
               g_strcmp0 (
                   argv[1],
                   "--verify-snapshot"
               ) == 0) {
        ok = verify_snapshot (
            argv[2],
            &error
        );
    } else if (argc == 3 &&
               g_strcmp0 (
                   argv[1],
                   "--verify-index"
               ) == 0) {
        ok = verify_index (
            argv[2],
            &error
        );
    } else {
        g_printerr (
            "Usage: recovery-fault-helper "
            "--snapshot ROOT CHECKPOINT | "
            "--index ROOT CHECKPOINT | "
            "--verify-snapshot ROOT | "
            "--verify-index ROOT\n"
        );
        return 64;
    }

    if (!ok) {
        return report_error (error);
    }

    g_clear_error (&error);
    return 0;
}
