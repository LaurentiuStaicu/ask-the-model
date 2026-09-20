#include "repository_ingest.h"
#include "archive_extract.h"
#include "repository_manifest.h"
#include "repository_storage.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *path;
    mode_t filetype;
    const char *content;
    const char *symlink_target;
} FixtureEntry;

static const char *
valid_sha (void)
{
    return "0123456789abcdef0123456789abcdef01234567";
}

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
new_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-ingest-pipeline-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_archive (
    const char *archive_path,
    const FixtureEntry *entries,
    gsize count
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

    for (gsize i = 0; i < count; i++) {
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

        if (fixture->symlink_target != NULL) {
            archive_entry_set_symlink (
                entry,
                fixture->symlink_target
            );
        }

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

    g_assert_cmpint (archive_write_close (writer), ==, ARCHIVE_OK);
    g_assert_cmpint (archive_write_free (writer), ==, ARCHIVE_OK);
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

static void
write_valid_fixture_archive (
    const char *archive_path,
    const char *repository_id
)
{
    char *manifest = valid_manifest (repository_id);
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL },
        { "repo-sha/.atm/", AE_IFDIR, NULL, NULL },
        { "repo-sha/.atm/repository.json", AE_IFREG, manifest, NULL },
        { "repo-sha/CITATION.cff", AE_IFREG,
          "cff-version: 1.2.0\n"
          "message: cite this\n"
          "type: software\n"
          "title: Test\n"
          "version: 0.1.0\n", NULL },
        { "repo-sha/STATUS.md", AE_IFREG, "# Status\n", NULL },
        { "repo-sha/README.md", AE_IFREG, "# Test\n", NULL },
        { "repo-sha/model/", AE_IFDIR, NULL, NULL },
        { "repo-sha/model/core.json", AE_IFREG, "{}\n", NULL }
    };

    write_archive (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );
    g_free (manifest);
}

static void
test_limits_are_bounded (void)
{
    g_assert_cmpuint (
        ATM_REPOSITORY_MAX_ARCHIVE_ENTRIES,
        ==,
        10000
    );
    g_assert_cmpuint (
        ATM_REPOSITORY_MAX_FILE_BYTES,
        ==,
        32 * 1024 * 1024
    );
    g_assert_cmpuint (
        ATM_REPOSITORY_MAX_TOTAL_BYTES,
        ==,
        512 * 1024 * 1024
    );
}

static void
test_valid_archive_promotes (void)
{
    char *root = new_root ();
    char *archive_path = g_build_filename (
        root,
        "repository.tar.gz",
        NULL
    );
    char *version = NULL;
    char *snapshot = NULL;
    GError *error = NULL;

    write_valid_fixture_archive (archive_path, "ewd");

    g_assert_true (
        atm_repository_ingest_archive (
            archive_path,
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            valid_sha (),
            &version,
            &snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (version, ==, "0.1.0");
    g_assert_true (g_file_test (snapshot, G_FILE_TEST_IS_DIR));

    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *status = g_build_filename (
        snapshot,
        "STATUS.md",
        NULL
    );

    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (status, G_FILE_TEST_IS_REGULAR));

    g_free (status);
    g_free (staging);
    g_free (snapshot);
    g_free (version);
    g_free (archive_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_manifest_leaves_no_candidate (void)
{
    char *root = new_root ();
    char *archive_path = g_build_filename (
        root,
        "repository.tar.gz",
        NULL
    );
    char *version = NULL;
    char *snapshot = NULL;
    GError *error = NULL;

    write_valid_fixture_archive (archive_path, "rmd");

    g_assert_false (
        atm_repository_ingest_archive (
            archive_path,
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            valid_sha (),
            &version,
            &snapshot,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_IDENTITY
    );
    g_assert_null (version);
    g_assert_null (snapshot);

    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *final = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );

    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (final, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_free (staging);
    g_free (final);
    g_free (archive_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_unsafe_archive_leaves_no_candidate (void)
{
    char *root = new_root ();
    char *archive_path = g_build_filename (
        root,
        "repository.tar.gz",
        NULL
    );
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL },
        { "repo-sha/../escape", AE_IFREG, "bad", NULL }
    };
    char *version = NULL;
    char *snapshot = NULL;
    GError *error = NULL;

    write_archive (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );

    g_assert_false (
        atm_repository_ingest_archive (
            archive_path,
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            valid_sha (),
            &version,
            &snapshot,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_ARCHIVE_ERROR,
        ATM_ARCHIVE_ERROR_UNSAFE_PATH
    );

    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *final = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );

    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (final, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_free (staging);
    g_free (final);
    g_free (archive_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_stale_exact_staging_is_replaced (void)
{
    char *root = new_root ();
    char *archive_path = g_build_filename (
        root,
        "repository.tar.gz",
        NULL
    );
    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *stale_file = g_build_filename (
        staging,
        "stale.txt",
        NULL
    );
    char *version = NULL;
    char *snapshot = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (staging, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            stale_file,
            "stale\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    write_valid_fixture_archive (archive_path, "ewd");

    g_assert_true (
        atm_repository_ingest_archive (
            archive_path,
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            valid_sha (),
            &version,
            &snapshot,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (g_file_test (stale_file, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (snapshot, G_FILE_TEST_IS_DIR));

    g_free (snapshot);
    g_free (version);
    g_free (stale_file);
    g_free (staging);
    g_free (archive_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_existing_snapshot_survives_failed_replacement (void)
{
    char *root = new_root ();
    char *archive_path = g_build_filename (
        root,
        "repository.tar.gz",
        NULL
    );
    char *final = atm_repository_snapshot_path (
        root,
        "ewd",
        valid_sha ()
    );
    char *sentinel = g_build_filename (
        final,
        "sentinel.txt",
        NULL
    );
    char *version = NULL;
    char *snapshot = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (final, 0700),
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

    write_valid_fixture_archive (archive_path, "ewd");

    g_assert_false (
        atm_repository_ingest_archive (
            archive_path,
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            valid_sha (),
            &version,
            &snapshot,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STORAGE_ERROR,
        ATM_STORAGE_ERROR_EXISTS
    );
    g_assert_true (g_file_test (sentinel, G_FILE_TEST_IS_REGULAR));

    char *staging = atm_repository_extraction_staging_path (
        root,
        "ewd",
        valid_sha ()
    );
    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    g_free (staging);
    g_free (sentinel);
    g_free (final);
    g_free (archive_path);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/ingest/limits-are-bounded",
        test_limits_are_bounded
    );
    g_test_add_func (
        "/ingest/valid-archive-promotes",
        test_valid_archive_promotes
    );
    g_test_add_func (
        "/ingest/invalid-manifest-no-candidate",
        test_invalid_manifest_leaves_no_candidate
    );
    g_test_add_func (
        "/ingest/unsafe-archive-no-candidate",
        test_unsafe_archive_leaves_no_candidate
    );
    g_test_add_func (
        "/ingest/stale-staging-replaced",
        test_stale_exact_staging_is_replaced
    );
    g_test_add_func (
        "/ingest/existing-snapshot-survives",
        test_existing_snapshot_survives_failed_replacement
    );

    return g_test_run ();
}
