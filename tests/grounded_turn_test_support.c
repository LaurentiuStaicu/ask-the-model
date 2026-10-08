#include "grounded_turn_test_support.h"

#include "retrieval_index_lifecycle.h"

#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

static void
write_text (
    const char *root,
    const char *relative,
    const char *contents,
    GError **error
)
{
    char *path = g_build_filename (root, relative, NULL);
    char *parent = g_path_get_dirname (path);

    if (g_mkdir_with_parents (parent, 0700) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create fixture directory: %s",
            parent
        );
        g_free (parent);
        g_free (path);
        return;
    }

    g_file_set_contents (path, contents, -1, error);
    g_free (parent);
    g_free (path);
}

gboolean
atm_grounded_turn_fixture_create (
    char **out_cache_root,
    char **out_snapshot_root,
    char **out_index_path,
    char **out_version,
    char **out_snapshot_sha,
    GError **error
)
{
    g_return_val_if_fail (out_cache_root != NULL, FALSE);
    g_return_val_if_fail (out_snapshot_root != NULL, FALSE);
    g_return_val_if_fail (out_index_path != NULL, FALSE);
    g_return_val_if_fail (out_version != NULL, FALSE);
    g_return_val_if_fail (out_snapshot_sha != NULL, FALSE);

    *out_cache_root = NULL;
    *out_snapshot_root = NULL;
    *out_index_path = NULL;
    *out_version = NULL;
    *out_snapshot_sha = NULL;

    char *cache_root = g_dir_make_tmp (
        "atm-grounded-turn-cache-XXXXXX",
        error
    );
    if (cache_root == NULL) {
        return FALSE;
    }

    char *snapshot_root = g_dir_make_tmp (
        "atm-grounded-turn-snapshot-XXXXXX",
        error
    );
    if (snapshot_root == NULL) {
        g_free (cache_root);
        return FALSE;
    }

    const char *snapshot_sha =
        "1111111111111111111111111111111111111111";

    write_text (
        snapshot_root,
        ".atm/repository.json",
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"acronym\": \"EWD\",\n"
        "  \"display_name\": \"Fixture\",\n"
        "  \"version_source\": {\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": [\"CITATION.cff\", \"STATUS.md\", \"model/core.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n",
        error
    );
    if (*error != NULL) {
        atm_grounded_turn_fixture_destroy (
            cache_root, snapshot_root, NULL, NULL, NULL
        );
        return FALSE;
    }

    write_text (
        snapshot_root,
        "CITATION.cff",
        "cff-version: 1.2.0\n"
        "message: Cite this software.\n"
        "title: Fixture\n"
        "version: 0.1.0\n"
        "type: software\n"
        "authors:\n"
        "  - family-names: Test\n"
        "    given-names: Fixture\n",
        error
    );
    if (*error != NULL) {
        atm_grounded_turn_fixture_destroy (
            cache_root, snapshot_root, NULL, NULL, NULL
        );
        return FALSE;
    }

    write_text (
        snapshot_root,
        "STATUS.md",
        "# Scientific status\n"
        "## Release status\n"
        "Current fixture status.\n",
        error
    );
    if (*error != NULL) {
        atm_grounded_turn_fixture_destroy (
            cache_root, snapshot_root, NULL, NULL, NULL
        );
        return FALSE;
    }

    write_text (
        snapshot_root,
        "model/core.json",
        "{\"variables\":[{\"id\":\"fixture_status\",\"label\":{\"en\":\"Fixture status\"}}]}\n",
        error
    );
    if (*error != NULL) {
        atm_grounded_turn_fixture_destroy (
            cache_root, snapshot_root, NULL, NULL, NULL
        );
        return FALSE;
    }

    AtmRetrievalEnsureResult ensure_result;
    char *index_path = NULL;
    char *version = NULL;

    if (!atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha,
            &index_path,
            &version,
            &ensure_result,
            error
        )) {
        atm_grounded_turn_fixture_destroy (
            cache_root, snapshot_root, NULL, NULL, NULL
        );
        g_free (index_path);
        g_free (version);
        return FALSE;
    }

    *out_cache_root = cache_root;
    *out_snapshot_root = snapshot_root;
    *out_index_path = index_path;
    *out_version = version;
    *out_snapshot_sha = g_strdup (snapshot_sha);
    return TRUE;
}

static void
remove_tree_best_effort (
    const char *path
)
{
    if (path == NULL) {
        return;
    }

    GDir *directory = g_dir_open (path, 0, NULL);
    if (directory != NULL) {
        const char *name = NULL;
        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }
        g_dir_close (directory);
    }

    if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
        g_rmdir (path);
    } else {
        g_remove (path);
    }
}

void
atm_grounded_turn_fixture_destroy (
    char *cache_root,
    char *snapshot_root,
    char *index_path,
    char *version,
    char *snapshot_sha
)
{
    (void) index_path;
    g_free (version);
    g_free (snapshot_sha);

    remove_tree_best_effort (snapshot_root);
    remove_tree_best_effort (cache_root);
}
