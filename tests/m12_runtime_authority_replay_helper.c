#include "control_state.h"
#include "fault_injection_support.h"
#include "repository_capacity_ui_bridge.h"
#include "repository_ingest.h"
#include "repository_mutation_lease.h"
#include "repository_storage.h"
#include "repository_ui_bridge.h"
#include "snapshot_seal.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define CHECKPOINT_FD 3
#define CONTROL_FD 4

static const char *REPOSITORY_ID = "ewd";
static const char *REPOSITORY_ACRONYM = "EWD";
static const char *REPOSITORY_NAME =
    "Empirical World3 Dynamics";
static const char *SNAPSHOT_SHA =
    "0123456789abcdef0123456789abcdef01234567";
static const char *VERSION = "0.1.0";

typedef struct {
    const char *path;
    const char *content;
    mode_t filetype;
} FixtureEntry;

static char *
data_root_for (
    const char *root
)
{
    return g_build_filename (root, "data", NULL);
}

static char *
state_root_for (
    const char *root
)
{
    return g_build_filename (root, "state", NULL);
}

static char *
cache_root_for (
    const char *root
)
{
    return g_build_filename (root, "cache", NULL);
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

static char *
archive_path_for (
    const char *root
)
{
    return g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
}

static char *
valid_manifest (void)
{
    return g_strdup (
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"acronym\": \"EWD\",\n"
        "  \"display_name\": \"Empirical World3 Dynamics\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": ["
        "\"CITATION.cff\", \"STATUS.md\", \"model/core.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": ["
        "\"STATUS.md\", \"README.md\", \"CITATION.cff\"],\n"
        "    \"structural\": [\"model\"],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": [\".github\", \"__pycache__\"]\n"
        "  }\n"
        "}\n"
    );
}

static gboolean
write_archive (
    const char *archive_path,
    GError **error
)
{
    char *manifest = valid_manifest ();
    const FixtureEntry entries[] = {
        { "repo-sha/", NULL, AE_IFDIR },
        { "repo-sha/.atm/", NULL, AE_IFDIR },
        {
            "repo-sha/.atm/repository.json",
            manifest,
            AE_IFREG
        },
        {
            "repo-sha/CITATION.cff",
            "cff-version: 1.2.0\n"
            "message: cite this\n"
            "type: software\n"
            "title: Test repository\n"
            "version: 0.1.0\n",
            AE_IFREG
        },
        {
            "repo-sha/STATUS.md",
            "# Status\n",
            AE_IFREG
        },
        {
            "repo-sha/README.md",
            "# Readme\n",
            AE_IFREG
        },
        { "repo-sha/model/", NULL, AE_IFDIR },
        {
            "repo-sha/model/core.json",
            "{}\n",
            AE_IFREG
        }
    };

    struct archive *writer = archive_write_new ();

    if (writer == NULL) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not allocate M12 archive writer."
        );
        g_free (manifest);
        return FALSE;
    }

    gboolean ok = FALSE;

    if (archive_write_set_format_pax_restricted (writer) !=
            ARCHIVE_OK ||
        archive_write_add_filter_gzip (writer) !=
            ARCHIVE_OK ||
        archive_write_open_filename (
            writer,
            archive_path
        ) != ARCHIVE_OK) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not initialize M12 archive: %s",
            archive_error_string (writer)
        );
        goto out;
    }

    for (gsize i = 0; i < G_N_ELEMENTS (entries); i++) {
        const FixtureEntry *fixture = &entries[i];
        struct archive_entry *entry =
            archive_entry_new ();
        gsize length = fixture->content != NULL
            ? strlen (fixture->content)
            : 0;

        if (entry == NULL) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "Could not allocate M12 archive entry."
            );
            goto out;
        }

        archive_entry_set_pathname (
            entry,
            fixture->path
        );
        archive_entry_set_filetype (
            entry,
            fixture->filetype
        );
        archive_entry_set_perm (
            entry,
            fixture->filetype == AE_IFDIR
                ? 0700
                : 0600
        );
        archive_entry_set_size (
            entry,
            fixture->filetype == AE_IFREG
                ? (int64_t) length
                : 0
        );

        if (archive_write_header (
                writer,
                entry
            ) != ARCHIVE_OK) {
            archive_entry_free (entry);
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "Could not write M12 archive header: %s",
                archive_error_string (writer)
            );
            goto out;
        }

        if (fixture->filetype == AE_IFREG &&
            length > 0 &&
            archive_write_data (
                writer,
                fixture->content,
                length
            ) != (la_ssize_t) length) {
            archive_entry_free (entry);
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "Could not write M12 archive payload: %s",
                archive_error_string (writer)
            );
            goto out;
        }

        archive_entry_free (entry);
    }

    if (archive_write_close (writer) != ARCHIVE_OK) {
        g_set_error (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "Could not close M12 archive: %s",
            archive_error_string (writer)
        );
        goto out_no_close;
    }

    ok = TRUE;

