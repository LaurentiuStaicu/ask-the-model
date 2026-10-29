#include "repository_ingest.h"
#include "repository_storage.h"
#include "snapshot_seal.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *path;
    const char *content;
    mode_t filetype;
} FixtureEntry;

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) || S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static char *
new_temp_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-ingest-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_archive_entries (
    const char *archive_path,
    const FixtureEntry *entries,
    gsize entry_count
)
{
    struct archive *writer = archive_write_new ();

    g_assert_nonnull (writer);
    g_assert_cmpint (
        archive_write_set_format_pax_restricted (writer),
        ==,
        ARCHIVE_OK
    );
    g_assert_cmpint (
        archive_write_add_filter_gzip (writer),
        ==,
        ARCHIVE_OK
    );
    g_assert_cmpint (
        archive_write_open_filename (writer, archive_path),
        ==,
        ARCHIVE_OK
    );

    for (gsize i = 0; i < entry_count; i++) {
        const FixtureEntry *fixture = &entries[i];
        struct archive_entry *entry = archive_entry_new ();
        gsize length =
            fixture->content != NULL ? strlen (fixture->content) : 0;

        archive_entry_set_pathname (entry, fixture->path);
        archive_entry_set_filetype (entry, fixture->filetype);
        archive_entry_set_perm (
            entry,
            fixture->filetype == AE_IFDIR ? 0700 : 0600
        );
        archive_entry_set_size (
            entry,
            fixture->filetype == AE_IFREG ? (int64_t) length : 0
        );

        g_assert_cmpint (
            archive_write_header (writer, entry),
            ==,
            ARCHIVE_OK
        );

        if (fixture->filetype == AE_IFREG && length > 0) {
            g_assert_cmpint (
                archive_write_data (
                    writer,
                    fixture->content,
                    length
                ),
                ==,
                (la_ssize_t) length
            );
        }

        archive_entry_free (entry);
    }

    g_assert_cmpint (
        archive_write_close (writer),
        ==,
        ARCHIVE_OK
    );
    g_assert_cmpint (
        archive_write_free (writer),
        ==,
        ARCHIVE_OK
    );
}

static void
write_archive (
    const char *archive_path,
    const char *manifest
)
{
    const FixtureEntry entries[] = {
        { "repo-sha/", NULL, AE_IFDIR },
        { "repo-sha/.atm/", NULL, AE_IFDIR },
        { "repo-sha/.atm/repository.json", manifest, AE_IFREG },
        {
            "repo-sha/CITATION.cff",
            "cff-version: 1.2.0\n"
            "message: cite this\n"
            "type: software\n"
            "title: Test repository\n"
            "version: 0.1.0\n",
            AE_IFREG
        },
        { "repo-sha/STATUS.md", "# Status\n", AE_IFREG },
        { "repo-sha/README.md", "# Readme\n", AE_IFREG },
        { "repo-sha/model/", NULL, AE_IFDIR },
        { "repo-sha/model/core.json", "{}\n", AE_IFREG }
    };
    write_archive_entries (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );
}

static char *
valid_manifest (const char *repository_id)
{
    return g_strdup_printf (
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"%s\",\n"
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
        "}\n",
        repository_id
    );
}

static const char *
test_sha (void)
{
    return "0123456789abcdef0123456789abcdef01234567";
}

