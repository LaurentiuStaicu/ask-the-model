#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include "json_pointer.h"
#include "scientific_control.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TEST_SHA =
    "0123456789abcdef0123456789abcdef01234567";

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0) {
        return;
    }

    if (!S_ISDIR (st.st_mode) ||
        S_ISLNK (st.st_mode)) {
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

static char *
new_snapshot_root (
    const char *base,
    const char *repository_id,
    const char *sha
)
{
    char *root = g_build_filename (
        base,
        "Repositories",
        repository_id,
        "snapshots",
        sha,
        NULL
    );

    g_assert_cmpint (
        g_mkdir_with_parents (
            root,
            0700
        ),
        ==,
        0
    );

    return root;
}

static char *
write_core_contract (
    const char *root,
    const char *json
)
{
    char *directory = g_build_filename (
        root,
        "model",
        "dynamics",
        NULL
    );
    char *path = g_build_filename (
        directory,
        "core_contract.json",
        NULL
    );
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            directory,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            path,
            json,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (directory);
    return path;
}

static void
test_rmd_behavioural_closure_false (void)
{
    char *base = new_temp_root (
        "atm-control-rmd-XXXXXX"
    );
    char *root = new_snapshot_root (
        base,
        "rmd",
        TEST_SHA
    );
    char *path = write_core_contract (
        root,
        "{\"behavioural_closure\":{\"active\":false}}"
    );
    AtmScientificControlValue *value = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_scientific_control_read (
            root,
            "rmd",
            TEST_SHA,
            ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
            &value,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->repository_id, ==, "rmd");
    g_assert_cmpstr (value->snapshot_sha, ==, TEST_SHA);
    g_assert_cmpstr (
        value->source_path,
        ==,
        "model/dynamics/core_contract.json"
    );
    g_assert_cmpstr (
        value->json_pointer,
        ==,
        "/behavioural_closure/active"
    );
    g_assert_cmpint (
        value->value_type,
        ==,
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN
    );
    g_assert_false (value->boolean_value);

    atm_scientific_control_value_free (value);
    g_free (path);
    g_free (root);
    remove_tree_best_effort (base);
    g_free (base);
}

static void
test_repository_mismatch_fails (void)
{
    char *base = new_temp_root (
        "atm-control-repo-XXXXXX"
    );
    char *root = new_snapshot_root (
        base,
        "rmd",
        TEST_SHA
    );
    char *path = write_core_contract (
        root,
        "{\"behavioural_closure\":{\"active\":false}}"
    );
    AtmScientificControlValue *value = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_control_read (
            root,
            "ewd",
            TEST_SHA,
            ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
            &value,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CONTROL_ERROR,
        ATM_SCIENTIFIC_CONTROL_ERROR_REPOSITORY
    );
    g_assert_null (value);
    g_clear_error (&error);

    g_free (path);
    g_free (root);
    remove_tree_best_effort (base);
    g_free (base);
}

static void
test_wrong_value_type_fails_closed (void)
{
    char *base = new_temp_root (
        "atm-control-type-XXXXXX"
    );
    char *root = new_snapshot_root (
        base,
        "rmd",
        TEST_SHA
    );
    char *path = write_core_contract (
        root,
        "{\"behavioural_closure\":{\"active\":\"false\"}}"
    );
    AtmScientificControlValue *value = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_control_read (
            root,
            "rmd",
            TEST_SHA,
            ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
            &value,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CONTROL_ERROR,
        ATM_SCIENTIFIC_CONTROL_ERROR_TYPE
    );
    g_assert_null (value);
    g_clear_error (&error);

    g_free (path);
    g_free (root);
    remove_tree_best_effort (base);
    g_free (base);
}

static void
test_missing_pointer_fails_closed (void)
{
    char *base = new_temp_root (
        "atm-control-pointer-XXXXXX"
    );
    char *root = new_snapshot_root (
        base,
        "rmd",
        TEST_SHA
    );
    char *path = write_core_contract (
        root,
        "{\"behavioural_closure\":{}}"
    );
    AtmScientificControlValue *value = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_scientific_control_read (
            root,
            "rmd",
            TEST_SHA,
            ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
            &value,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CONTROL_ERROR,
        ATM_SCIENTIFIC_CONTROL_ERROR_POINTER
    );
    g_assert_null (value);
    g_clear_error (&error);

    g_free (path);
    g_free (root);
    remove_tree_best_effort (base);
    g_free (base);
}

static void
test_final_symlink_is_rejected (void)
{
    char *base = new_temp_root (
        "atm-control-symlink-XXXXXX"
    );
    char *root = new_snapshot_root (
        base,
        "rmd",
        TEST_SHA
    );
    char *external = new_temp_root (
        "atm-control-external-XXXXXX"
    );
    char *external_file = g_build_filename (
        external,
        "outside.json",
        NULL
    );
    char *directory = g_build_filename (
        root,
        "model",
        "dynamics",
        NULL
    );
    char *link_path = g_build_filename (
        directory,
        "core_contract.json",
        NULL
    );
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            external_file,
            "{\"behavioural_closure\":{\"active\":true}}",
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        g_mkdir_with_parents (
            directory,
            0700
        ),
        ==,
        0
    );
    g_assert_cmpint (
        symlink (
            external_file,
            link_path
        ),
        ==,
        0
    );

    AtmScientificControlValue *value = NULL;

    g_assert_false (
        atm_scientific_control_read (
            root,
            "rmd",
            TEST_SHA,
            ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
            &value,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CONTROL_ERROR,
        ATM_SCIENTIFIC_CONTROL_ERROR_PATH
    );
    g_assert_null (value);
    g_clear_error (&error);

    g_free (link_path);
    g_free (directory);
    g_free (external_file);
    remove_tree_best_effort (external);
    g_free (external);
    g_free (root);
    remove_tree_best_effort (base);
    g_free (base);
}

static void
test_snapshot_sha_binding_mismatch_fails (void)
{
    char *base = new_temp_root (
        "atm-control-snapshot-binding-XXXXXX"
    );
    char *root = new_snapshot_root (
        base,
        "rmd",
        TEST_SHA
    );
    char *path = write_core_contract (
        root,
        "{\"behavioural_closure\":{\"active\":false}}"
    );
    AtmScientificControlValue *value = NULL;
    GError *error = NULL;
    const char *other_sha =
        "1123456789abcdef0123456789abcdef01234567";

    g_assert_false (
        atm_scientific_control_read (
            root,
            "rmd",
            other_sha,
            ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
            &value,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SCIENTIFIC_CONTROL_ERROR,
        ATM_SCIENTIFIC_CONTROL_ERROR_SNAPSHOT
    );
    g_assert_null (value);
    g_clear_error (&error);

    g_free (path);
    g_free (root);
    remove_tree_best_effort (base);
    g_free (base);
}

static JsonParser *
parse_fixture (void)
{
    const char *json =
        "{"
        "\"foo\":[\"bar\",\"baz\"],"
        "\"\":0,"
        "\"a/b\":1,"
        "\"m~n\":8"
        "}";
    JsonParser *parser = json_parser_new ();
    GError *error = NULL;

    g_assert_true (
        json_parser_load_from_data (
            parser,
            json,
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    return parser;
}

static void
test_json_pointer_rfc_examples (void)
{
    JsonParser *parser = parse_fixture ();
    JsonNode *root = json_parser_get_root (parser);
    GError *error = NULL;

    g_assert_true (
        atm_json_pointer_evaluate (
            root,
            "",
            &error
        ) == root
    );
    g_assert_no_error (error);

    JsonNode *bar = atm_json_pointer_evaluate (
        root,
        "/foo/0",
        &error
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        json_node_get_string (bar),
        ==,
        "bar"
    );

    JsonNode *slash = atm_json_pointer_evaluate (
        root,
        "/a~1b",
        &error
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        json_node_get_int (slash),
        ==,
        1
    );

    JsonNode *tilde = atm_json_pointer_evaluate (
        root,
        "/m~0n",
        &error
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        json_node_get_int (tilde),
        ==,
        8
    );

    JsonNode *empty = atm_json_pointer_evaluate (
        root,
        "/",
        &error
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        json_node_get_int (empty),
        ==,
        0
    );

    g_object_unref (parser);
}

static void
test_json_pointer_rejects_bad_array_indices (void)
{
    JsonParser *parser = parse_fixture ();
    GError *error = NULL;

    g_assert_null (
        atm_json_pointer_evaluate (
            json_parser_get_root (parser),
            "/foo/00",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_JSON_POINTER_ERROR,
        ATM_JSON_POINTER_ERROR_SYNTAX
    );
    g_clear_error (&error);

    g_assert_null (
        atm_json_pointer_evaluate (
            json_parser_get_root (parser),
            "/foo/-",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_JSON_POINTER_ERROR,
        ATM_JSON_POINTER_ERROR_LOOKUP
    );
    g_clear_error (&error);

    g_object_unref (parser);
}

static void
test_json_pointer_rejects_bad_escape (void)
{
    JsonParser *parser = parse_fixture ();
    GError *error = NULL;

    g_assert_null (
        atm_json_pointer_evaluate (
            json_parser_get_root (parser),
            "/m~2n",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_JSON_POINTER_ERROR,
        ATM_JSON_POINTER_ERROR_SYNTAX
    );
    g_clear_error (&error);

    g_object_unref (parser);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/scientific-control/rmd-behavioural-closure-false",
        test_rmd_behavioural_closure_false
    );
    g_test_add_func (
        "/scientific-control/repository-mismatch",
        test_repository_mismatch_fails
    );
    g_test_add_func (
        "/scientific-control/wrong-type",
        test_wrong_value_type_fails_closed
    );
    g_test_add_func (
        "/scientific-control/missing-pointer",
        test_missing_pointer_fails_closed
    );
    g_test_add_func (
        "/scientific-control/final-symlink-rejected",
        test_final_symlink_is_rejected
    );
    g_test_add_func (
        "/scientific-control/snapshot-sha-binding",
        test_snapshot_sha_binding_mismatch_fails
    );
    g_test_add_func (
        "/scientific-control/json-pointer-rfc-examples",
        test_json_pointer_rfc_examples
    );
    g_test_add_func (
        "/scientific-control/json-pointer-bad-array-index",
        test_json_pointer_rejects_bad_array_indices
    );
    g_test_add_func (
        "/scientific-control/json-pointer-bad-escape",
        test_json_pointer_rejects_bad_escape
    );

    return g_test_run ();
}