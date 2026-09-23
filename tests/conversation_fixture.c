#include "conversation_fixture.h"

#include "retrieval_index_lifecycle.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

static void
remove_tree_best_effort (
    const char *path
)
{
    GStatBuf stat_buffer;

    if (path == NULL ||
        g_lstat (
            path,
            &stat_buffer
        ) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *directory_error = NULL;
    GDir *directory = g_dir_open (
        path,
        0,
        &directory_error
    );

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (
                    directory
                )) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );

            remove_tree_best_effort (
                child
            );
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&directory_error);
    g_rmdir (path);
}

static gboolean
write_text (
    const char *root,
    const char *relative,
    const char *contents,
    GError **error
)
{
    char *path = g_build_filename (
        root,
        relative,
        NULL
    );
    char *parent =
        g_path_get_dirname (path);

    if (g_mkdir_with_parents (
            parent,
            0700
        ) != 0) {
        g_set_error (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            "Could not create fixture directory '%s': %s.",
            parent,
            g_strerror (errno)
        );
        g_free (parent);
        g_free (path);
        return FALSE;
    }

    gboolean ok =
        g_file_set_contents (
            path,
            contents,
            -1,
            error
        );

    g_free (parent);
    g_free (path);
    return ok;
}

gboolean
atm_test_conversation_fixture_create (
    const char *repository_id,
    const char *acronym,
    const char *display_name,
    const char *repository_version,
    const char *snapshot_sha,
    char **out_snapshot_root,
    char **out_cache_root,
    char **out_index_path,
    char **out_repository_version,
    GError **error
)
{
    if (repository_id == NULL ||
        acronym == NULL ||
        display_name == NULL ||
        repository_version == NULL ||
        snapshot_sha == NULL ||
        out_snapshot_root == NULL ||
        out_cache_root == NULL ||
        out_index_path == NULL ||
        out_repository_version == NULL ||
        *out_snapshot_root != NULL ||
        *out_cache_root != NULL ||
        *out_index_path != NULL ||
        *out_repository_version != NULL) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_ARGUMENT,
            "Conversation test fixture received invalid arguments."
        );
        return FALSE;
    }

    char *snapshot_root = NULL;
    char *cache_root = NULL;
    char *index_path = NULL;
    char *detected_version = NULL;
    char *manifest = NULL;
    char *cff = NULL;
    GError *local_error = NULL;
    AtmRetrievalEnsureResult ensure_result;

    snapshot_root = g_dir_make_tmp (
        "atm-conversation-fixture-snapshot-XXXXXX",
        &local_error
    );

    if (snapshot_root == NULL) {
        g_propagate_error (
            error,
            local_error
        );
        return FALSE;
    }

    cache_root = g_dir_make_tmp (
        "atm-conversation-fixture-cache-XXXXXX",
        &local_error
    );

    if (cache_root == NULL) {
        remove_tree_best_effort (
            snapshot_root
        );
        g_free (snapshot_root);
        g_propagate_error (
            error,
            local_error
        );
        return FALSE;
    }

    manifest = g_strdup_printf (
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"%s\",\n"
        "  \"acronym\": \"%s\",\n"
        "  \"display_name\": \"%s\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": [\"STATUS.md\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n",
        repository_id,
        acronym,
        display_name
    );

    cff = g_strdup_printf (
        "cff-version: 1.2.0\n"
        "message: Cite this software.\n"
        "title: Fixture\n"
        "version: %s\n"
        "authors:\n"
        "  - family-names: Test\n"
        "    given-names: Fixture\n",
        repository_version
    );

    gboolean ok =
        write_text (
            snapshot_root,
            ".atm/repository.json",
            manifest,
            &local_error
        ) &&
        write_text (
            snapshot_root,
            "CITATION.cff",
            cff,
            &local_error
        ) &&
        write_text (
            snapshot_root,
            "STATUS.md",
            "# Scientific status\n"
            "Current fixture status.\n",
            &local_error
        );

    if (!ok) {
        goto fail;
    }

    if (!atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &index_path,
            &detected_version,
            &ensure_result,
            &local_error
        )) {
        goto fail;
    }

    if (g_strcmp0 (
            detected_version,
            repository_version
        ) != 0) {
        g_set_error (
            &local_error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Fixture version '%s' does not match requested version '%s'.",
            detected_version != NULL
                ? detected_version
                : "(null)",
            repository_version
        );
        goto fail;
    }

    if (ensure_result !=
            ATM_RETRIEVAL_ENSURE_REBUILT &&
        ensure_result !=
            ATM_RETRIEVAL_ENSURE_REUSED) {
        g_set_error_literal (
            &local_error,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "Conversation fixture retrieval index has an unknown ensure result."
        );
        goto fail;
    }

    *out_snapshot_root =
        g_steal_pointer (
            &snapshot_root
        );
    *out_cache_root =
        g_steal_pointer (
            &cache_root
        );
    *out_index_path =
        g_steal_pointer (
            &index_path
        );
    *out_repository_version =
        g_steal_pointer (
            &detected_version
        );

    g_free (manifest);
    g_free (cff);
    return TRUE;

fail:
    g_free (index_path);
    g_free (detected_version);
    g_free (manifest);
    g_free (cff);

    remove_tree_best_effort (
        snapshot_root
    );
    remove_tree_best_effort (
        cache_root
    );
    g_free (snapshot_root);
    g_free (cache_root);

    g_propagate_error (
        error,
        local_error
    );
    return FALSE;
}

void
atm_test_conversation_fixture_remove (
    const char *snapshot_root,
    const char *cache_root
)
{
    remove_tree_best_effort (
        snapshot_root
    );
    remove_tree_best_effort (
        cache_root
    );
}
