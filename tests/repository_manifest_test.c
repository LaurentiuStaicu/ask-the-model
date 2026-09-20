#include "repository_manifest.h"

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
new_snapshot_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-manifest-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);

    char *atm = g_build_filename (root, ".atm", NULL);
    char *model = g_build_filename (root, "model", NULL);

    g_assert_cmpint (g_mkdir_with_parents (atm, 0700), ==, 0);
    g_assert_cmpint (g_mkdir_with_parents (model, 0700), ==, 0);

    char *citation = g_build_filename (root, "CITATION.cff", NULL);
    char *status = g_build_filename (root, "STATUS.md", NULL);
    char *readme = g_build_filename (root, "README.md", NULL);
    char *core = g_build_filename (root, "model", "core.json", NULL);

    g_assert_true (
        g_file_set_contents (
            citation,
            "cff-version: 1.2.0\n"
            "message: cite this\n"
            "type: software\n"
            "title: Test\n"
            "version: 0.1.0\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (status, "# Status\n", -1, &error)
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (readme, "# Test\n", -1, &error)
    );
    g_assert_no_error (error);

    g_assert_true (
        g_file_set_contents (core, "{}\n", -1, &error)
    );
    g_assert_no_error (error);

    g_free (citation);
    g_free (status);
    g_free (readme);
    g_free (core);
    g_free (atm);
    g_free (model);

    return root;
}

static char *
make_manifest (
    const char *repository_id,
    const char *required_path,
    const char *structural_path,
    const char *extra_top_level
)
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
        "\"CITATION.cff\", \"STATUS.md\", \"%s\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": ["
        "\"STATUS.md\", \"README.md\", \"CITATION.cff\"],\n"
        "    \"structural\": [\"%s\"],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": [\".github\", \"__pycache__\"]\n"
        "  }%s\n"
        "}\n",
        repository_id,
        required_path,
        structural_path,
        extra_top_level
    );
}

static void
write_manifest (const char *root, const char *manifest)
{
    GError *error = NULL;
    char *path = g_build_filename (
        root,
        ".atm",
        "repository.json",
        NULL
    );

    g_assert_true (
        g_file_set_contents (path, manifest, -1, &error)
    );
    g_assert_no_error (error);
    g_free (path);
}

static void
test_valid_snapshot (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "ewd",
        "model/core.json",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_true (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (version, ==, "0.1.0");

    g_free (version);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_wrong_identity_rejected (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "rmd",
        "model/core.json",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_IDENTITY
    );
    g_assert_null (version);

    g_clear_error (&error);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_extra_member_rejected (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "ewd",
        "model/core.json",
        "model",
        ",\n  \"unexpected\": true"
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_SHAPE
    );

    g_clear_error (&error);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_unsafe_path_rejected (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "ewd",
        "../escape",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_PATH
    );

    g_clear_error (&error);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_glob_path_rejected (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "ewd",
        "model/*.json",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_PATH
    );

    g_clear_error (&error);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_missing_required_path_rejected (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "ewd",
        "model/missing.json",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_MISSING_PATH
    );

    g_clear_error (&error);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_missing_retrieval_path_rejected (void)
{
    char *root = new_snapshot_root ();
    char *manifest = make_manifest (
        "ewd",
        "model/core.json",
        "model/missing",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_MISSING_PATH
    );

    g_clear_error (&error);
    g_free (manifest);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_symlink_required_path_rejected (void)
{
    char *root = new_snapshot_root ();
    char *link_path = g_build_filename (
        root,
        "model",
        "link.json",
        NULL
    );
    char *manifest = make_manifest (
        "ewd",
        "model/link.json",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        symlink ("/tmp", link_path),
        ==,
        0
    );
    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_MISSING_PATH
    );

    g_clear_error (&error);
    g_free (manifest);
    g_free (link_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_missing_top_level_cff_version_rejected (void)
{
    char *root = new_snapshot_root ();
    char *citation = g_build_filename (
        root,
        "CITATION.cff",
        NULL
    );
    char *manifest = make_manifest (
        "ewd",
        "model/core.json",
        "model",
        ""
    );
    char *version = NULL;
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            citation,
            "cff-version: 1.2.0\n"
            "message: cite this\n"
            "type: software\n"
            "title: Test\n"
            "references:\n"
            "  - type: software\n"
            "    title: Nested\n"
            "    version: 9.9.9\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    write_manifest (root, manifest);

    g_assert_false (
        atm_repository_validate_snapshot (
            root,
            "ewd",
            "EWD",
            "Empirical World3 Dynamics",
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MANIFEST_ERROR,
        ATM_MANIFEST_ERROR_VERSION
    );

    g_clear_error (&error);
    g_free (manifest);
    g_free (citation);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/manifest/valid-snapshot",
        test_valid_snapshot
    );
    g_test_add_func (
        "/manifest/wrong-identity-rejected",
        test_wrong_identity_rejected
    );
    g_test_add_func (
        "/manifest/extra-member-rejected",
        test_extra_member_rejected
    );
    g_test_add_func (
        "/manifest/unsafe-path-rejected",
        test_unsafe_path_rejected
    );
    g_test_add_func (
        "/manifest/glob-path-rejected",
        test_glob_path_rejected
    );
    g_test_add_func (
        "/manifest/missing-required-path-rejected",
        test_missing_required_path_rejected
    );
    g_test_add_func (
        "/manifest/missing-retrieval-path-rejected",
        test_missing_retrieval_path_rejected
    );
    g_test_add_func (
        "/manifest/symlink-required-path-rejected",
        test_symlink_required_path_rejected
    );
    g_test_add_func (
        "/manifest/missing-top-level-cff-version-rejected",
        test_missing_top_level_cff_version_rejected
    );

    return g_test_run ();
}