out_no_close:
    archive_write_free (writer);
    g_free (manifest);
    return ok;

out:
    archive_write_close (writer);
    goto out_no_close;
}

static gboolean
initialize_root (
    const char *root,
    GError **error
)
{
    char *data_root = data_root_for (root);
    char *state_root = state_root_for (root);
    char *cache_root = cache_root_for (root);
    char *control_path = control_path_for (root);
    char *archive_path = archive_path_for (root);
    AtmControlStateStore *store = NULL;
    gboolean ok = FALSE;

    if (g_mkdir_with_parents (data_root, 0700) != 0 ||
        g_mkdir_with_parents (state_root, 0700) != 0 ||
        g_mkdir_with_parents (cache_root, 0700) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create M12 roots: %s",
            g_strerror (errno)
        );
        goto out;
    }

    if (!atm_control_state_open (
            control_path,
            &store,
            error
        ) ||
        !write_archive (
            archive_path,
            error
        )) {
        goto out;
    }

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"initialized\":true,"
        "\"active_generation_id\":0"
        "}\n"
    );
    ok = TRUE;

out:
    if (store != NULL) {
        atm_control_state_close (store);
    }
    g_free (archive_path);
    g_free (control_path);
    g_free (cache_root);
    g_free (state_root);
    g_free (data_root);
    return ok;
}