static void
test_successful_ingest (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    char *version = NULL;
    char *snapshot_path = NULL;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    GError *error = NULL;

    write_archive (archive_path, manifest);

    g_assert_true (
        atm_repository_ingest_archive (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            &version,
            &snapshot_path,
            &entries,
            &total_bytes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (version, ==, "0.1.0");
    g_assert_cmpuint (entries, ==, 8);
    g_assert_cmpuint (total_bytes, >, 0);
    g_assert_true (
        g_file_test (snapshot_path, G_FILE_TEST_IS_DIR)
    );

    char *expected = atm_repository_snapshot_path (
        data_root,
        "ewd",
        test_sha ()
    );
    char *staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        test_sha ()
    );

    g_assert_cmpstr (snapshot_path, ==, expected);
    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));

    g_free (staging);
    g_free (expected);
    g_free (snapshot_path);
    g_free (version);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_durable_ingest_success (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    char *pre_barrier_seal = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    char *final_seal = NULL;
    guint64 entries = 0;
    guint64 total_bytes = 0;
    guint64 sealed_files = 0;
    guint64 sealed_bytes = 0;
    GError *error = NULL;

    write_archive (archive_path, manifest);
    g_assert_cmpint (
        g_mkdir_with_parents (
            data_root,
            0700
        ),
        ==,
        0
    );

    g_assert_true (
        atm_repository_ingest_archive_durable (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            &pre_barrier_seal,
            &version,
            &snapshot_path,
            &entries,
            &total_bytes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (pre_barrier_seal);
    g_assert_cmpstr (version, ==, "0.1.0");
    g_assert_cmpuint (entries, ==, 8);
    g_assert_cmpuint (total_bytes, >, 0);
    g_assert_true (
        g_file_test (
            snapshot_path,
            G_FILE_TEST_IS_DIR
        )
    );

    g_assert_true (
        atm_snapshot_seal_compute (
            snapshot_path,
            &final_seal,
            &sealed_files,
            &sealed_bytes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        final_seal,
        ==,
        pre_barrier_seal
    );
    g_assert_cmpuint (sealed_files, ==, 5);
    g_assert_cmpuint (sealed_bytes, >, 0);

    char *staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        test_sha ()
    );
    g_assert_false (
        g_file_test (
            staging,
            G_FILE_TEST_EXISTS
        )
    );

    g_free (staging);
    g_free (final_seal);
    g_free (snapshot_path);
    g_free (version);
    g_free (pre_barrier_seal);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_durable_ingest_requires_existing_data_root (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (
        root,
        "missing-data",
        NULL
    );
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    char *pre_barrier_seal = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    GError *error = NULL;

    write_archive (archive_path, manifest);

    g_assert_false (
        atm_repository_ingest_archive_durable (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            &pre_barrier_seal,
            &version,
            &snapshot_path,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_INGEST_ERROR,
        ATM_INGEST_ERROR_IO
    );
    g_assert_null (pre_barrier_seal);
    g_assert_null (version);
    g_assert_null (snapshot_path);
    g_assert_false (
        g_file_test (
            data_root,
            G_FILE_TEST_EXISTS
        )
    );

    g_clear_error (&error);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_durable_existing_final_is_preserved (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    char *final_path = atm_repository_snapshot_path (
        data_root,
        "ewd",
        test_sha ()
    );
    char *sentinel = g_build_filename (
        final_path,
        "sentinel.txt",
        NULL
    );
    char *pre_barrier_seal = NULL;
    char *version = NULL;
    char *snapshot_path = NULL;
    GError *error = NULL;

    write_archive (archive_path, manifest);
    g_assert_cmpint (
        g_mkdir_with_parents (
            final_path,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            sentinel,
            "keep\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_repository_ingest_archive_durable (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            &pre_barrier_seal,
            &version,
            &snapshot_path,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_EXISTS
    );
    g_assert_null (pre_barrier_seal);
    g_assert_null (version);
    g_assert_null (snapshot_path);
    g_assert_true (
        g_file_test (
            sentinel,
            G_FILE_TEST_EXISTS
        )
    );

    char *staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        test_sha ()
    );
    g_assert_false (
        g_file_test (
            staging,
            G_FILE_TEST_EXISTS
        )
    );

    g_clear_error (&error);
    g_free (staging);
    g_free (sentinel);
    g_free (final_path);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_validation_failure_cleans_staging (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("rmd");
    char *version = NULL;
    char *snapshot_path = NULL;
    GError *error = NULL;

    write_archive (archive_path, manifest);

    g_assert_false (
        atm_repository_ingest_archive (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            &version,
            &snapshot_path,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_nonnull (error);
    g_assert_null (version);
    g_assert_null (snapshot_path);

    char *staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        test_sha ()
    );
    char *final_path = atm_repository_snapshot_path (
        data_root,
        "ewd",
        test_sha ()
    );

    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (final_path, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_free (final_path);
    g_free (staging);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_existing_staging_is_preserved (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    char *staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        test_sha ()
    );
    char *sentinel = g_build_filename (
        staging,
        "sentinel.txt",
        NULL
    );
    char *version = NULL;
    char *snapshot_path = NULL;
    GError *error = NULL;

    write_archive (archive_path, manifest);
    g_assert_cmpint (
        g_mkdir_with_parents (staging, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            sentinel,
            "keep\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_repository_ingest_archive (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            &version,
            &snapshot_path,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_INGEST_ERROR,
        ATM_INGEST_ERROR_STAGING_EXISTS
    );
    g_assert_true (g_file_test (sentinel, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_free (sentinel);
    g_free (staging);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_cancelled_ingest_does_not_create_staging (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    char *version = NULL;
    char *snapshot_path = NULL;
    GCancellable *cancellable = g_cancellable_new ();
    GError *error = NULL;

    write_archive (archive_path, manifest);
    g_cancellable_cancel (cancellable);

    g_assert_false (
        atm_repository_ingest_archive_cancellable (
            data_root,
            archive_path,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            test_sha (),
            cancellable,
            &version,
            &snapshot_path,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_null (version);
    g_assert_null (snapshot_path);

    char *staging = atm_repository_extraction_staging_path (
        data_root,
        "ewd",
        test_sha ()
    );
    char *final_path = atm_repository_snapshot_path (
        data_root,
        "ewd",
        test_sha ()
    );

    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (final_path, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_object_unref (cancellable);
    g_free (final_path);
    g_free (staging);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

typedef struct {
    GMainLoop *loop;
    gboolean success;
    char *version;
    char *snapshot_path;
    guint64 entries;
    guint64 total_bytes;
    GError *error;
} AsyncIngestState;

static void
async_ingest_complete (
    GObject *source_object,
    GAsyncResult *result,
    gpointer user_data
)
{
    AsyncIngestState *state = user_data;

    (void) source_object;

    state->success = atm_repository_ingest_archive_finish (
        result,
        &state->version,
        &state->snapshot_path,
        &state->entries,
        &state->total_bytes,
        &state->error
    );

    g_main_loop_quit (state->loop);
}

static void
test_async_ingest_success (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    AsyncIngestState state = { 0 };

    write_archive (archive_path, manifest);
    state.loop = g_main_loop_new (NULL, FALSE);

    atm_repository_ingest_archive_async (
        data_root,
        archive_path,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        test_sha (),
        NULL,
        async_ingest_complete,
        &state
    );

    g_main_loop_run (state.loop);

    g_assert_true (state.success);
    g_assert_no_error (state.error);
    g_assert_cmpstr (state.version, ==, "0.1.0");
    g_assert_nonnull (state.snapshot_path);
    g_assert_cmpuint (state.entries, ==, 8);
    g_assert_cmpuint (state.total_bytes, >, 0);
    g_assert_true (
        g_file_test (state.snapshot_path, G_FILE_TEST_IS_DIR)
    );

    g_main_loop_unref (state.loop);
    g_free (state.version);
    g_free (state.snapshot_path);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_async_ingest_cancelled (void)
{
    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *manifest = valid_manifest ("ewd");
    GCancellable *cancellable = g_cancellable_new ();
    AsyncIngestState state = { 0 };

    write_archive (archive_path, manifest);
    state.loop = g_main_loop_new (NULL, FALSE);
    g_cancellable_cancel (cancellable);

    atm_repository_ingest_archive_async (
        data_root,
        archive_path,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        test_sha (),
        cancellable,
        async_ingest_complete,
        &state
    );

    g_main_loop_run (state.loop);

    g_assert_false (state.success);
    g_assert_error (
        state.error,
        G_IO_ERROR,
        G_IO_ERROR_CANCELLED
    );
    g_assert_null (state.version);
    g_assert_null (state.snapshot_path);

    char *final_path = atm_repository_snapshot_path (
        data_root,
        "ewd",
        test_sha ()
    );
    g_assert_false (
        g_file_test (final_path, G_FILE_TEST_EXISTS)
    );

    g_free (final_path);
    g_clear_error (&state.error);
    g_main_loop_unref (state.loop);
    g_object_unref (cancellable);
    g_free (manifest);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}


static void
test_builtin_compatibility_overlay_is_sha_bound_and_sealed (void)
{
    const char *historical_sha =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    const char *other_sha =
        "1111111111111111111111111111111111111111";
    char *root = new_temp_root ();
    char *historical_root = g_build_filename (root, historical_sha, NULL);
    char *other_root = g_build_filename (root, other_sha, NULL);
    GError *error = NULL;
    gboolean applied = FALSE;
    char *manifest_path = NULL;
    char *receipt_path = NULL;
    char *manifest = NULL;
    char *receipt = NULL;
    gsize manifest_length = 0;
    gsize receipt_length = 0;
    char *seal_before = NULL;
    char *seal_after = NULL;
    guint64 file_count = 0;
    guint64 total_bytes = 0;
    guint64 ignored_files = 0;
    guint64 ignored_bytes = 0;

    g_assert_cmpint (g_mkdir (historical_root, 0700), ==, 0);
    g_assert_true (atm_repository_apply_builtin_compatibility_overlay (
        historical_root, "ewd", historical_sha, &applied, &error));
    g_assert_no_error (error);
    g_assert_true (applied);

    manifest_path = g_build_filename (
        historical_root, ".atm", "repository.json", NULL);
    receipt_path = g_build_filename (
        historical_root, ".atm", "compatibility-overlay.json", NULL);
    g_assert_true (g_file_get_contents (
        manifest_path, &manifest, &manifest_length, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_get_contents (
        receipt_path, &receipt, &receipt_length, &error));
    g_assert_no_error (error);
    g_assert_nonnull (strstr (manifest, "\"repository_id\": \"ewd\""));
    g_assert_nonnull (strstr (receipt, "atm-ewd-d9e249-manifest-overlay/1"));
    g_assert_nonnull (strstr (receipt, historical_sha));
    g_assert_nonnull (strstr (receipt, "absent from the upstream commit"));
    char *manifest_hash = g_compute_checksum_for_data (
        G_CHECKSUM_SHA256,
        (const guchar *) manifest,
        manifest_length
    );
    g_assert_nonnull (strstr (receipt, manifest_hash));

    g_assert_true (atm_snapshot_seal_compute (
        historical_root, &seal_before, &file_count, &total_bytes, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (file_count, ==, 2);
    g_assert_cmpuint (total_bytes, >, 0);

    /* A second call must not rewrite, replace, or "repair" the overlay. */
    applied = TRUE;
    g_assert_true (atm_repository_apply_builtin_compatibility_overlay (
        historical_root, "ewd", historical_sha, &applied, &error));
    g_assert_no_error (error);
    g_assert_false (applied);
    g_assert_true (atm_snapshot_seal_compute (
        historical_root, &seal_after, &ignored_files, &ignored_bytes, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (seal_before, ==, seal_after);

    g_assert_cmpint (g_mkdir (other_root, 0700), ==, 0);
    applied = TRUE;
    g_assert_true (atm_repository_apply_builtin_compatibility_overlay (
        other_root, "ewd", other_sha, &applied, &error));
    g_assert_no_error (error);
    g_assert_false (applied);
    char *other_atm = g_build_filename (other_root, ".atm", NULL);
    g_assert_false (g_file_test (other_atm, G_FILE_TEST_EXISTS));

    /* The same historical SHA cannot activate the overlay for another repo. */
    applied = TRUE;
    g_assert_true (atm_repository_apply_builtin_compatibility_overlay (
        other_root, "cbd", historical_sha, &applied, &error));
    g_assert_no_error (error);
    g_assert_false (applied);
    g_assert_false (g_file_test (other_atm, G_FILE_TEST_EXISTS));

    /* Existing partial .atm state must fail closed rather than be overlaid. */
    char *partial_parent = g_build_filename (root, "partial", NULL);
    char *partial_root = g_build_filename (
        partial_parent, historical_sha, NULL);
    char *partial_atm = g_build_filename (partial_root, ".atm", NULL);
    g_assert_cmpint (g_mkdir_with_parents (partial_atm, 0700), ==, 0);
    applied = TRUE;
    g_assert_true (atm_repository_apply_builtin_compatibility_overlay (
        partial_root, "ewd", historical_sha, &applied, &error));
    g_assert_no_error (error);
    g_assert_false (applied);
    char *partial_manifest = g_build_filename (
        partial_atm, "repository.json", NULL);
    g_assert_false (g_file_test (partial_manifest, G_FILE_TEST_EXISTS));

    g_free (partial_manifest);
    g_free (partial_atm);
    g_free (partial_root);
    g_free (partial_parent);
    g_free (other_atm);
    g_free (manifest_hash);
    g_free (seal_after);
    g_free (seal_before);
    g_free (receipt);
    g_free (manifest);
    g_free (receipt_path);
    g_free (manifest_path);
    g_free (other_root);
    g_free (historical_root);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_historical_ewd_overlay_is_applied_by_ingest (void)
{
    const char *historical_sha =
        "d9e249339663015f6d1c05752338a955bf64ad0b";
    const FixtureEntry entries[] = {
        { "repo-sha/", NULL, AE_IFDIR },
        { "repo-sha/CITATION.cff",
          "cff-version: 1.2.0\nmessage: cite this\ntype: software\n"
          "title: Empirical World3 Dynamics\nversion: 0.1.0\n",
          AE_IFREG },
        { "repo-sha/STATUS.md", "# Historical EWD status\n", AE_IFREG },
        { "repo-sha/README.md", "# Historical EWD\n", AE_IFREG },
        { "repo-sha/science/", NULL, AE_IFDIR },
        { "repo-sha/science/configs/", NULL, AE_IFDIR },
        { "repo-sha/science/configs/real_model_structure.json", "{}\n", AE_IFREG },
        { "repo-sha/science/data/", NULL, AE_IFDIR },
        { "repo-sha/science/data/registry.csv", "id\n", AE_IFREG },
        { "repo-sha/science/data/processed/", NULL, AE_IFDIR },
        { "repo-sha/science/data/experiments/", NULL, AE_IFDIR },
        { "repo-sha/science/data/forecasts/", NULL, AE_IFDIR },
        { "repo-sha/science/data/raw/", NULL, AE_IFDIR },
        { "repo-sha/science/data/raw/source.csv", "raw source\n", AE_IFREG },
        { "repo-sha/science/vendor/", NULL, AE_IFDIR },
        { "repo-sha/science/vendor/library.py", "# vendored code\n", AE_IFREG },
        { "repo-sha/science/src/", NULL, AE_IFDIR },
        { "repo-sha/science/src/world3_empirical/", NULL, AE_IFDIR },
        { "repo-sha/science/src/world3_empirical/real_model_contract.py", "# fixture\n", AE_IFREG },
        { "repo-sha/science/src/world3_empirical/model.py", "# fixture\n", AE_IFREG },
        { "repo-sha/science/src/world3_empirical/world3_03.py", "# fixture\n", AE_IFREG },
        { "repo-sha/science/scripts/", NULL, AE_IFDIR },
        { "repo-sha/data/", NULL, AE_IFDIR },
        { "repo-sha/data/scenarios/", NULL, AE_IFDIR },
        { "repo-sha/data/scenarios/bau_hybrid_2026_manifest.json", "{}\n", AE_IFREG },
        { "repo-sha/data/scenarios/fit_diagnostics.csv", "id\n", AE_IFREG },
        { "repo-sha/data/scenarios/backtest_2019_latest.csv", "id\n", AE_IFREG },
        { "repo-sha/data/scenarios/backtest_multi_origin.csv", "id\n", AE_IFREG },
        { "repo-sha/data/scenarios/parameter_identifiability.csv", "id\n", AE_IFREG },
        { "repo-sha/data/scenarios/candidate_ranking.csv", "id\n", AE_IFREG },
        { "repo-sha/data/scenarios/validation_candidate_ranking.csv", "id\n", AE_IFREG }
    };

    char *root = new_temp_root ();
    char *data_root = g_build_filename (root, "data", NULL);
    char *archive_path = g_build_filename (
        root, "historical-ewd-without-atm.tar.gz", NULL);
    char *version = NULL;
    char *snapshot_path = NULL;
    char *manifest_path = NULL;
    char *receipt_path = NULL;
    char *manifest = NULL;
    char *receipt = NULL;
    char *seal = NULL;
    guint64 entries_written = 0;
    guint64 total_bytes = 0;
    guint64 sealed_files = 0;
    guint64 sealed_bytes = 0;
    GError *error = NULL;
    gsize manifest_length = 0;
    gsize receipt_length = 0;

    guint64 source_file_entries = 0;
    for (gsize i = 0; i < G_N_ELEMENTS (entries); i++) {
        if (entries[i].filetype == AE_IFREG) {
            source_file_entries++;
        }
    }

    /* This synthetic archive deliberately has no upstream .atm directory.
     * It exercises the production ingest seam; the separate PRES-09 E2E
     * uses the complete upstream tree for the same exact historical pin. */
    write_archive_entries (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );

    g_assert_true (atm_repository_ingest_archive (
        data_root,
        archive_path,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        historical_sha,
        &version,
        &snapshot_path,
        &entries_written,
        &total_bytes,
        &error
    ));
    g_assert_no_error (error);
    g_assert_cmpstr (version, ==, "0.1.0");
    g_assert_nonnull (snapshot_path);
    g_assert_true (g_file_test (snapshot_path, G_FILE_TEST_IS_DIR));
    g_assert_cmpuint (entries_written, ==, G_N_ELEMENTS (entries));
    g_assert_cmpuint (total_bytes, >, 0);

    manifest_path = g_build_filename (
        snapshot_path, ".atm", "repository.json", NULL);
    receipt_path = g_build_filename (
        snapshot_path, ".atm", "compatibility-overlay.json", NULL);
    g_assert_true (g_file_get_contents (
        manifest_path, &manifest, &manifest_length, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_get_contents (
        receipt_path, &receipt, &receipt_length, &error));
    g_assert_no_error (error);
    g_assert_nonnull (strstr (manifest, "\"repository_id\": \"ewd\""));
    g_assert_nonnull (strstr (manifest, "\"science/vendor\""));
    g_assert_nonnull (strstr (manifest, "\"science/data/raw\""));
    g_assert_nonnull (strstr (receipt, "atm-ewd-d9e249-manifest-overlay/1"));
    g_assert_nonnull (strstr (receipt, historical_sha));

    g_assert_true (atm_snapshot_seal_compute (
        snapshot_path, &seal, &sealed_files, &sealed_bytes, &error));
    g_assert_no_error (error);
    /* The local seal contains each source file plus the manifest and receipt. */
    g_assert_cmpuint (sealed_files, ==, source_file_entries + 2);
    g_assert_cmpuint (sealed_bytes, >, total_bytes);

    g_free (seal);
    g_free (receipt);
    g_free (manifest);
    g_free (receipt_path);
    g_free (manifest_path);
    g_free (snapshot_path);
    g_free (version);
    g_free (archive_path);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
}


static void
test_production_limits (void)
{
    g_assert_cmpuint (ATM_INGEST_MAX_ENTRIES, ==, 10000);
    g_assert_cmpuint (
        ATM_INGEST_MAX_FILE_BYTES,
        ==,
        (guint64) 32 * 1024 * 1024
    );
    g_assert_cmpuint (
        ATM_INGEST_MAX_TOTAL_BYTES,
        ==,
        (guint64) 512 * 1024 * 1024
    );
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/ingest/successful",
        test_successful_ingest
    );
    g_test_add_func (
        "/ingest/durable-successful",
        test_durable_ingest_success
    );
    g_test_add_func (
        "/ingest/durable-requires-existing-data-root",
        test_durable_ingest_requires_existing_data_root
    );
    g_test_add_func (
        "/ingest/durable-existing-final-preserved",
        test_durable_existing_final_is_preserved
    );
    g_test_add_func (
        "/ingest/validation-failure-cleans-staging",
        test_validation_failure_cleans_staging
    );
    g_test_add_func (
        "/ingest/existing-staging-preserved",
        test_existing_staging_is_preserved
    );
    g_test_add_func (
        "/ingest/cancelled-before-start",
        test_cancelled_ingest_does_not_create_staging
    );
    g_test_add_func (
        "/ingest/async-success",
        test_async_ingest_success
    );
    g_test_add_func (
        "/ingest/async-cancelled",
        test_async_ingest_cancelled
    );
    g_test_add_func (
        "/ingest/builtin-compatibility-overlay-sha-bound",
        test_builtin_compatibility_overlay_is_sha_bound_and_sealed
    );
    g_test_add_func (
        "/ingest/historical-ewd-overlay-through-ingest",
        test_historical_ewd_overlay_is_applied_by_ingest
    );
    g_test_add_func (
        "/ingest/production-limits",
        test_production_limits
    );

    return g_test_run ();
}
