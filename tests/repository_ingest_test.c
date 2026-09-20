#include "repository_ingest.h"
#include "repository_storage.h"

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

    for (gsize i = 0; i < G_N_ELEMENTS (entries); i++) {
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
        "/ingest/production-limits",
        test_production_limits
    );

    return g_test_run ();
}
