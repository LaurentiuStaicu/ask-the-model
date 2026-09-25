#include "control_state.h"
#include "fault_injection_support.h"
#include "fault_injection_test_hook.h"
#include "repository_storage.h"
#include "snapshot_durability.h"
#include "snapshot_namespace_durability.h"
#include "snapshot_seal.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#define CHECKPOINT_FD 3
#define CONTROL_FD 4

static const char *REPOSITORY_ID = "ewd";
static const char *NEW_SHA =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

static char *
data_root_for (
    const char *root
)
{
    return g_build_filename (
        root,
        "data",
        NULL
    );
}

static char *
state_root_for (
    const char *root
)
{
    return g_build_filename (
        root,
        "state",
        NULL
    );
}

static char *
control_path_for (
    const char *root
)
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
path_exists_any (
    const char *path
)
{
    GStatBuf st;
    return g_lstat (path, &st) == 0;
}

static gboolean
initialize_control_state (
    const char *root,
    GError **error
)
{
    char *state_root = state_root_for (root);
    char *control_path = control_path_for (root);
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
            "Could not create A1-M10 state root: %s",
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

    ok = TRUE;

out:
    if (store != NULL) {
        atm_control_state_close (store);
    }
    g_free (control_path);
    g_free (state_root);
    return ok;
}

static gboolean
initialize_empty (
    const char *root,
    GError **error
)
{
    if (!initialize_control_state (
            root,
            error
        )) {
        return FALSE;
    }

    char *data_root = data_root_for (root);
    char *repositories = g_build_filename (
        data_root,
        "Repositories",
        NULL
    );
    gboolean ok = FALSE;

    if (g_mkdir_with_parents (
            data_root,
            0700
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create A1-M10 data root: %s",
            g_strerror (errno)
        );
        goto out;
    }

    if (path_exists_any (repositories)) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_EXIST,
            "A1-M10 fresh baseline unexpectedly contains Repositories."
        );
        goto out;
    }

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"classification\":\"EMPTY_BASELINE\","
        "\"repositories_exists\":false"
        "}\n"
    );
    ok = TRUE;

out:
    g_free (repositories);
    g_free (data_root);
    return ok;
}

