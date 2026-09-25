#include "control_state.h"
#include "fault_injection_support.h"
#include "repository_storage.h"
#include "snapshot_seal.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>

#define CHECKPOINT_FD 3
#define CONTROL_FD 4

static const char *REPOSITORY_ID = "ewd";
static const char *OLD_SHA =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *NEW_SHA =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

static char *
data_root_for (const char *root)
{
    return g_build_filename (root, "data", NULL);
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
path_exists_any (const char *path)
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

    if (g_mkdir_with_parents (state_root, 0700) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create A1-M4 state root."
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
write_fixture (
    const char *staging,
    const char *label,
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

    if (g_mkdir_with_parents (nested, 0700) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create A1-M4 snapshot fixture directories."
        );
        goto out;
    }

    char *status_text = g_strdup_printf (
        "# A1-M4 %s snapshot\n",
        label
    );

    if (!g_file_set_contents (
            status,
            status_text,
            -1,
            error
        )) {
        g_free (status_text);
        goto out;
    }
    g_free (status_text);

    for (guint i = 0; i < 32768; i++) {
        g_string_append_printf (
            payload,
            "atm-a1-m4-%s-line-%05u\n",
            label,
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

static gboolean
prepare_and_activate (
    const char *root,
    const char *sha,
    const char *version,
    const char *label,
    const char *checkpoint,
    GError **error
)
{
    char *data_root = data_root_for (root);
    char *control_path = control_path_for (root);
    char *staging = NULL;
    char *promoted = NULL;
    char *seal = NULL;
    guint64 file_count = 0;
    guint64 total_bytes = 0;
    gboolean ok = FALSE;

    staging = atm_repository_extraction_staging_path (
        data_root,
        REPOSITORY_ID,
        sha
    );

    if (staging == NULL ||
        g_mkdir_with_parents (staging, 0700) != 0) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not create A1-M4 snapshot staging."
        );
        goto out;
    }

    if (!write_fixture (
            staging,
            label,
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

    if (checkpoint != NULL) {
        atm_test_fault_configure (
            checkpoint,
            CHECKPOINT_FD,
            CONTROL_FD
        );
    }

    if (!atm_repository_promote_snapshot (
            data_root,
            REPOSITORY_ID,
            sha,
            staging,
            &promoted,
            error
        )) {
        goto out;
    }

    if (!atm_control_state_set_current_values (
            control_path,
            REPOSITORY_ID,
            sha,
            version,
            seal,
            error
        )) {
        goto out;
    }

    g_print (
        "{"
        "\"sha\":\"%s\","
        "\"seal\":\"%s\","
        "\"file_count\":%" G_GUINT64_FORMAT ","
        "\"total_bytes\":%" G_GUINT64_FORMAT
        "}\n",
        sha,
        seal,
        file_count,
        total_bytes
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
initialize_old (
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

    return prepare_and_activate (
        root,
        OLD_SHA,
        "0.0-old",
        "old",
        NULL,
        error
    );
}

static gboolean
promote_new (
    const char *root,
    const char *checkpoint,
    GError **error
)
{
    return prepare_and_activate (
        root,
        NEW_SHA,
        "0.0-new",
        "new",
        checkpoint,
        error
    );
}

static void
print_invalid (
    const char *reason_code
)
{
    g_print (
        "{"
        "\"schema_version\":1,"
        "\"classification\":\"INVALID_AUTHORITY\","
        "\"active_repository_sha\":null,"
        "\"stored_seal\":null,"
        "\"computed_seal\":null,"
        "\"seal_match\":false,"
        "\"old_snapshot_exists\":false,"
        "\"new_snapshot_exists\":false,"
        "\"new_staging_exists\":false,"
        "\"qualified\":false,"
        "\"reason_code\":\"%s\""
        "}\n",
        reason_code
    );
}

static gboolean
verify_replayed_authority (
    const char *root
)
{
    char *data_root = data_root_for (root);
    char *control_path = control_path_for (root);
    char *old_path = atm_repository_snapshot_path (
        data_root,
        REPOSITORY_ID,
        OLD_SHA
    );
    char *new_path = atm_repository_snapshot_path (
        data_root,
        REPOSITORY_ID,
        NEW_SHA
    );
    char *new_staging =
        atm_repository_extraction_staging_path (
            data_root,
            REPOSITORY_ID,
            NEW_SHA
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
    const char *classification = "INVALID_AUTHORITY";
    const char *reason_code = "unknown";
    gboolean seal_match = FALSE;
    gboolean qualified = FALSE;

    if (!atm_control_state_active_generation_id (
            control_path,
            &generation_id,
            &error
        ) ||
        generation_id <= 0 ||
        !atm_control_state_load_repository_values_at_generation (
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
        print_invalid ("control_state_unreadable_or_missing");
        goto out;
    }

    const char *active_path = NULL;

    if (g_strcmp0 (active_sha, OLD_SHA) == 0) {
        active_path = old_path;
    } else if (g_strcmp0 (active_sha, NEW_SHA) == 0) {
        active_path = new_path;
    } else {
        print_invalid ("unexpected_active_sha");
        goto out;
    }

    if (!atm_snapshot_seal_compute (
            active_path,
            &computed_seal,
            &file_count,
            &total_bytes,
            &error
        )) {
        g_clear_error (&error);
        print_invalid ("active_snapshot_unreadable");
        goto out;
    }

    seal_match =
        g_strcmp0 (stored_seal, computed_seal) == 0;

    if (!seal_match) {
        classification = "INVALID_AUTHORITY";
        reason_code = "active_snapshot_seal_mismatch";
    } else if (g_strcmp0 (active_sha, OLD_SHA) == 0) {
        classification = "OLD_AUTHORITY_VALID";
        reason_code = "old_generation_and_seal_valid";
        qualified = TRUE;
    } else {
        classification = "NEW_AUTHORITY_VALID";
        reason_code = "new_generation_and_seal_valid";
        qualified = TRUE;
    }

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"classification\":\"%s\","
        "\"active_generation_id\":%" G_GINT64_FORMAT ","
        "\"active_repository_sha\":\"%s\","
        "\"stored_seal\":\"%s\","
        "\"computed_seal\":\"%s\","
        "\"seal_match\":%s,"
        "\"active_file_count\":%" G_GUINT64_FORMAT ","
        "\"active_total_bytes\":%" G_GUINT64_FORMAT ","
        "\"old_snapshot_exists\":%s,"
        "\"new_snapshot_exists\":%s,"
        "\"new_staging_exists\":%s,"
        "\"qualified\":%s,"
        "\"reason_code\":\"%s\""
        "}\n",
        classification,
        generation_id,
        active_sha,
        stored_seal,
        computed_seal,
        seal_match ? "true" : "false",
        file_count,
        total_bytes,
        path_exists_any (old_path) ? "true" : "false",
        path_exists_any (new_path) ? "true" : "false",
        path_exists_any (new_staging) ? "true" : "false",
        qualified ? "true" : "false",
        reason_code
    );

out:
    g_free (computed_seal);
    g_free (stored_seal);
    g_free (version);
    g_free (active_sha);
    g_free (new_staging);
    g_free (new_path);
    g_free (old_path);
    g_free (control_path);
    g_free (data_root);
    return TRUE;
}

static int
report_error (GError *error)
{
    g_printerr (
        "a1-snapshot-replay-helper: %s\n",
        error != NULL ? error->message : "unknown error"
    );
    g_clear_error (&error);
    return 2;
}

int
main (int argc, char **argv)
{
    GError *error = NULL;
    gboolean ok = FALSE;

    if (argc == 3 &&
        g_strcmp0 (argv[1], "--initialize-old") == 0) {
        ok = initialize_old (argv[2], &error);
    } else if (argc == 4 &&
               g_strcmp0 (argv[1], "--promote-new") == 0) {
        ok = promote_new (
            argv[2],
            argv[3],
            &error
        );
    } else if (argc == 3 &&
               g_strcmp0 (argv[1], "--verify") == 0) {
        ok = verify_replayed_authority (argv[2]);
    } else {
        g_printerr (
            "Usage: a1-snapshot-replay-helper "
            "--initialize-old ROOT | "
            "--promote-new ROOT CHECKPOINT | "
            "--verify ROOT\n"
        );
        return 64;
    }

    if (!ok) {
        return report_error (error);
    }

    g_clear_error (&error);
    return 0;
}
