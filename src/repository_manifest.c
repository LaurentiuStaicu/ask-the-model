#include "repository_manifest.h"
#include "cff_version.h"

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define ATM_MAX_MANIFEST_BYTES (256 * 1024)

GQuark
atm_manifest_error_quark (void)
{
    return g_quark_from_static_string ("atm-manifest-error-quark");
}

static gboolean
string_in_set (
    const char *value,
    const char *const *allowed,
    gsize allowed_count
)
{
    for (gsize i = 0; i < allowed_count; i++) {
        if (g_strcmp0 (value, allowed[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
object_has_exact_members (
    JsonObject *object,
    const char *const *allowed,
    gsize allowed_count
)
{
    GList *members = json_object_get_members (object);
    gboolean ok = g_list_length (members) == (gint) allowed_count;

    for (GList *item = members; ok && item != NULL; item = item->next) {
        const char *name = item->data;
        if (!string_in_set (name, allowed, allowed_count)) {
            ok = FALSE;
        }
    }

    g_list_free (members);
    return ok;
}

static gboolean
node_is_string (JsonNode *node)
{
    return node != NULL &&
        json_node_get_node_type (node) == JSON_NODE_VALUE &&
        json_node_get_value_type (node) == G_TYPE_STRING;
}

static gboolean
node_is_int64 (JsonNode *node)
{
    return node != NULL &&
        json_node_get_node_type (node) == JSON_NODE_VALUE &&
        json_node_get_value_type (node) == G_TYPE_INT64;
}

static gboolean
repository_path_is_safe (const char *path)
{
    char **parts;
    gboolean ok = TRUE;

    if (path == NULL ||
        path[0] == '\0' ||
        strlen (path) > 512 ||
        g_path_is_absolute (path) ||
        strstr (path, "//") != NULL ||
        strpbrk (path, "*?[") != NULL) {
        return FALSE;
    }

    parts = g_strsplit (path, "/", -1);

    for (gsize i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0' ||
            strcmp (parts[i], ".") == 0 ||
            strcmp (parts[i], "..") == 0) {
            ok = FALSE;
            break;
        }
    }

    g_strfreev (parts);
    return ok;
}

static gboolean
path_exists_without_symlink (
    const char *snapshot_root,
    const char *relative_path,
    gboolean require_exists
)
{
    char **parts = g_strsplit (relative_path, "/", -1);
    char *current = g_strdup (snapshot_root);
    gboolean ok = TRUE;

    for (gsize i = 0; parts[i] != NULL; i++) {
        char *next = g_build_filename (current, parts[i], NULL);
        GStatBuf stat_buffer;

        g_free (current);
        current = next;

        if (g_lstat (current, &stat_buffer) != 0) {
            if (!require_exists && errno == ENOENT) {
                ok = TRUE;
            } else {
                ok = FALSE;
            }
            break;
        }

        if (S_ISLNK (stat_buffer.st_mode)) {
            ok = FALSE;
            break;
        }
    }

    g_free (current);
    g_strfreev (parts);
    return ok;
}

static gboolean
validate_string_array (
    JsonObject *owner,
    const char *member_name,
    const char *snapshot_root,
    gboolean require_paths,
    GError **error
)
{
    JsonNode *node;
    JsonArray *array;
    GHashTable *seen;

    if (!json_object_has_member (owner, member_name)) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Manifest is missing array member '%s'.",
            member_name
        );
        return FALSE;
    }

    node = json_object_get_member (owner, member_name);
    if (node == NULL ||
        json_node_get_node_type (node) != JSON_NODE_ARRAY) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Manifest member '%s' must be an array.",
            member_name
        );
        return FALSE;
    }

    array = json_node_get_array (node);
    seen = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );

    for (guint i = 0; i < json_array_get_length (array); i++) {
        JsonNode *item = json_array_get_element (array, i);
        const char *path;

        if (!node_is_string (item)) {
            g_set_error (
                error,
                ATM_MANIFEST_ERROR,
                ATM_MANIFEST_ERROR_SHAPE,
                "Manifest array '%s' contains a non-string item.",
                member_name
            );
            g_hash_table_unref (seen);
            return FALSE;
        }

        path = json_node_get_string (item);
        if (!repository_path_is_safe (path)) {
            g_set_error (
                error,
                ATM_MANIFEST_ERROR,
                ATM_MANIFEST_ERROR_PATH,
                "Manifest array '%s' contains unsafe path '%s'.",
                member_name,
                path
            );
            g_hash_table_unref (seen);
            return FALSE;
        }

        if (g_hash_table_contains (seen, path)) {
            g_set_error (
                error,
                ATM_MANIFEST_ERROR,
                ATM_MANIFEST_ERROR_SHAPE,
                "Manifest array '%s' contains duplicate path '%s'.",
                member_name,
                path
            );
            g_hash_table_unref (seen);
            return FALSE;
        }

        g_hash_table_add (seen, g_strdup (path));

        if (require_paths &&
            !path_exists_without_symlink (
                snapshot_root,
                path,
                TRUE
            )) {
            g_set_error (
                error,
                ATM_MANIFEST_ERROR,
                ATM_MANIFEST_ERROR_MISSING_PATH,
                "Manifest path '%s' is missing or crosses a symlink.",
                path
            );
            g_hash_table_unref (seen);
            return FALSE;
        }
    }

    g_hash_table_unref (seen);
    return TRUE;
}

