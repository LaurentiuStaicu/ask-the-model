#include "repository_reconcile.h"
#include "snapshot_seal.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include <sys/stat.h>
#include <unistd.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0) {
        return;
    }

    if (!S_ISDIR (st.st_mode) || S_ISLNK (st.st_mode)) {
        g_remove (path);
        return;
    }

    GDir *directory = g_dir_open (path, 0, NULL);
    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child =
                g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_rmdir (path);
}

static char *
new_temp_root (const char *pattern)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (pattern, &error);

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static char *
snapshot_path (
    const char *data_root,
    const char *repository_id,
    const char *sha
)
{
    return g_build_filename (
        data_root,
        "Repositories",
        repository_id,
        "snapshots",
        sha,
        NULL
    );
}

static char *
manifest_text (
    const char *repository_id,
    const char *acronym,
    const char *display_name
)
{
    return g_strdup_printf (
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"%s\",\n"
        "  \"acronym\": \"%s\",\n"
        "  \"display_name\": \"%s\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": ["
        "\"CITATION.cff\", \"STATUS.md\", \"model/core.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": ["
        "\"STATUS.md\", \"README.md\", \"CITATION.cff\"],\n"
        "    \"structural\": [\"model/core.json\"],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": [\".github\", \"__pycache__\"]\n"
        "  }\n"
        "}\n",
        repository_id,
        acronym,
        display_name
    );
}

static char *
create_valid_snapshot (
    const char *data_root,
    const char *repository_id,
    const char *acronym,
    const char *display_name,
    const char *sha,
    const char *version
)
{
    GError *error = NULL;
    char *root = snapshot_path (
        data_root,
        repository_id,
        sha
    );
    char *atm =
        g_build_filename (root, ".atm", NULL);
    char *model =
        g_build_filename (root, "model", NULL);

    g_assert_cmpint (
        g_mkdir_with_parents (atm, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir_with_parents (model, 0700),
        ==,
        0
    );

    char *citation =
        g_build_filename (root, "CITATION.cff", NULL);
    char *status =
        g_build_filename (root, "STATUS.md", NULL);
    char *readme =
        g_build_filename (root, "README.md", NULL);
    char *core =
        g_build_filename (root, "model", "core.json", NULL);
    char *manifest_path =
        g_build_filename (
            root,
            ".atm",
            "repository.json",
            NULL
        );

    char *citation_text = g_strdup_printf (
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Test\n"
        "version: %s\n",
        version
    );
    char *manifest =
        manifest_text (
            repository_id,
            acronym,
            display_name
        );

    g_assert_true (
        g_file_set_contents (
            citation,
            citation_text,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (
            status,
            "# Status\nValidated test status.\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (
            readme,
            "# Test repository\nGrounded evidence.\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (
            core,
            "{\"entity\": \"test\"}\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (
            manifest_path,
            manifest,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (manifest);
    g_free (citation_text);
    g_free (manifest_path);
    g_free (core);
    g_free (readme);
    g_free (status);
    g_free (citation);
    g_free (model);
    g_free (atm);
    return root;
}

static AtmRepositoryReconcileResult *
reconcile_ready (
    const char *cache_root,
    const char *snapshot_root,
    const char *sha
)
{
    GError *error = NULL;
    AtmRepositoryReconcileResult *result = NULL;

    g_assert_true (
        atm_repository_reconcile_local (
            cache_root,
            snapshot_root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            sha,
            "0.1.0",
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (result);
    return result;
}

static void
write_state (
    const char *state_root,
    const char *snapshot_root,
    const char *sha
)
{
    GError *error = NULL;
    char *path =
        g_build_filename (
            state_root,
            "repository-state.json",
            NULL
        );
    char *snapshot_seal = NULL;
    guint64 sealed_files = 0;
    guint64 sealed_bytes = 0;

    g_assert_true (
        atm_snapshot_seal_compute (
            snapshot_root,
            &snapshot_seal,
            &sealed_files,
            &sealed_bytes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (snapshot_seal);

    char *contents = g_strdup_printf (
        "{\n"
        "  \"schema_version\": 2,\n"
        "  \"repositories\": [\n"
        "    {\"id\": \"ewd\", \"sha\": \"%s\", "
        "\"version\": \"0.1.0\", "
        "\"snapshot_seal_sha256\": \"%s\"}\n"
        "  ]\n"
        "}\n",
        sha,
        snapshot_seal
    );

    g_assert_cmpint (
        g_mkdir_with_parents (state_root, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (contents);
    g_free (snapshot_seal);
    g_free (path);
}

static gboolean
oracle_accepts (
    const char *oracle_path,
    const char *data_root,
    const char *cache_root,
    const char *state_root,
    char **out_stdout,
    char **out_stderr
)
{
    gchar *argv[] = {
        (gchar *) oracle_path,
        "--data-root",
        (gchar *) data_root,
        "--cache-root",
        (gchar *) cache_root,
        "--state-root",
        (gchar *) state_root,
        "--repository-id",
        "ewd",
        "--acronym",
        "EWD",
        "--display-name",
        "Empirical World3 Dynamics",
        NULL
    };
    GError *error = NULL;
    gint wait_status = 0;

    gboolean spawned = g_spawn_sync (
        NULL,
        argv,
        NULL,
        G_SPAWN_DEFAULT,
        NULL,
        NULL,
        out_stdout,
        out_stderr,
        &wait_status,
        &error
    );

    g_assert_true (spawned);
    g_assert_no_error (error);

    GError *status_error = NULL;
    gboolean accepted =
        g_spawn_check_wait_status (
            wait_status,
            &status_error
        );
    g_clear_error (&status_error);
    return accepted;
}

static char *
read_index_source_hash (
    const char *index_path,
    const char *path
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    char *hash = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            "SELECT sha256 FROM source_files WHERE path=?1;",
            -1,
            &statement,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_bind_text (
        statement,
        1,
        path,
        -1,
        SQLITE_STATIC
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_ROW
    );
    hash = g_strdup (
        (const char *) sqlite3_column_text (
            statement,
            0
        )
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_DONE
    );

    sqlite3_finalize (statement);
    sqlite3_close (db);
    return hash;
}

static void
set_index_source_hash (
    const char *index_path,
    const char *path,
    const char *hash
)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            "UPDATE source_files SET sha256=?1 WHERE path=?2;",
            -1,
            &statement,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_bind_text (
        statement,
        1,
        hash,
        -1,
        SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement,
        2,
        path,
        -1,
        SQLITE_STATIC
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_DONE
    );

    sqlite3_finalize (statement);
    sqlite3_close (db);
}

static void
assert_oracle (
    gboolean expected,
    const char *oracle_path,
    const char *data_root,
    const char *cache_root,
    const char *state_root
)
{
    char *standard_output = NULL;
    char *standard_error = NULL;
    gboolean actual =
        oracle_accepts (
            oracle_path,
            data_root,
            cache_root,
            state_root,
            &standard_output,
            &standard_error
        );

    if (actual != expected) {
        g_test_message (
            "oracle stdout: %s",
            standard_output != NULL
                ? standard_output
                : ""
        );
        g_test_message (
            "oracle stderr: %s",
            standard_error != NULL
                ? standard_error
                : ""
        );
    }

    g_assert_cmpint (actual, ==, expected);
    g_free (standard_output);
    g_free (standard_error);
}

static void
test_production_ready_then_perturbed (void)
{
    const char *oracle_path =
        g_getenv ("ATM_REPOSITORY_ORACLE");
    g_assert_nonnull (oracle_path);
    g_assert_cmpstr (oracle_path, !=, "");

    const char *sha =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char *root = new_temp_root (
        "atm-oracle-integration-XXXXXX"
    );
    char *data_root =
        g_build_filename (root, "data", NULL);
    char *cache_root =
        g_build_filename (root, "cache", NULL);
    char *state_root =
        g_build_filename (root, "state", NULL);

    g_assert_cmpint (
        g_mkdir_with_parents (data_root, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir_with_parents (cache_root, 0700),
        ==,
        0
    );

    char *snapshot = create_valid_snapshot (
        data_root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    AtmRepositoryReconcileResult *first =
        reconcile_ready (
            cache_root,
            snapshot,
            sha
        );
    g_assert_cmpint (
        first->status,
        ==,
        ATM_REPOSITORY_RECONCILE_READY_REPAIRED_INDEX
    );
    g_assert_nonnull (first->index_path);
    char *index_path =
        g_strdup (first->index_path);
    atm_repository_reconcile_result_free (first);

    AtmRepositoryReconcileResult *second =
        reconcile_ready (
            cache_root,
            snapshot,
            sha
        );
    g_assert_cmpint (
        second->status,
        ==,
        ATM_REPOSITORY_RECONCILE_READY
    );
    atm_repository_reconcile_result_free (second);

    write_state (
        state_root,
        snapshot,
        sha
    );

    /* Independent oracle agrees with the production READY generation. */
    assert_oracle (
        TRUE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    /*
     * Fault 1: mutate an indexed source after production reported READY.
     * Production reconciliation is deliberately NOT called again.
     */
    GError *error = NULL;
    char *status =
        g_build_filename (snapshot, "STATUS.md", NULL);
    g_assert_true (
        g_file_set_contents (
            status,
            "# Status\nPERTURBED AFTER READY.\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    assert_oracle (
        FALSE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    g_assert_true (
        g_file_set_contents (
            status,
            "# Status\nValidated test status.\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    assert_oracle (
        TRUE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    /*
     * Fault 2: replace a required/indexed structural source with a symlink.
     * The oracle must reject before following the link.
     */
    char *core =
        g_build_filename (
            snapshot,
            "model",
            "core.json",
            NULL
        );
    char *external =
        g_build_filename (
            root,
            "external-core.json",
            NULL
        );
    g_assert_true (
        g_file_set_contents (
            external,
            "{\"entity\": \"external\"}\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (g_remove (core), ==, 0);
    g_assert_cmpint (
        symlink (external, core),
        ==,
        0
    );

    assert_oracle (
        FALSE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    g_assert_cmpint (g_remove (core), ==, 0);
    g_assert_true (
        g_file_set_contents (
            core,
            "{\"entity\": \"test\"}\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    assert_oracle (
        TRUE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    /*
     * Fault 3: tamper with indexed provenance after READY while keeping the
     * SQLite database structurally valid.
     */
    char *original_hash =
        read_index_source_hash (
            index_path,
            "STATUS.md"
        );
    set_index_source_hash (
        index_path,
        "STATUS.md",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    );

    assert_oracle (
        FALSE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    set_index_source_hash (
        index_path,
        "STATUS.md",
        original_hash
    );

    assert_oracle (
        TRUE,
        oracle_path,
        data_root,
        cache_root,
        state_root
    );

    g_free (original_hash);
    g_free (external);
    g_free (core);
    g_free (status);
    g_free (index_path);
    g_free (snapshot);
    g_free (state_root);
    g_free (cache_root);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/oracle/production-ready-then-perturbed",
        test_production_ready_then_perturbed
    );

    return g_test_run ();
}
