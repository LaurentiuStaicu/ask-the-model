#include "retrieval_index.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

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
new_cache_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-retrieval-index-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static AtmRetrievalIndexMetadata
valid_metadata (void)
{
    AtmRetrievalIndexMetadata metadata = {
        .repository_id = "ewd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .manifest_schema_version = 1,
        .manifest_sha256 =
            "0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef",
        .created_at_utc = "2026-09-20T00:00:00Z"
    };

    return metadata;
}

static void
test_create_validate_and_refuse_overwrite (void)
{
    char *root = new_cache_root ();
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (index_path);
    g_assert_true (
        g_file_test (index_path, G_FILE_TEST_IS_REGULAR)
    );

    char *expected = atm_retrieval_index_path (
        root,
        metadata.repository_id,
        metadata.snapshot_sha
    );
    char *staging = atm_retrieval_index_staging_path (
        root,
        metadata.repository_id,
        metadata.snapshot_sha
    );

    g_assert_cmpstr (index_path, ==, expected);
    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));

    g_assert_true (
        atm_retrieval_index_validate_identity (
            index_path,
            metadata.repository_id,
            metadata.snapshot_sha,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_retrieval_index_validate_identity (
            index_path,
            metadata.repository_id,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY
    );
    g_clear_error (&error);

    char *second_path = NULL;
    g_assert_false (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &second_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_EXISTS
    );
    g_assert_null (second_path);

    g_clear_error (&error);
    g_free (staging);
    g_free (expected);
    g_free (index_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_identity_is_rejected_before_creation (void)
{
    char *root = new_cache_root ();
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    metadata.repository_id = "other";

    g_assert_false (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INVALID_ID
    );
    g_assert_null (index_path);

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_manifest_hash_is_rejected (void)
{
    char *root = new_cache_root ();
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    metadata.manifest_sha256 = "not-a-hash";

    g_assert_false (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INVALID_MANIFEST_HASH
    );
    g_assert_null (index_path);

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-index/create-validate",
        test_create_validate_and_refuse_overwrite
    );
    g_test_add_func (
        "/retrieval-index/invalid-id",
        test_invalid_identity_is_rejected_before_creation
    );
    g_test_add_func (
        "/retrieval-index/invalid-manifest-hash",
        test_invalid_manifest_hash_is_rejected
    );

    return g_test_run ();
}