static gboolean
write_fixture (
    const char *staging,
    GError **error
)
{
    char *nested = g_build_filename (
        staging,
        "nested",
        NULL
    );
    char *status = g_build_filename (
        staging,
        "STATUS.md",
        NULL
    );
    char *payload_path = g_build_filename (
        nested,
        "payload.txt",
        NULL
    );
    GString *payload = g_string_new (NULL);
    gboolean ok = FALSE;

    if (g_mkdir_with_parents (
            nested,
            0700
        ) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create A1-M10 fixture directories."
        );
        goto out;
    }

    if (!g_file_set_contents (
            status,
            "# A1-M10 fresh snapshot\n",
            -1,
            error
        )) {
        goto out;
    }

    for (guint i = 0;
         i < 32768;
         i++) {
        g_string_append_printf (
            payload,
            "atm-a1-m10-fresh-line-%05u\n",
            i
        );
    }

    if (!g_file_set_contents (
            payload_path,
            payload->str,
            (gssize) payload->len,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    g_string_free (payload, TRUE);
    g_free (payload_path);
    g_free (status);
    g_free (nested);
    return ok;
}

typedef enum {
    FRESH_PARENT_ONLY,
    FRESH_DESTINATION_CHAIN,
    FRESH_DESTINATION_AND_SOURCE
} FreshNamespaceStrategy;

static gboolean
parse_strategy (
    const char *name,
    FreshNamespaceStrategy *out_strategy
)
{
    if (g_strcmp0 (
            name,
            "S1_PARENT_ONLY"
        ) == 0) {
        *out_strategy = FRESH_PARENT_ONLY;
        return TRUE;
    }

    if (g_strcmp0 (
            name,
            "S1_DEST_CHAIN"
        ) == 0) {
        *out_strategy = FRESH_DESTINATION_CHAIN;
        return TRUE;
    }

    if (g_strcmp0 (
            name,
            "S1_DEST_SOURCE"
        ) == 0) {
        *out_strategy =
            FRESH_DESTINATION_AND_SOURCE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
checkpoint_valid (
    const char *checkpoint
)
{
    return
        g_strcmp0 (
            checkpoint,
            "fresh_post_barrier_pre_destination_hierarchy"
        ) == 0 ||
        g_strcmp0 (
            checkpoint,
            "fresh_post_destination_hierarchy_pre_rename"
        ) == 0 ||
        g_strcmp0 (
            checkpoint,
            "fresh_post_rename_pre_destination_parent_fsync"
        ) == 0 ||
        g_strcmp0 (
            checkpoint,
            "fresh_post_destination_parent_fsync_pre_source_parent_fsync"
        ) == 0 ||
        g_strcmp0 (
            checkpoint,
            "fresh_post_source_parent_fsync_pre_authority"
        ) == 0 ||
        g_strcmp0 (
            checkpoint,
            "fresh_after_authority"
        ) == 0;
}

static gboolean
promote_candidate (
    const char *root,
    const char *strategy_name,
    const char *checkpoint,
    GError **error
)
{
    FreshNamespaceStrategy strategy;

    if (!parse_strategy (
            strategy_name,
            &strategy
        )) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Unknown A1-M10 namespace strategy."
        );
        return FALSE;
    }

    if (!checkpoint_valid (checkpoint)) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Unknown A1-M10 replay checkpoint."
        );
        return FALSE;
    }

    char *data_root = data_root_for (root);
    char *control_path = control_path_for (root);
    char *staging =
        atm_repository_extraction_staging_path (
            data_root,
            REPOSITORY_ID,
            NEW_SHA
        );
    char *promoted = NULL;
    char *seal = NULL;
    guint64 file_count = 0;
    guint64 total_bytes = 0;
    AtmSnapshotNamespaceStats namespace_stats = {
        0
    };
    AtmSnapshotDurabilityStats tree_stats = {
        0
    };
    AtmSnapshotDurabilityStats destination_stats = {
        0
    };
    AtmSnapshotDurabilityStats source_stats = {
        0
    };
    gboolean ok = FALSE;

    if (staging == NULL ||
        g_mkdir_with_parents (
            staging,
            0700
        ) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create A1-M10 staging."
        );
        goto out;
    }

    if (!write_fixture (
            staging,
            error
        ) ||
        !atm_snapshot_seal_compute (
            staging,
            &seal,
            &file_count,
            &total_bytes,
            error
        )) {
        goto out;
    }

    atm_test_fault_configure (
        checkpoint,
        CHECKPOINT_FD,
        CONTROL_FD
    );

    if (!atm_snapshot_durability_sync_tree (
            staging,
            &tree_stats,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "fresh_post_barrier_pre_destination_hierarchy"
    );

    if (strategy != FRESH_PARENT_ONLY &&
        !atm_snapshot_namespace_prepare_final_parent (
            data_root,
            REPOSITORY_ID,
            &namespace_stats,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "fresh_post_destination_hierarchy_pre_rename"
    );

    if (!atm_repository_promote_snapshot (
            data_root,
            REPOSITORY_ID,
            NEW_SHA,
            staging,
            &promoted,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "fresh_post_rename_pre_destination_parent_fsync"
    );

    if (!atm_snapshot_durability_sync_parent (
            promoted,
            &destination_stats,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "fresh_post_destination_parent_fsync_pre_source_parent_fsync"
    );

    if (strategy ==
            FRESH_DESTINATION_AND_SOURCE &&
        !atm_snapshot_durability_sync_parent (
            staging,
            &source_stats,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "fresh_post_source_parent_fsync_pre_authority"
    );

    if (!atm_control_state_set_current_values (
            control_path,
            REPOSITORY_ID,
            NEW_SHA,
            "0.0-fresh",
            seal,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "fresh_after_authority"
    );

    g_print (
        "{"
        "\"strategy\":\"%s\","
        "\"sha\":\"%s\","
        "\"seal\":\"%s\","
        "\"file_count\":%" G_GUINT64_FORMAT ","
        "\"total_bytes\":%" G_GUINT64_FORMAT ","
        "\"tree_file_fsync_calls\":%" G_GUINT64_FORMAT ","
        "\"tree_directory_fsync_calls\":%" G_GUINT64_FORMAT ","
        "\"namespace_directories_created\":%" G_GUINT64_FORMAT ","
        "\"namespace_fsync_calls\":%" G_GUINT64_FORMAT ","
        "\"destination_parent_fsync_calls\":%" G_GUINT64_FORMAT ","
        "\"source_parent_fsync_calls\":%" G_GUINT64_FORMAT
        "}\n",
        strategy_name,
        NEW_SHA,
        seal,
        file_count,
        total_bytes,
        tree_stats.file_fsync_calls,
        tree_stats.directory_fsync_calls,
        namespace_stats.directories_created,
        namespace_stats.directory_fsync_calls,
        destination_stats.parent_fsync_calls,
        source_stats.parent_fsync_calls
    );

    ok = TRUE;

out:
    g_free (seal);
    g_free (promoted);
    g_free (staging);
    g_free (control_path);
    g_free (data_root);
    return ok;
}

static gboolean
verify_fresh (
    const char *root
)
{
    char *data_root = data_root_for (root);
    char *control_path = control_path_for (root);
    char *final_path =
        atm_repository_snapshot_path (
            data_root,
            REPOSITORY_ID,
            NEW_SHA
        );
    char *staging_path =
        atm_repository_extraction_staging_path (
            data_root,
            REPOSITORY_ID,
            NEW_SHA
        );
    char *repositories_path = g_build_filename (
        data_root,
        "Repositories",
        NULL
    );
    char *repository_path = g_build_filename (
        repositories_path,
        REPOSITORY_ID,
        NULL
    );
    char *snapshots_path = g_build_filename (
        repository_path,
        "snapshots",
        NULL
    );
    char *staging_root = g_build_filename (
        repositories_path,
        ".staging",
        NULL
    );
    char *staging_repository =
        g_build_filename (
            staging_root,
            REPOSITORY_ID,
            NULL
        );
    gint64 generation_id = 0;
    gboolean present = FALSE;
    char *active_sha = NULL;
    char *version = NULL;
    char *stored_seal = NULL;
    char *computed_seal = NULL;
    guint64 file_count = 0;
    guint64 total_bytes = 0;
    GError *error = NULL;
    const char *classification =
        "INVALID_AUTHORITY";
    const char *reason_code = "unknown";
    gboolean seal_match = FALSE;
    gboolean has_seal_match = FALSE;
    gboolean qualified = FALSE;
    gboolean final_exists =
        path_exists_any (final_path);
    gboolean staging_exists =
        path_exists_any (staging_path);

    if (!atm_control_state_active_generation_id (
            control_path,
            &generation_id,
            &error
        )) {
        g_clear_error (&error);
        reason_code = "control_state_unreadable";
        goto print;
    }

    if (generation_id == 0) {
        classification =
            "EMPTY_AUTHORITY_VALID";
        reason_code =
            "no_repository_authority";
        qualified = TRUE;
        goto print;
    }

    if (!atm_control_state_load_repository_values_at_generation (
            control_path,
            generation_id,
            REPOSITORY_ID,
            &present,
            &active_sha,
            &version,
            &stored_seal,
            &error
        ) ||
        !present ||
        active_sha == NULL ||
        stored_seal == NULL) {
        g_clear_error (&error);
        reason_code =
            "active_repository_row_missing";
        goto print;
    }

    if (g_strcmp0 (
            active_sha,
            NEW_SHA
        ) != 0) {
        reason_code = "unexpected_active_sha";
        goto print;
    }

    if (!final_exists ||
        !atm_snapshot_seal_compute (
            final_path,
            &computed_seal,
            &file_count,
            &total_bytes,
            &error
        )) {
        g_clear_error (&error);
        reason_code =
            "active_snapshot_unreadable";
        goto print;
    }

    has_seal_match = TRUE;
    seal_match =
        g_strcmp0 (
            stored_seal,
            computed_seal
        ) == 0;

    if (!seal_match) {
        reason_code =
            "active_snapshot_seal_mismatch";
        goto print;
    }

    classification =
        "NEW_AUTHORITY_VALID";
    reason_code =
        "new_generation_and_seal_valid";
    qualified = TRUE;

print:
    char *active_sha_json =
        active_sha != NULL
            ? g_strdup_printf (
                "\"%s\"",
                active_sha
            )
            : g_strdup ("null");
    char *stored_seal_json =
        stored_seal != NULL
            ? g_strdup_printf (
                "\"%s\"",
                stored_seal
            )
            : g_strdup ("null");
    char *computed_seal_json =
        computed_seal != NULL
            ? g_strdup_printf (
                "\"%s\"",
                computed_seal
            )
            : g_strdup ("null");

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"classification\":\"%s\","
        "\"active_generation_id\":%" G_GINT64_FORMAT ","
        "\"active_repository_sha\":%s,"
        "\"stored_seal\":%s,"
        "\"computed_seal\":%s,"
        "\"seal_match\":%s,"
        "\"final_snapshot_exists\":%s,"
        "\"staging_exists\":%s,"
        "\"repositories_exists\":%s,"
        "\"repository_directory_exists\":%s,"
        "\"snapshots_directory_exists\":%s,"
        "\"staging_root_exists\":%s,"
        "\"staging_repository_exists\":%s,"
        "\"namespace_clean\":%s,"
        "\"qualified\":%s,"
        "\"reason_code\":\"%s\""
        "}\n",
        classification,
        generation_id,
        active_sha_json,
        stored_seal_json,
        computed_seal_json,
        has_seal_match
            ? (seal_match ? "true" : "false")
            : "null",
        final_exists ? "true" : "false",
        staging_exists ? "true" : "false",
        path_exists_any (
            repositories_path
        ) ? "true" : "false",
        path_exists_any (
            repository_path
        ) ? "true" : "false",
        path_exists_any (
            snapshots_path
        ) ? "true" : "false",
        path_exists_any (
            staging_root
        ) ? "true" : "false",
        path_exists_any (
            staging_repository
        ) ? "true" : "false",
        staging_exists ? "false" : "true",
        qualified ? "true" : "false",
        reason_code
    );

    g_free (computed_seal_json);
    g_free (stored_seal_json);
    g_free (active_sha_json);
    g_free (computed_seal);
    g_free (stored_seal);
    g_free (version);
    g_free (active_sha);
    g_free (staging_repository);
    g_free (staging_root);
    g_free (snapshots_path);
    g_free (repository_path);
    g_free (repositories_path);
    g_free (staging_path);
    g_free (final_path);
    g_free (control_path);
    g_free (data_root);
    return TRUE;
}

static int
report_error (
    GError *error
)
{
    g_printerr (
        "a1-fresh-namespace-replay-helper: %s\n",
        error != NULL
            ? error->message
            : "unknown error"
    );
    g_clear_error (&error);
    return 2;
}

int
main (
    int argc,
    char **argv
)
{
    GError *error = NULL;
    gboolean ok = FALSE;

    if (argc == 3 &&
        g_strcmp0 (
            argv[1],
            "--initialize-empty"
        ) == 0) {
        ok = initialize_empty (
            argv[2],
            &error
        );
    } else if (argc == 5 &&
               g_strcmp0 (
                   argv[1],
                   "--promote-fresh-namespace-candidate-boundary"
               ) == 0) {
        ok = promote_candidate (
            argv[2],
            argv[3],
            argv[4],
            &error
        );
    } else if (argc == 3 &&
               g_strcmp0 (
                   argv[1],
                   "--verify-fresh"
               ) == 0) {
        ok = verify_fresh (argv[2]);
    } else {
        g_printerr (
            "Usage: a1-fresh-namespace-replay-helper "
            "--initialize-empty ROOT | "
            "--promote-fresh-namespace-candidate-boundary ROOT STRATEGY CHECKPOINT | "
            "--verify-fresh ROOT\n"
        );
        return 64;
    }

    if (!ok) {
        return report_error (error);
    }

    g_clear_error (&error);
    return 0;
}