static gboolean
checkpoint_valid (
    const char *checkpoint
)
{
    static const char *valid[] = {
        "runtime_post_durable_ingest_pre_preindex_seal",
        "runtime_post_preindex_seal_pre_index",
        "runtime_post_index_pre_postindex_seal",
        "runtime_post_postindex_seal_pre_state_capacity",
        "runtime_post_state_capacity_pre_authority",
        "runtime_after_authority"
    };

    for (gsize i = 0;
         i < G_N_ELEMENTS (valid);
         i++) {
        if (g_strcmp0 (
                checkpoint,
                valid[i]
            ) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
run_pipeline (
    const char *root,
    const char *checkpoint,
    gboolean force_seal_mismatch,
    GError **error
)
{
    if (checkpoint != NULL &&
        !checkpoint_valid (checkpoint)) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_INVAL,
            "Unknown M12 runtime checkpoint."
        );
        return FALSE;
    }

    char *data_root = data_root_for (root);
    char *state_root = state_root_for (root);
    char *cache_root = cache_root_for (root);
    char *control_path = control_path_for (root);
    char *archive_path = archive_path_for (root);
    char *pre_barrier_seal = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    char *pre_index_seal = NULL;
    char *post_index_seal = NULL;
    char *index_path = NULL;
    char *index_version = NULL;
    char *capacity_detail = NULL;
    gint lease_fd = -1;
    gboolean contended = FALSE;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    guint64 pre_files = 0;
    guint64 pre_bytes = 0;
    guint64 post_files = 0;
    guint64 post_bytes = 0;
    gboolean capacity_admitted = FALSE;
    gint64 generation_before = -1;
    gint64 generation_after = -1;
    gboolean ok = FALSE;

    if (!atm_repository_mutation_lease_try_acquire (
            state_root,
            &lease_fd,
            &contended,
            error
        ) ||
        contended ||
        lease_fd < 0) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "Could not acquire M12 mutation lease."
            );
        }
        goto out;
    }

    if (checkpoint != NULL) {
        atm_test_fault_configure (
            checkpoint,
            CHECKPOINT_FD,
            CONTROL_FD
        );
    }

    if (!atm_repository_ingest_archive_durable (
            data_root,
            archive_path,
            REPOSITORY_ID,
            REPOSITORY_ACRONYM,
            REPOSITORY_NAME,
            SNAPSHOT_SHA,
            &pre_barrier_seal,
            &version,
            &snapshot_path,
            &entries,
            &total_bytes,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "runtime_post_durable_ingest_pre_preindex_seal"
    );

    if (force_seal_mismatch) {
        char *status_path = g_build_filename (
            snapshot_path,
            "STATUS.md",
            NULL
        );

        if (!g_file_set_contents (
                status_path,
                "# Status\nmutated-after-barrier\n",
                -1,
                error
            )) {
            g_free (status_path);
            goto out;
        }

        g_free (status_path);
    }

    if (!atm_snapshot_seal_compute (
            snapshot_path,
            &pre_index_seal,
            &pre_files,
            &pre_bytes,
            error
        )) {
        goto out;
    }

    if (g_strcmp0 (
            pre_barrier_seal,
            pre_index_seal
        ) != 0) {
        if (force_seal_mismatch) {
            g_print (
                "{"
                "\"schema_version\":1,"
                "\"seal_mismatch_detected\":true,"
                "\"authority_published\":false"
                "}\n"
            );
            ok = TRUE;
            goto out;
        }

        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "M12 durable pre-barrier seal mismatch."
        );
        goto out;
    }

    if (force_seal_mismatch) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "M12 expected seal mismatch was not detected."
        );
        goto out;
    }

    atm_test_fault_checkpoint (
        "runtime_post_preindex_seal_pre_index"
    );

    if (!atm_repository_ui_ensure_index (
            state_root,
            TRUE,
            cache_root,
            snapshot_path,
            REPOSITORY_ID,
            SNAPSHOT_SHA,
            &index_path,
            &index_version,
            error
        )) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "runtime_post_index_pre_postindex_seal"
    );

    if (!atm_snapshot_seal_compute (
            snapshot_path,
            &post_index_seal,
            &post_files,
            &post_bytes,
            error
        )) {
        goto out;
    }

    if (g_strcmp0 (
            pre_index_seal,
            post_index_seal
        ) != 0 ||
        pre_files != post_files ||
        pre_bytes != post_bytes) {
        g_set_error_literal (
            error,
            G_FILE_ERROR,
            G_FILE_ERROR_FAILED,
            "M12 snapshot changed during index preparation."
        );
        goto out;
    }

    atm_test_fault_checkpoint (
        "runtime_post_postindex_seal_pre_state_capacity"
    );

    if (!atm_repository_capacity_ui_state_commit_preflight (
            state_root,
            &capacity_admitted,
            &capacity_detail,
            error
        ) ||
        !capacity_admitted) {
        if (error != NULL && *error == NULL) {
            g_set_error (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_NOSPC,
                "M12 state capacity rejected publication: %s",
                capacity_detail != NULL
                    ? capacity_detail
                    : "no detail"
            );
        }
        goto out;
    }

    atm_test_fault_checkpoint (
        "runtime_post_state_capacity_pre_authority"
    );

    if (!atm_control_state_active_generation_id (
            control_path,
            &generation_before,
            error
        ) ||
        generation_before != 0) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                G_FILE_ERROR,
                G_FILE_ERROR_FAILED,
                "M12 expected empty authority generation before publication."
            );
        }
        goto out;
    }

    if (!atm_control_state_set_current_values_guarded (
            control_path,
            generation_before,
            REPOSITORY_ID,
            SNAPSHOT_SHA,
            index_version,
            post_index_seal,
            &generation_after,
            error
        ) ||
        generation_after <= generation_before) {
        goto out;
    }

    atm_test_fault_checkpoint (
        "runtime_after_authority"
    );

    g_print (
        "{"
        "\"schema_version\":1,"
        "\"pre_barrier_seal\":\"%s\","
        "\"post_index_seal\":\"%s\","
        "\"index_version\":\"%s\","
        "\"entries\":%" G_GUINT64_FORMAT ","
        "\"total_bytes\":%" G_GUINT64_FORMAT ","
        "\"generation_before\":%" G_GINT64_FORMAT ","
        "\"generation_after\":%" G_GINT64_FORMAT
        "}\n",
        pre_barrier_seal,
        post_index_seal,
        index_version,
        entries,
        total_bytes,
        generation_before,
        generation_after
    );

    ok = TRUE;