static gboolean
validate_exact_string_member (
    JsonObject *object,
    const char *member_name,
    const char *expected,
    AtmManifestError code,
    GError **error
)
{
    JsonNode *node;

    if (!json_object_has_member (object, member_name)) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Manifest is missing member '%s'.",
            member_name
        );
        return FALSE;
    }

    node = json_object_get_member (object, member_name);
    if (!node_is_string (node)) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Manifest member '%s' must be a string.",
            member_name
        );
        return FALSE;
    }

    if (g_strcmp0 (json_node_get_string (node), expected) != 0) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            code,
            "Manifest member '%s' does not match the expected value.",
            member_name
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_repository_validate_snapshot (
    const char *snapshot_root,
    const char *expected_id,
    const char *expected_acronym,
    const char *expected_display_name,
    char **out_version,
    GError **error
)
{
    static const char *const top_members[] = {
        "schema_version",
        "repository_id",
        "acronym",
        "display_name",
        "version_source",
        "status_source",
        "required_paths",
        "retrieval"
    };
    static const char *const version_members[] = {
        "type",
        "path"
    };
    static const char *const retrieval_members[] = {
        "canonical",
        "structural",
        "evidence",
        "tabular",
        "implementation",
        "exclude"
    };

    char *manifest_path = NULL;
    char *manifest_data = NULL;
    gsize manifest_length = 0;
    JsonParser *parser = NULL;
    JsonNode *root_node;
    JsonObject *root;
    JsonNode *schema_node;
    JsonNode *version_source_node;
    JsonObject *version_source;
    JsonNode *retrieval_node;
    JsonObject *retrieval;
    char *citation_path = NULL;
    char *citation_data = NULL;
    gsize citation_length = 0;
    char *status_path = NULL;
    char *version = NULL;
    gboolean ok = FALSE;
    GError *local_error = NULL;

    g_return_val_if_fail (snapshot_root != NULL, FALSE);
    g_return_val_if_fail (expected_id != NULL, FALSE);
    g_return_val_if_fail (expected_acronym != NULL, FALSE);
    g_return_val_if_fail (expected_display_name != NULL, FALSE);
    g_return_val_if_fail (out_version != NULL, FALSE);
    g_return_val_if_fail (*out_version == NULL, FALSE);

    manifest_path = g_build_filename (
        snapshot_root,
        ".atm",
        "repository.json",
        NULL
    );

    GStatBuf manifest_stat;
    if (g_lstat (manifest_path, &manifest_stat) != 0 ||
        !S_ISREG (manifest_stat.st_mode) ||
        S_ISLNK (manifest_stat.st_mode)) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_MISSING_PATH,
            "Snapshot does not contain a regular .atm/repository.json."
        );
        goto out;
    }

    if ((guint64) manifest_stat.st_size > ATM_MAX_MANIFEST_BYTES) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_PARSE,
            "Repository manifest exceeds the configured size limit."
        );
        goto out;
    }

    if (!g_file_get_contents (
            manifest_path,
            &manifest_data,
            &manifest_length,
            &local_error
        )) {
        g_propagate_error (error, local_error);
        local_error = NULL;
        goto out;
    }

    parser = json_parser_new ();
    if (!json_parser_load_from_data (
            parser,
            manifest_data,
            manifest_length,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_PARSE,
            "Repository manifest is not valid JSON: %s",
            local_error != NULL ? local_error->message : "unknown parse error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    root_node = json_parser_get_root (parser);
    if (root_node == NULL ||
        json_node_get_node_type (root_node) != JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Repository manifest root must be a JSON object."
        );
        goto out;
    }

    root = json_node_get_object (root_node);
    if (!object_has_exact_members (
            root,
            top_members,
            G_N_ELEMENTS (top_members)
        )) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Repository manifest top-level members do not match schema v1."
        );
        goto out;
    }

    schema_node = json_object_get_member (root, "schema_version");
    if (!node_is_int64 (schema_node) ||
        json_node_get_int (schema_node) != 1) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Repository manifest schema_version must be integer 1."
        );
        goto out;
    }

    if (!validate_exact_string_member (
            root,
            "repository_id",
            expected_id,
            ATM_MANIFEST_ERROR_IDENTITY,
            error
        ) ||
        !validate_exact_string_member (
            root,
            "acronym",
            expected_acronym,
            ATM_MANIFEST_ERROR_IDENTITY,
            error
        ) ||
        !validate_exact_string_member (
            root,
            "display_name",
            expected_display_name,
            ATM_MANIFEST_ERROR_IDENTITY,
            error
        )) {
        goto out;
    }

    version_source_node =
        json_object_get_member (root, "version_source");
    if (version_source_node == NULL ||
        json_node_get_node_type (version_source_node) !=
            JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Repository manifest version_source must be an object."
        );
        goto out;
    }

    version_source = json_node_get_object (version_source_node);
    if (!object_has_exact_members (
            version_source,
            version_members,
            G_N_ELEMENTS (version_members)
        ) ||
        !validate_exact_string_member (
            version_source,
            "type",
            "cff",
            ATM_MANIFEST_ERROR_SHAPE,
            error
        ) ||
        !validate_exact_string_member (
            version_source,
            "path",
            "CITATION.cff",
            ATM_MANIFEST_ERROR_SHAPE,
            error
        )) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                ATM_MANIFEST_ERROR,
                ATM_MANIFEST_ERROR_SHAPE,
                "Repository manifest version_source does not match schema v1."
            );
        }
        goto out;
    }

    if (!validate_exact_string_member (
            root,
            "status_source",
            "STATUS.md",
            ATM_MANIFEST_ERROR_SHAPE,
            error
        )) {
        goto out;
    }

    if (!validate_string_array (
            root,
            "required_paths",
            snapshot_root,
            TRUE,
            error
        )) {
        goto out;
    }

    retrieval_node = json_object_get_member (root, "retrieval");
    if (retrieval_node == NULL ||
        json_node_get_node_type (retrieval_node) !=
            JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Repository manifest retrieval must be an object."
        );
        goto out;
    }

    retrieval = json_node_get_object (retrieval_node);
    if (!object_has_exact_members (
            retrieval,
            retrieval_members,
            G_N_ELEMENTS (retrieval_members)
        )) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_SHAPE,
            "Repository manifest retrieval members do not match schema v1."
        );
        goto out;
    }

    for (gsize i = 0; i < G_N_ELEMENTS (retrieval_members); i++) {
        gboolean require_path =
            g_strcmp0 (retrieval_members[i], "exclude") != 0;

        if (!validate_string_array (
                retrieval,
                retrieval_members[i],
                snapshot_root,
                require_path,
                error
            )) {
            goto out;
        }
    }

    citation_path = g_build_filename (
        snapshot_root,
        "CITATION.cff",
        NULL
    );
    if (!path_exists_without_symlink (
            snapshot_root,
            "CITATION.cff",
            TRUE
        ) ||
        !g_file_get_contents (
            citation_path,
            &citation_data,
            &citation_length,
            &local_error
        )) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_VERSION,
            "CITATION.cff is missing, unsafe or unreadable."
        );
        g_clear_error (&local_error);
        goto out;
    }

    if (!atm_cff_extract_version (
            (const guint8 *) citation_data,
            citation_length,
            &version,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_VERSION,
            "CITATION.cff version metadata is invalid: %s",
            local_error != NULL ? local_error->message : "unknown CFF error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    status_path = g_build_filename (
        snapshot_root,
        "STATUS.md",
        NULL
    );
    if (!path_exists_without_symlink (
            snapshot_root,
            "STATUS.md",
            TRUE
        ) ||
        !g_file_test (status_path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal (
            error,
            ATM_MANIFEST_ERROR,
            ATM_MANIFEST_ERROR_STATUS,
            "STATUS.md is missing or unsafe."
        );
        goto out;
    }

    *out_version = g_steal_pointer (&version);
    ok = TRUE;

out:
    g_clear_error (&local_error);
    g_clear_pointer (&version, g_free);
    g_clear_pointer (&status_path, g_free);
    g_clear_pointer (&citation_data, g_free);
    g_clear_pointer (&citation_path, g_free);
    g_clear_object (&parser);
    g_clear_pointer (&manifest_data, g_free);
    g_clear_pointer (&manifest_path, g_free);
    return ok;
}
