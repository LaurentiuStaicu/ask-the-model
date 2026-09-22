#include "snapshot_seal.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GDir *directory = g_dir_open (
        path,
        0,
        NULL
    );

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );
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
    char *root = g_dir_make_tmp (
        pattern,
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_text (
    const char *path,
    const char *contents
)
{
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);
}

static char *
create_snapshot (
    const char *parent,
    const char *sha,
    gboolean reverse_creation_order
)
{
    char *root = g_build_filename (
        parent,
        sha,
        NULL
    );
    char *model = g_build_filename (
        root,
        "model",
        NULL
    );
    char *atm = g_build_filename (
        root,
        ".atm",
        NULL
    );

    g_assert_cmpint (
        g_mkdir_with_parents (model, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        g_mkdir_with_parents (atm, 0700),
        ==,
        0
    );

    char *readme = g_build_filename (
        root,
        "README.md",
        NULL
    );
    char *core = g_build_filename (
        root,
        "model",
        "core.json",
        NULL
    );
    char *manifest = g_build_filename (
        root,
        ".atm",
        "repository.json",
        NULL
    );

    if (reverse_creation_order) {
        write_text (
            manifest,
            "{\"schema_version\":1}\n"
        );
        write_text (
            core,
            "{\"value\":42}\n"
        );
        write_text (
            readme,
            "# Snapshot\nEvidence.\n"
        );
    } else {
        write_text (
            readme,
            "# Snapshot\nEvidence.\n"
        );
        write_text (
            core,
            "{\"value\":42}\n"
        );
        write_text (
            manifest,
            "{\"schema_version\":1}\n"
        );
    }

    g_free (manifest);
    g_free (core);
    g_free (readme);
    g_free (atm);
    g_free (model);

    return root;
}

static char *
seal_path (
    const char *seal_root,
    const char *repository_id,
    const char *sha
)
{
    char *filename = g_strdup_printf (
        "%s.json",
        sha
    );
    char *path = g_build_filename (
        seal_root,
        repository_id,
        filename,
        NULL
    );

    g_free (filename);
    return path;
}

static void
assert_check_status (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *sha,
    AtmSnapshotSealStatus expected
)
{
    GError *error = NULL;
    AtmSnapshotSealStatus status;
    char *detail = NULL;
    char *root_sha = NULL;

    g_assert_true (
        atm_snapshot_seal_check (
            seal_root,
            snapshot_root,
            repository_id,
            sha,
            &status,
            &detail,
            &root_sha,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (status, ==, expected);
    g_assert_nonnull (detail);

    if (expected == ATM_SNAPSHOT_SEAL_VALID ||
        expected == ATM_SNAPSHOT_SEAL_MISMATCH) {
        if (expected == ATM_SNAPSHOT_SEAL_VALID) {
            g_assert_nonnull (root_sha);
        }
    }

    g_free (root_sha);
    g_free (detail);
}

static void
test_absent_create_validate_and_refuse_overwrite (void)
{
    GError *error = NULL;
    const char *sha =
        "1111111111111111111111111111111111111111";
    char *snapshot_parent = new_temp_root (
        "atm-seal-snapshot-XXXXXX"
    );
    char *snapshot_root = create_snapshot (
        snapshot_parent,
        sha,
        FALSE
    );
    char *seal_root = new_temp_root (
        "atm-seal-state-XXXXXX"
    );

    assert_check_status (
        seal_root,
        snapshot_root,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_ABSENT
    );

    char *created_path = NULL;
    char *root_sha = NULL;

    g_assert_true (
        atm_snapshot_seal_create (
            seal_root,
            snapshot_root,
            "ewd",
            sha,
            "ingest",
            &created_path,
            &root_sha,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (created_path);
    g_assert_nonnull (root_sha);
    g_assert_cmpuint (strlen (root_sha), ==, 64);

    assert_check_status (
        seal_root,
        snapshot_root,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_VALID
    );

    char *before = NULL;
    gsize before_length = 0;
    g_assert_true (
        g_file_get_contents (
            created_path,
            &before,
            &before_length,
            &error
        )
    );
    g_assert_no_error (error);

    char *second_path = NULL;
    char *second_sha = NULL;

    g_assert_false (
        atm_snapshot_seal_create (
            seal_root,
            snapshot_root,
            "ewd",
            sha,
            "migration",
            &second_path,
            &second_sha,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SNAPSHOT_SEAL_ERROR,
        ATM_SNAPSHOT_SEAL_ERROR_EXISTS
    );
    g_assert_null (second_path);
    g_assert_null (second_sha);
    g_clear_error (&error);

    char *after = NULL;
    gsize after_length = 0;
    g_assert_true (
        g_file_get_contents (
            created_path,
            &after,
            &after_length,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        before_length,
        ==,
        after_length
    );
    g_assert_cmpmem (
        before,
        before_length,
        after,
        after_length
    );

    g_free (after);
    g_free (before);
    g_free (root_sha);
    g_free (created_path);
    remove_tree_best_effort (seal_root);
    remove_tree_best_effort (snapshot_parent);
    g_free (seal_root);
    g_free (snapshot_root);
    g_free (snapshot_parent);
}

static void
test_deterministic_digest_ignores_creation_order (void)
{
    GError *error = NULL;
    const char *sha =
        "2222222222222222222222222222222222222222";
    char *parent_a = new_temp_root (
        "atm-seal-order-a-XXXXXX"
    );
    char *parent_b = new_temp_root (
        "atm-seal-order-b-XXXXXX"
    );
    char *snapshot_a = create_snapshot (
        parent_a,
        sha,
        FALSE
    );
    char *snapshot_b = create_snapshot (
        parent_b,
        sha,
        TRUE
    );
    char *seal_a = new_temp_root (
        "atm-seal-state-a-XXXXXX"
    );
    char *seal_b = new_temp_root (
        "atm-seal-state-b-XXXXXX"
    );
    char *path_a = NULL;
    char *path_b = NULL;
    char *digest_a = NULL;
    char *digest_b = NULL;

    g_assert_true (
        atm_snapshot_seal_create (
            seal_a,
            snapshot_a,
            "ewd",
            sha,
            "ingest",
            &path_a,
            &digest_a,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_snapshot_seal_create (
            seal_b,
            snapshot_b,
            "ewd",
            sha,
            "ingest",
            &path_b,
            &digest_b,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        digest_a,
        ==,
        digest_b
    );

    g_free (digest_b);
    g_free (digest_a);
    g_free (path_b);
    g_free (path_a);
    remove_tree_best_effort (seal_b);
    remove_tree_best_effort (seal_a);
    remove_tree_best_effort (parent_b);
    remove_tree_best_effort (parent_a);
    g_free (seal_b);
    g_free (seal_a);
    g_free (snapshot_b);
    g_free (snapshot_a);
    g_free (parent_b);
    g_free (parent_a);
}

static void
test_modified_file_is_mismatch (void)
{
    GError *error = NULL;
    const char *sha =
        "3333333333333333333333333333333333333333";
    char *parent = new_temp_root (
        "atm-seal-modified-XXXXXX"
    );
    char *snapshot = create_snapshot (
        parent,
        sha,
        FALSE
    );
    char *seal_root = new_temp_root (
        "atm-seal-modified-state-XXXXXX"
    );
    char *path = NULL;
    char *digest = NULL;

    g_assert_true (
        atm_snapshot_seal_create (
            seal_root,
            snapshot,
            "ewd",
            sha,
            "ingest",
            &path,
            &digest,
            &error
        )
    );
    g_assert_no_error (error);

    char *readme = g_build_filename (
        snapshot,
        "README.md",
        NULL
    );
    write_text (
        readme,
        "# Snapshot\nModified.\n"
    );

    assert_check_status (
        seal_root,
        snapshot,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_MISMATCH
    );

    g_free (readme);
    g_free (digest);
    g_free (path);
    remove_tree_best_effort (seal_root);
    remove_tree_best_effort (parent);
    g_free (seal_root);
    g_free (snapshot);
    g_free (parent);
}

static void
test_added_and_deleted_files_are_mismatch (void)
{
    GError *error = NULL;
    const char *sha =
        "4444444444444444444444444444444444444444";
    char *parent = new_temp_root (
        "atm-seal-file-set-XXXXXX"
    );
    char *snapshot = create_snapshot (
        parent,
        sha,
        FALSE
    );
    char *seal_root = new_temp_root (
        "atm-seal-file-set-state-XXXXXX"
    );
    char *path = NULL;
    char *digest = NULL;

    g_assert_true (
        atm_snapshot_seal_create (
            seal_root,
            snapshot,
            "ewd",
            sha,
            "ingest",
            &path,
            &digest,
            &error
        )
    );
    g_assert_no_error (error);

    char *extra = g_build_filename (
        snapshot,
        "extra.txt",
        NULL
    );
    write_text (extra, "extra\n");

    assert_check_status (
        seal_root,
        snapshot,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_MISMATCH
    );

    g_remove (extra);

    char *readme = g_build_filename (
        snapshot,
        "README.md",
        NULL
    );
    g_remove (readme);

    assert_check_status (
        seal_root,
        snapshot,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_MISMATCH
    );

    g_free (readme);
    g_free (extra);
    g_free (digest);
    g_free (path);
    remove_tree_best_effort (seal_root);
    remove_tree_best_effort (parent);
    g_free (seal_root);
    g_free (snapshot);
    g_free (parent);
}

static void
test_symlink_is_mismatch (void)
{
    GError *error = NULL;
    const char *sha =
        "5555555555555555555555555555555555555555";
    char *parent = new_temp_root (
        "atm-seal-symlink-XXXXXX"
    );
    char *snapshot = create_snapshot (
        parent,
        sha,
        FALSE
    );
    char *seal_root = new_temp_root (
        "atm-seal-symlink-state-XXXXXX"
    );
    char *path = NULL;
    char *digest = NULL;

    g_assert_true (
        atm_snapshot_seal_create (
            seal_root,
            snapshot,
            "ewd",
            sha,
            "ingest",
            &path,
            &digest,
            &error
        )
    );
    g_assert_no_error (error);

    char *readme = g_build_filename (
        snapshot,
        "README.md",
        NULL
    );
    g_assert_cmpint (g_remove (readme), ==, 0);
    g_assert_cmpint (
        symlink (
            "model/core.json",
            readme
        ),
        ==,
        0
    );

    assert_check_status (
        seal_root,
        snapshot,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_MISMATCH
    );

    g_free (readme);
    g_free (digest);
    g_free (path);
    remove_tree_best_effort (seal_root);
    remove_tree_best_effort (parent);
    g_free (seal_root);
    g_free (snapshot);
    g_free (parent);
}

static void
test_corrupt_seal_is_invalid (void)
{
    GError *error = NULL;
    const char *sha =
        "6666666666666666666666666666666666666666";
    char *parent = new_temp_root (
        "atm-seal-corrupt-XXXXXX"
    );
    char *snapshot = create_snapshot (
        parent,
        sha,
        FALSE
    );
    char *seal_root = new_temp_root (
        "atm-seal-corrupt-state-XXXXXX"
    );
    char *path = NULL;
    char *digest = NULL;

    g_assert_true (
        atm_snapshot_seal_create (
            seal_root,
            snapshot,
            "ewd",
            sha,
            "ingest",
            &path,
            &digest,
            &error
        )
    );
    g_assert_no_error (error);

    write_text (
        path,
        "{ this is not json"
    );

    assert_check_status (
        seal_root,
        snapshot,
        "ewd",
        sha,
        ATM_SNAPSHOT_SEAL_INVALID
    );

    g_free (digest);
    g_free (path);
    remove_tree_best_effort (seal_root);
    remove_tree_best_effort (parent);
    g_free (seal_root);
    g_free (snapshot);
    g_free (parent);
}

static void
test_snapshot_path_must_match_sha (void)
{
    GError *error = NULL;
    const char *actual_sha =
        "7777777777777777777777777777777777777777";
    const char *claimed_sha =
        "8888888888888888888888888888888888888888";
    char *parent = new_temp_root (
        "atm-seal-path-binding-XXXXXX"
    );
    char *snapshot = create_snapshot (
        parent,
        actual_sha,
        FALSE
    );
    char *seal_root = new_temp_root (
        "atm-seal-path-binding-state-XXXXXX"
    );
    AtmSnapshotSealStatus status;
    char *detail = NULL;
    char *digest = NULL;

    g_assert_false (
        atm_snapshot_seal_check (
            seal_root,
            snapshot,
            "ewd",
            claimed_sha,
            &status,
            &detail,
            &digest,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SNAPSHOT_SEAL_ERROR,
        ATM_SNAPSHOT_SEAL_ERROR_ARGUMENT
    );
    g_assert_null (detail);
    g_assert_null (digest);

    g_clear_error (&error);
    remove_tree_best_effort (seal_root);
    remove_tree_best_effort (parent);
    g_free (seal_root);
    g_free (snapshot);
    g_free (parent);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/snapshot-seal/create-validate-no-overwrite",
        test_absent_create_validate_and_refuse_overwrite
    );
    g_test_add_func (
        "/snapshot-seal/deterministic-order",
        test_deterministic_digest_ignores_creation_order
    );
    g_test_add_func (
        "/snapshot-seal/modified-file",
        test_modified_file_is_mismatch
    );
    g_test_add_func (
        "/snapshot-seal/file-set-change",
        test_added_and_deleted_files_are_mismatch
    );
    g_test_add_func (
        "/snapshot-seal/symlink",
        test_symlink_is_mismatch
    );
    g_test_add_func (
        "/snapshot-seal/corrupt-seal",
        test_corrupt_seal_is_invalid
    );
    g_test_add_func (
        "/snapshot-seal/path-sha-binding",
        test_snapshot_path_must_match_sha
    );

    return g_test_run ();
}
