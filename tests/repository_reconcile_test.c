#include "repository_reconcile.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
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

    GDir *directory = g_dir_open (path, 0, NULL);
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
    char *atm = g_build_filename (root, ".atm", NULL);
    char *model = g_build_filename (root, "model", NULL);

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

    char *citation = g_build_filename (
        root,
        "CITATION.cff",
        NULL
    );
    char *status = g_build_filename (
        root,
        "STATUS.md",
        NULL
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
    char *manifest_path = g_build_filename (
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
    char *manifest = manifest_text (
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
reconcile (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *acronym,
    const char *display_name,
    const char *sha,
    const char *version
)
{
    GError *error = NULL;
    AtmRepositoryReconcileResult *result = NULL;

    g_assert_true (
        atm_repository_reconcile_local (
            cache_root,
            snapshot_root,
            repository_id,
            acronym,
            display_name,
            sha,
            version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (result);
    return result;
}

static void
test_missing_snapshot (void)
{
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *sha =
        "1111111111111111111111111111111111111111";
    char *root = snapshot_path (
        data_root,
        "ewd",
        sha
    );

    AtmRepositoryReconcileResult *result = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    g_assert_cmpint (
        result->status,
        ==,
        ATM_REPOSITORY_RECONCILE_SNAPSHOT_MISSING
    );
    g_assert_cmpstr (
        result->reason_code,
        ==,
        "snapshot_missing"
    );
    g_assert_null (result->index_path);

    atm_repository_reconcile_result_free (result);
    g_free (root);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (cache_root);
    g_free (data_root);
}

static void
test_invalid_snapshot_root (void)
{
    GError *error = NULL;
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *sha =
        "2222222222222222222222222222222222222222";
    char *root = snapshot_path (
        data_root,
        "ewd",
        sha
    );

    char *parent = g_path_get_dirname (root);
    g_assert_cmpint (
        g_mkdir_with_parents (parent, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            root,
            "not a directory",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    AtmRepositoryReconcileResult *result = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    g_assert_cmpint (
        result->status,
        ==,
        ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID
    );
    g_assert_cmpstr (
        result->reason_code,
        ==,
        "snapshot_root_invalid"
    );

    atm_repository_reconcile_result_free (result);
    g_free (parent);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (root);
    g_free (cache_root);
    g_free (data_root);
}

static void
test_rebuild_then_reuse (void)
{
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *sha =
        "3333333333333333333333333333333333333333";
    char *root = create_valid_snapshot (
        data_root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    AtmRepositoryReconcileResult *first = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    g_assert_cmpint (
        first->status,
        ==,
        ATM_REPOSITORY_RECONCILE_READY_REPAIRED_INDEX
    );
    g_assert_cmpstr (
        first->reason_code,
        ==,
        "index_rebuilt"
    );
    g_assert_cmpstr (
        first->repository_version,
        ==,
        "0.1.0"
    );
    g_assert_nonnull (first->index_path);
    char *index_path = g_strdup (first->index_path);
    atm_repository_reconcile_result_free (first);

    AtmRepositoryReconcileResult *second = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    g_assert_cmpint (
        second->status,
        ==,
        ATM_REPOSITORY_RECONCILE_READY
    );
    g_assert_cmpstr (
        second->reason_code,
        ==,
        "ready"
    );
    g_assert_cmpstr (
        second->index_path,
        ==,
        index_path
    );

    atm_repository_reconcile_result_free (second);
    g_free (index_path);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (root);
    g_free (cache_root);
    g_free (data_root);
}

static void
test_corrupt_index_is_rebuilt (void)
{
    GError *error = NULL;
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *sha =
        "4444444444444444444444444444444444444444";
    char *root = create_valid_snapshot (
        data_root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    AtmRepositoryReconcileResult *first = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );
    g_assert_nonnull (first->index_path);
    char *index_path = g_strdup (first->index_path);
    atm_repository_reconcile_result_free (first);

    g_assert_true (
        g_file_set_contents (
            index_path,
            "corrupt",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    AtmRepositoryReconcileResult *repaired = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    g_assert_cmpint (
        repaired->status,
        ==,
        ATM_REPOSITORY_RECONCILE_READY_REPAIRED_INDEX
    );
    g_assert_cmpstr (
        repaired->reason_code,
        ==,
        "index_rebuilt"
    );

    atm_repository_reconcile_result_free (repaired);
    g_free (index_path);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (root);
    g_free (cache_root);
    g_free (data_root);
}

static void
test_version_mismatch_is_not_ready (void)
{
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *sha =
        "5555555555555555555555555555555555555555";
    char *root = create_valid_snapshot (
        data_root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.2.0"
    );

    AtmRepositoryReconcileResult *result = reconcile (
        cache_root,
        root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        sha,
        "0.1.0"
    );

    g_assert_cmpint (
        result->status,
        ==,
        ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID
    );
    g_assert_cmpstr (
        result->reason_code,
        ==,
        "version_mismatch"
    );
    g_assert_cmpstr (
        result->repository_version,
        ==,
        "0.2.0"
    );
    g_assert_null (result->index_path);

    atm_repository_reconcile_result_free (result);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (root);
    g_free (cache_root);
    g_free (data_root);
}

static void
test_manifest_identity_failure_isolated (void)
{
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *ewd_sha =
        "6666666666666666666666666666666666666666";
    const char *cbd_sha =
        "7777777777777777777777777777777777777777";

    char *bad_ewd = create_valid_snapshot (
        data_root,
        "ewd",
        "CBD",
        "Cognitive Belief Dynamics",
        ewd_sha,
        "0.1.0"
    );
    char *good_cbd = create_valid_snapshot (
        data_root,
        "cbd",
        "CBD",
        "Cognitive Belief Dynamics",
        cbd_sha,
        "0.1.0"
    );

    AtmRepositoryReconcileResult *ewd = reconcile (
        cache_root,
        bad_ewd,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        ewd_sha,
        "0.1.0"
    );

    g_assert_cmpint (
        ewd->status,
        ==,
        ATM_REPOSITORY_RECONCILE_SNAPSHOT_INVALID
    );
    g_assert_cmpstr (
        ewd->reason_code,
        ==,
        "manifest_validation_failed"
    );

    AtmRepositoryReconcileResult *cbd = reconcile (
        cache_root,
        good_cbd,
        "cbd",
        "CBD",
        "Cognitive Belief Dynamics",
        cbd_sha,
        "0.1.0"
    );

    g_assert_cmpint (
        cbd->status,
        ==,
        ATM_REPOSITORY_RECONCILE_READY_REPAIRED_INDEX
    );
    g_assert_nonnull (cbd->index_path);

    atm_repository_reconcile_result_free (ewd);
    atm_repository_reconcile_result_free (cbd);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (bad_ewd);
    g_free (good_cbd);
    g_free (cache_root);
    g_free (data_root);
}

static void
test_snapshot_path_must_match_sha (void)
{
    GError *error = NULL;
    char *data_root = new_temp_root (
        "atm-reconcile-data-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-reconcile-cache-XXXXXX"
    );
    const char *actual_sha =
        "8888888888888888888888888888888888888888";
    const char *claimed_sha =
        "9999999999999999999999999999999999999999";
    char *root = create_valid_snapshot (
        data_root,
        "ewd",
        "EWD",
        "Empirical World3 Dynamics",
        actual_sha,
        "0.1.0"
    );
    AtmRepositoryReconcileResult *result = NULL;

    g_assert_false (
        atm_repository_reconcile_local (
            cache_root,
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            claimed_sha,
            "0.1.0",
            &result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_REPOSITORY_RECONCILE_ERROR,
        ATM_REPOSITORY_RECONCILE_ERROR_ARGUMENT
    );
    g_assert_null (result);

    g_clear_error (&error);
    remove_tree_best_effort (cache_root);
    remove_tree_best_effort (data_root);
    g_free (root);
    g_free (cache_root);
    g_free (data_root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/reconcile/missing-snapshot",
        test_missing_snapshot
    );
    g_test_add_func (
        "/reconcile/invalid-root",
        test_invalid_snapshot_root
    );
    g_test_add_func (
        "/reconcile/rebuild-then-reuse",
        test_rebuild_then_reuse
    );
    g_test_add_func (
        "/reconcile/corrupt-index-rebuilt",
        test_corrupt_index_is_rebuilt
    );
    g_test_add_func (
        "/reconcile/version-mismatch",
        test_version_mismatch_is_not_ready
    );
    g_test_add_func (
        "/reconcile/failure-isolation",
        test_manifest_identity_failure_isolated
    );
    g_test_add_func (
        "/reconcile/path-sha-binding",
        test_snapshot_path_must_match_sha
    );

    return g_test_run ();
}