out:
    if (lease_fd >= 0) {
        atm_repository_mutation_lease_release (
            lease_fd
        );
    }
    g_free (capacity_detail);
    g_free (index_version);
    g_free (index_path);
    g_free (post_index_seal);
    g_free (pre_index_seal);
    g_free (snapshot_path);
    g_free (version);
    g_free (pre_barrier_seal);
    g_free (archive_path);
    g_free (control_path);
    g_free (cache_root);
    g_free (state_root);
    g_free (data_root);
    return ok;
}

static gboolean
verify_root (
    const char *root
)
{
    char *data_root = data_root_for (root);
    char *control_path = control_path_for (root);
    char *snapshot_path =
        atm_repository_snapshot_path (
            data_root,
            REPOSITORY_ID,
            SNAPSHOT_SHA
        );
    gint64 generation_id = -1;
    gboolean present = FALSE;
    char *active_sha = NULL;
    char *version = NULL;
    char *stored_seal = NULL;
    char *computed_seal = NULL;
    guint64 files = 0;
    guint64 bytes = 0;
    GError *error = NULL;
    const char *classification =
        "INVALID_AUTHORITY";
    const char *reason_code = "unknown";
    gboolean qualified = FALSE;
    gboolean seal_match = FALSE;
    gboolean has_seal_match = FALSE;

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
            "no_active_repository_generation";
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
            "active_repository_state_missing";
        goto print;
    }

    if (g_strcmp0 (
            active_sha,
            SNAPSHOT_SHA
        ) != 0 ||
        g_strcmp0 (
            version,
            VERSION
        ) != 0) {
        reason_code =
            "unexpected_active_identity";
        goto print;
    }

    if (!atm_snapshot_seal_compute (
            snapshot_path,
            &computed_seal,
            &files,
            &bytes,
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
        "guarded_generation_and_seal_valid";
    qualified = TRUE;

print:
    g_print (
        "{"
        "\"schema_version\":1,"
        "\"classification\":\"%s\","
        "\"active_generation_id\":%" G_GINT64_FORMAT ","
        "\"active_repository_sha\":%s,"
        "\"seal_match\":%s,"
        "\"qualified\":%s,"
        "\"reason_code\":\"%s\""
        "}\n",
        classification,
        generation_id,
        active_sha != NULL
            ? g_strdup_printf (
                "\"%s\"",
                active_sha
            )
            : "null",
        has_seal_match
            ? (seal_match ? "true" : "false")
            : "null",
        qualified ? "true" : "false",
        reason_code
    );

    g_clear_error (&error);
    g_free (computed_seal);
    g_free (stored_seal);
    g_free (version);
    g_free (active_sha);
    g_free (snapshot_path);
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
        "m12-runtime-authority-helper: %s\n",
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

    if (argc == 3 &&
        g_strcmp0 (
            argv[1],
            "--initialize"
        ) == 0) {
        return initialize_root (
            argv[2],
            &error
        ) ? 0 : report_error (error);
    }

    if (argc == 4 &&
        g_strcmp0 (
            argv[1],
            "--run-boundary"
        ) == 0) {
        return run_pipeline (
            argv[2],
            argv[3],
            FALSE,
            &error
        ) ? 0 : report_error (error);
    }

    if (argc == 3 &&
        g_strcmp0 (
            argv[1],
            "--seal-mismatch"
        ) == 0) {
        return run_pipeline (
            argv[2],
            NULL,
            TRUE,
            &error
        ) ? 0 : report_error (error);
    }

    if (argc == 3 &&
        g_strcmp0 (
            argv[1],
            "--verify"
        ) == 0) {
        return verify_root (argv[2])
            ? 0
            : 2;
    }

    g_printerr (
        "usage: %s --initialize ROOT | "
        "--run-boundary ROOT CHECKPOINT | "
        "--seal-mismatch ROOT | "
        "--verify ROOT\n",
        argv[0]
    );
    return 64;
}
