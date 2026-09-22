#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>
#include <yaml.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ORACLE_STATE_SCHEMA_VERSION 1
#define ORACLE_MANIFEST_SCHEMA_VERSION 1
#define ORACLE_RETRIEVAL_SCHEMA_VERSION 2

typedef enum {
    ORACLE_ERROR_ARGUMENT,
    ORACLE_ERROR_STATE,
    ORACLE_ERROR_SNAPSHOT,
    ORACLE_ERROR_MANIFEST,
    ORACLE_ERROR_VERSION,
    ORACLE_ERROR_INDEX,
    ORACLE_ERROR_INTEGRITY
} OracleError;

typedef struct {
    char *sha;
    char *version;
} OraclePinnedState;

typedef struct {
    char *data_root;
    char *cache_root;
    char *state_root;
    char *repository_id;
    char *acronym;
    char *display_name;
} OracleInput;

typedef enum {
    ORACLE_ROLE_STATUS = 1u << 0,
    ORACLE_ROLE_CANONICAL = 1u << 1,
    ORACLE_ROLE_STRUCTURAL = 1u << 2,
    ORACLE_ROLE_EVIDENCE = 1u << 3,
    ORACLE_ROLE_TABULAR = 1u << 4,
    ORACLE_ROLE_IMPLEMENTATION = 1u << 5
} OracleSourceRole;

typedef struct {
    char *status_source;
    GPtrArray *canonical;
    GPtrArray *structural;
    GPtrArray *evidence;
    GPtrArray *tabular;
    GPtrArray *implementation;
    GPtrArray *exclude;
} OracleManifestPolicy;

typedef struct {
    char *path;
    char *sha256;
    guint64 byte_size;
    char *media_type;
    guint roles;
} OracleExpectedSource;

static GQuark
oracle_error_quark (void)
{
    return g_quark_from_static_string ("atm-independent-oracle-error");
}

#define ORACLE_ERROR (oracle_error_quark ())

static void
oracle_pinned_state_clear (OraclePinnedState *state)
{
    g_clear_pointer (&state->sha, g_free);
    g_clear_pointer (&state->version, g_free);
}

static void
oracle_manifest_policy_clear (OracleManifestPolicy *policy)
{
    if (policy == NULL) {
        return;
    }

    g_clear_pointer (&policy->status_source, g_free);
    g_clear_pointer (&policy->canonical, g_ptr_array_unref);
    g_clear_pointer (&policy->structural, g_ptr_array_unref);
    g_clear_pointer (&policy->evidence, g_ptr_array_unref);
    g_clear_pointer (&policy->tabular, g_ptr_array_unref);
    g_clear_pointer (&policy->implementation, g_ptr_array_unref);
    g_clear_pointer (&policy->exclude, g_ptr_array_unref);
}

static void
oracle_expected_source_free (OracleExpectedSource *source)
{
    if (source == NULL) {
        return;
    }

    g_free (source->path);
    g_free (source->sha256);
    g_free (source->media_type);
    g_free (source);
}

static gboolean
is_nonempty_string_node (JsonNode *node)
{
    return node != NULL &&
        json_node_get_node_type (node) == JSON_NODE_VALUE &&
        json_node_get_value_type (node) == G_TYPE_STRING &&
        json_node_get_string (node) != NULL &&
        json_node_get_string (node)[0] != '\0';
}

static gboolean
is_integer_node (JsonNode *node)
{
    if (node == NULL ||
        json_node_get_node_type (node) != JSON_NODE_VALUE) {
        return FALSE;
    }

    GType value_type = json_node_get_value_type (node);
    return value_type == G_TYPE_INT64 ||
        value_type == G_TYPE_INT ||
        value_type == G_TYPE_LONG;
}

static gboolean
lower_hex_is_valid (const char *value, gsize expected_length)
{
    if (value == NULL || strlen (value) != expected_length) {
        return FALSE;
    }

    for (gsize i = 0; i < expected_length; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (value[i] >= 'A' && value[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
safe_component_is_valid (const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return FALSE;
    }

    for (const char *p = value; *p != '\0'; p++) {
        if (!(g_ascii_islower (*p) ||
              g_ascii_isdigit (*p) ||
              *p == '-' ||
              *p == '_')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
relative_path_is_safe (const char *relative)
{
    if (relative == NULL ||
        relative[0] == '\0' ||
        g_path_is_absolute (relative)) {
        return FALSE;
    }

    char **parts = g_strsplit (relative, "/", -1);
    gboolean ok = TRUE;

    for (gsize i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0' ||
            g_strcmp0 (parts[i], ".") == 0 ||
            g_strcmp0 (parts[i], "..") == 0) {
            ok = FALSE;
            break;
        }
    }

    g_strfreev (parts);
    return ok;
}

static gboolean
root_is_real_directory (const char *root, GError **error)
{
    GStatBuf st;

    if (g_lstat (root, &st) != 0 ||
        !S_ISDIR (st.st_mode) ||
        S_ISLNK (st.st_mode)) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_SNAPSHOT,
            "Oracle root is not a real directory: %s",
            root
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
path_beneath_root_is_safe (
    const char *root,
    const char *relative,
    gboolean final_must_be_directory,
    GError **error
)
{
    if (!relative_path_is_safe (relative)) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_SNAPSHOT,
            "Unsafe relative path: %s",
            relative != NULL ? relative : "(null)"
        );
        return FALSE;
    }

    if (!root_is_real_directory (root, error)) {
        return FALSE;
    }

    char **parts = g_strsplit (relative, "/", -1);
    char *current = g_strdup (root);
    gboolean ok = TRUE;

    for (gsize i = 0; parts[i] != NULL; i++) {
        char *next = g_build_filename (current, parts[i], NULL);
        GStatBuf st;
        gboolean is_last = parts[i + 1] == NULL;

        g_free (current);
        current = next;

        if (g_lstat (current, &st) != 0 || S_ISLNK (st.st_mode)) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Oracle path is missing or crosses a symlink: %s",
                current
            );
            ok = FALSE;
            break;
        }

        if (!is_last && !S_ISDIR (st.st_mode)) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Oracle path component is not a directory: %s",
                current
            );
            ok = FALSE;
            break;
        }

        if (is_last) {
            gboolean type_ok = final_must_be_directory
                ? S_ISDIR (st.st_mode)
                : S_ISREG (st.st_mode);

            if (!type_ok) {
                g_set_error (
                    error,
                    ORACLE_ERROR,
                    ORACLE_ERROR_SNAPSHOT,
                    "Oracle final path has the wrong file type: %s",
                    current
                );
                ok = FALSE;
                break;
            }
        }
    }

    g_free (current);
    g_strfreev (parts);
    return ok;
}

static gboolean
path_beneath_root_exists_no_symlink (
    const char *root,
    const char *relative,
    GError **error
)
{
    if (!relative_path_is_safe (relative) ||
        !root_is_real_directory (root, error)) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Manifest path is unsafe."
            );
        }
        return FALSE;
    }

    char **parts = g_strsplit (relative, "/", -1);
    char *current = g_strdup (root);
    gboolean ok = TRUE;

    for (gsize i = 0; parts[i] != NULL; i++) {
        char *next = g_build_filename (current, parts[i], NULL);
        GStatBuf st;
        gboolean is_last = parts[i + 1] == NULL;

        g_free (current);
        current = next;

        if (g_lstat (current, &st) != 0 ||
            S_ISLNK (st.st_mode) ||
            (!S_ISDIR (st.st_mode) &&
             !S_ISREG (st.st_mode))) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Manifest path is missing, unsafe or unsupported: %s",
                current
            );
            ok = FALSE;
            break;
        }

        if (!is_last && !S_ISDIR (st.st_mode)) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Manifest path crosses a non-directory component: %s",
                current
            );
            ok = FALSE;
            break;
        }
    }

    g_free (current);
    g_strfreev (parts);
    return ok;
}

static gboolean
copy_manifest_path_array (
    JsonObject *owner,
    const char *member_name,
    const char *snapshot_root,
    gboolean require_exists,
    GPtrArray **out_values,
    GError **error
)
{
    JsonNode *node =
        json_object_get_member (owner, member_name);

    if (node == NULL ||
        json_node_get_node_type (node) != JSON_NODE_ARRAY) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Manifest member '%s' is not an array.",
            member_name
        );
        return FALSE;
    }

    JsonArray *array = json_node_get_array (node);
    GPtrArray *values =
        g_ptr_array_new_with_free_func (g_free);
    GHashTable *seen =
        g_hash_table_new_full (
            g_str_hash,
            g_str_equal,
            g_free,
            NULL
        );

    for (guint i = 0;
         i < json_array_get_length (array);
         i++) {
        JsonNode *item =
            json_array_get_element (array, i);

        if (!is_nonempty_string_node (item)) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_MANIFEST,
                "Manifest array '%s' contains a non-string or empty item.",
                member_name
            );
            g_hash_table_unref (seen);
            g_ptr_array_unref (values);
            return FALSE;
        }

        const char *value = json_node_get_string (item);

        if (!relative_path_is_safe (value) ||
            g_hash_table_contains (seen, value) ||
            (require_exists &&
             !path_beneath_root_exists_no_symlink (
                 snapshot_root,
                 value,
                 error
             ))) {
            if (error != NULL && *error == NULL) {
                g_set_error (
                    error,
                    ORACLE_ERROR,
                    ORACLE_ERROR_MANIFEST,
                    "Manifest array '%s' contains an unsafe or duplicate path '%s'.",
                    member_name,
                    value
                );
            }
            g_hash_table_unref (seen);
            g_ptr_array_unref (values);
            return FALSE;
        }

        g_hash_table_add (seen, g_strdup (value));
        g_ptr_array_add (values, g_strdup (value));
    }

    g_hash_table_unref (seen);
    *out_values = values;
    return TRUE;
}

static gboolean
path_matches_prefix_independently (
    const char *path,
    const char *prefix
)
{
    if (g_strcmp0 (path, prefix) == 0) {
        return TRUE;
    }

    gsize length = strlen (prefix);
    return g_str_has_prefix (path, prefix) &&
        path[length] == '/';
}

static gboolean
array_matches_path_independently (
    GPtrArray *values,
    const char *path
)
{
    for (guint i = 0; i < values->len; i++) {
        const char *prefix =
            g_ptr_array_index (values, i);

        if (path_matches_prefix_independently (
                path,
                prefix
            )) {
            return TRUE;
        }
    }

    return FALSE;
}

static guint
oracle_roles_for_path (
    const OracleManifestPolicy *policy,
    const char *path
)
{
    guint roles = 0;

    if (g_strcmp0 (path, policy->status_source) == 0) {
        roles |= ORACLE_ROLE_STATUS;
    }
    if (array_matches_path_independently (
            policy->canonical,
            path
        )) {
        roles |= ORACLE_ROLE_CANONICAL;
    }
    if (array_matches_path_independently (
            policy->structural,
            path
        )) {
        roles |= ORACLE_ROLE_STRUCTURAL;
    }
    if (array_matches_path_independently (
            policy->evidence,
            path
        )) {
        roles |= ORACLE_ROLE_EVIDENCE;
    }
    if (array_matches_path_independently (
            policy->tabular,
            path
        )) {
        roles |= ORACLE_ROLE_TABULAR;
    }
    if (array_matches_path_independently (
            policy->implementation,
            path
        )) {
        roles |= ORACLE_ROLE_IMPLEMENTATION;
    }

    return roles;
}

static gboolean
oracle_path_is_excluded (
    const OracleManifestPolicy *policy,
    const char *path
)
{
    return array_matches_path_independently (
        policy->exclude,
        path
    );
}

static gint
compare_expected_source_paths (
    gconstpointer a,
    gconstpointer b
)
{
    const OracleExpectedSource *left =
        *(OracleExpectedSource * const *) a;
    const OracleExpectedSource *right =
        *(OracleExpectedSource * const *) b;

    return g_strcmp0 (left->path, right->path);
}

static const char *
oracle_media_type_for_path (const char *path)
{
    if (g_str_has_suffix (path, ".json")) {
        return "application/json";
    }
    if (g_str_has_suffix (path, ".csv")) {
        return "text/csv";
    }
    if (g_str_has_suffix (path, ".md")) {
        return "text/markdown";
    }
    if (g_str_has_suffix (path, ".cff") ||
        g_str_has_suffix (path, ".yaml") ||
        g_str_has_suffix (path, ".yml")) {
        return "text/yaml";
    }
    if (g_str_has_suffix (path, ".py")) {
        return "text/x-python";
    }
    if (g_str_has_suffix (path, ".vala")) {
        return "text/x-vala";
    }
    if (g_str_has_suffix (path, ".c") ||
        g_str_has_suffix (path, ".h")) {
        return "text/x-c";
    }
    if (g_str_has_suffix (path, ".sql")) {
        return "text/x-sql";
    }
    if (g_str_has_suffix (path, ".txt") ||
        g_str_has_suffix (path, ".toml") ||
        g_str_has_suffix (path, ".sha256")) {
        return "text/plain";
    }

    return "application/octet-stream";
}

static gboolean
oracle_hash_file (
    const char *path,
    char **out_sha256,
    guint64 *out_size,
    GError **error
)
{
    int fd = g_open (
        path,
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW,
        0
    );
    GChecksum *checksum = NULL;
    guint64 total = 0;
    guint8 buffer[64 * 1024];
    gboolean ok = FALSE;

    if (fd < 0) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_SNAPSHOT,
            "Oracle could not open source file without following symlinks: %s",
            path
        );
        return FALSE;
    }

    checksum = g_checksum_new (G_CHECKSUM_SHA256);
    if (checksum == NULL) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "Oracle SHA-256 support is unavailable."
        );
        goto out;
    }

    while (TRUE) {
        ssize_t count = read (
            fd,
            buffer,
            sizeof buffer
        );

        if (count == 0) {
            break;
        }

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Oracle could not read source file: %s",
                path
            );
            goto out;
        }

        g_checksum_update (
            checksum,
            buffer,
            (gsize) count
        );
        total += (guint64) count;
    }

    *out_sha256 =
        g_strdup (g_checksum_get_string (checksum));
    *out_size = total;
    ok = *out_sha256 != NULL;

out:
    if (fd >= 0) {
        close (fd);
    }
    g_clear_pointer (&checksum, g_checksum_free);

    if (!ok) {
        g_clear_pointer (out_sha256, g_free);
    }

    return ok;
}

static gboolean
oracle_collect_regular_files (
    const char *snapshot_root,
    const char *relative_directory,
    const OracleManifestPolicy *policy,
    GPtrArray *out_sources,
    GError **error
)
{
    char *absolute_directory =
        relative_directory[0] == '\0'
            ? g_strdup (snapshot_root)
            : g_build_filename (
                snapshot_root,
                relative_directory,
                NULL
            );
    GDir *directory =
        g_dir_open (absolute_directory, 0, error);

    if (directory == NULL) {
        g_free (absolute_directory);
        return FALSE;
    }

    const char *name;

    while ((name = g_dir_read_name (directory)) != NULL) {
        char *relative =
            relative_directory[0] == '\0'
                ? g_strdup (name)
                : g_build_filename (
                    relative_directory,
                    name,
                    NULL
                );
        char *absolute =
            g_build_filename (
                snapshot_root,
                relative,
                NULL
            );
        GStatBuf st;

        if (g_lstat (absolute, &st) != 0 ||
            S_ISLNK (st.st_mode) ||
            (!S_ISDIR (st.st_mode) &&
             !S_ISREG (st.st_mode))) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_SNAPSHOT,
                "Oracle found an unsafe snapshot entry: %s",
                relative
            );
            g_free (absolute);
            g_free (relative);
            g_dir_close (directory);
            g_free (absolute_directory);
            return FALSE;
        }

        if (S_ISDIR (st.st_mode)) {
            if (!oracle_collect_regular_files (
                    snapshot_root,
                    relative,
                    policy,
                    out_sources,
                    error
                )) {
                g_free (absolute);
                g_free (relative);
                g_dir_close (directory);
                g_free (absolute_directory);
                return FALSE;
            }
        } else if (!oracle_path_is_excluded (
                       policy,
                       relative
                   )) {
            guint roles =
                oracle_roles_for_path (
                    policy,
                    relative
                );

            if (roles != 0) {
                OracleExpectedSource *source =
                    g_new0 (
                        OracleExpectedSource,
                        1
                    );
                source->path = g_strdup (relative);
                source->roles = roles;
                source->media_type =
                    g_strdup (
                        oracle_media_type_for_path (
                            relative
                        )
                    );

                if (!oracle_hash_file (
                        absolute,
                        &source->sha256,
                        &source->byte_size,
                        error
                    )) {
                    oracle_expected_source_free (source);
                    g_free (absolute);
                    g_free (relative);
                    g_dir_close (directory);
                    g_free (absolute_directory);
                    return FALSE;
                }

                g_ptr_array_add (
                    out_sources,
                    source
                );
            }
        }

        g_free (absolute);
        g_free (relative);
    }

    g_dir_close (directory);
    g_free (absolute_directory);
    return TRUE;
}

static gboolean
load_json_object (
    const char *path,
    JsonParser **out_parser,
    JsonObject **out_object,
    GError **error
)
{
    char *contents = NULL;
    gsize length = 0;
    JsonParser *parser = NULL;
    GError *local_error = NULL;

    g_return_val_if_fail (out_parser != NULL, FALSE);
    g_return_val_if_fail (*out_parser == NULL, FALSE);
    g_return_val_if_fail (out_object != NULL, FALSE);
    g_return_val_if_fail (*out_object == NULL, FALSE);

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            &local_error
        )) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_STATE,
            "Could not read JSON file %s: %s",
            path,
            local_error != NULL ? local_error->message : "unknown error"
        );
        g_clear_error (&local_error);
        return FALSE;
    }

    parser = json_parser_new ();
    if (!json_parser_load_from_data (
            parser,
            contents,
            length,
            &local_error
        )) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_STATE,
            "Could not parse JSON file %s: %s",
            path,
            local_error != NULL ? local_error->message : "unknown error"
        );
        g_clear_error (&local_error);
        g_free (contents);
        g_object_unref (parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root (parser);
    if (root == NULL ||
        json_node_get_node_type (root) != JSON_NODE_OBJECT) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_STATE,
            "JSON root is not an object: %s",
            path
        );
        g_free (contents);
        g_object_unref (parser);
        return FALSE;
    }

    g_free (contents);
    *out_object = json_node_get_object (root);
    *out_parser = parser;
    return TRUE;
}

static gboolean
read_pinned_state (
    const OracleInput *input,
    OraclePinnedState *out_state,
    GError **error
)
{
    char *path = g_build_filename (
        input->state_root,
        "repository-state.json",
        NULL
    );
    JsonParser *parser = NULL;
    JsonObject *root = NULL;
    GHashTable *ids = NULL;
    gboolean found = FALSE;
    gboolean ok = FALSE;

    if (!load_json_object (
            path,
            &parser,
            &root,
            error
        )) {
        goto out;
    }

    JsonNode *schema_node =
        json_object_get_member (root, "schema_version");
    JsonNode *repositories_node =
        json_object_get_member (root, "repositories");

    if (!is_integer_node (schema_node) ||
        json_node_get_int (schema_node) !=
            ORACLE_STATE_SCHEMA_VERSION ||
        repositories_node == NULL ||
        json_node_get_node_type (repositories_node) !=
            JSON_NODE_ARRAY) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_STATE,
            "Repository state schema is invalid."
        );
        goto out;
    }

    ids = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );

    JsonArray *repositories =
        json_node_get_array (repositories_node);

    for (guint i = 0;
         i < json_array_get_length (repositories);
         i++) {
        JsonNode *item_node =
            json_array_get_element (repositories, i);

        if (item_node == NULL ||
            json_node_get_node_type (item_node) !=
                JSON_NODE_OBJECT) {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_STATE,
                "Repository state contains a non-object entry."
            );
            goto out;
        }

        JsonObject *item = json_node_get_object (item_node);
        JsonNode *id_node =
            json_object_get_member (item, "id");
        JsonNode *sha_node =
            json_object_get_member (item, "sha");
        JsonNode *version_node =
            json_object_get_member (item, "version");

        if (!is_nonempty_string_node (id_node) ||
            !is_nonempty_string_node (sha_node) ||
            !is_nonempty_string_node (version_node)) {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_STATE,
                "Repository state entry is incomplete."
            );
            goto out;
        }

        const char *id = json_node_get_string (id_node);
        const char *sha = json_node_get_string (sha_node);
        const char *version =
            json_node_get_string (version_node);

        if (!safe_component_is_valid (id) ||
            !lower_hex_is_valid (sha, 40)) {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_STATE,
                "Repository state identity is invalid."
            );
            goto out;
        }

        if (g_hash_table_contains (ids, id)) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_STATE,
                "Repository state contains duplicate id '%s'.",
                id
            );
            goto out;
        }

        g_hash_table_add (ids, g_strdup (id));

        if (g_strcmp0 (id, input->repository_id) == 0) {
            found = TRUE;
            out_state->sha = g_strdup (sha);
            out_state->version = g_strdup (version);
        }
    }

    if (!found) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_STATE,
            "Repository '%s' is not installed in persistent state.",
            input->repository_id
        );
        goto out;
    }

    ok = TRUE;

out:
    g_clear_pointer (&ids, g_hash_table_unref);
    g_clear_object (&parser);
    g_free (path);

    if (!ok) {
        oracle_pinned_state_clear (out_state);
    }

    return ok;
}

static gboolean
yaml_node_scalar_equals (
    yaml_node_t *node,
    const char *expected
)
{
    if (node == NULL || node->type != YAML_SCALAR_NODE) {
        return FALSE;
    }

    gsize expected_length = strlen (expected);
    return node->data.scalar.length == expected_length &&
        memcmp (
            node->data.scalar.value,
            expected,
            expected_length
        ) == 0;
}

static gboolean
read_cff_version_independently (
    const char *path,
    char **out_version,
    GError **error
)
{
    char *contents = NULL;
    gsize length = 0;
    yaml_parser_t yaml_parser;
    yaml_document_t document;
    yaml_document_t extra_document;
    gboolean parser_ready = FALSE;
    gboolean document_ready = FALSE;
    gboolean extra_ready = FALSE;
    char *version = NULL;
    gboolean ok = FALSE;

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            error
        )) {
        return FALSE;
    }

    if (!yaml_parser_initialize (&yaml_parser)) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "Oracle could not initialize the CFF YAML parser."
        );
        goto out;
    }

    parser_ready = TRUE;
    yaml_parser_set_input_string (
        &yaml_parser,
        (const unsigned char *) contents,
        length
    );

    if (!yaml_parser_load (&yaml_parser, &document)) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "Oracle could not parse CITATION.cff."
        );
        goto out;
    }

    document_ready = TRUE;
    yaml_node_t *root =
        yaml_document_get_root_node (&document);

    if (root == NULL || root->type != YAML_MAPPING_NODE) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "CITATION.cff root is not a YAML mapping."
        );
        goto out;
    }

    for (yaml_node_pair_t *pair = root->data.mapping.pairs.start;
         pair < root->data.mapping.pairs.top;
         pair++) {
        yaml_node_t *key =
            yaml_document_get_node (&document, pair->key);
        yaml_node_t *value =
            yaml_document_get_node (&document, pair->value);

        if (!yaml_node_scalar_equals (key, "version")) {
            continue;
        }

        if (version != NULL ||
            value == NULL ||
            value->type != YAML_SCALAR_NODE) {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_VERSION,
                "CITATION.cff must expose exactly one scalar top-level version."
            );
            goto out;
        }

        version = g_strndup (
            (const char *) value->data.scalar.value,
            value->data.scalar.length
        );
        g_strstrip (version);

        if (version[0] == '\0') {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_VERSION,
                "CITATION.cff version is empty."
            );
            goto out;
        }
    }

    if (version == NULL) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "CITATION.cff has no top-level version."
        );
        goto out;
    }

    if (!yaml_parser_load (&yaml_parser, &extra_document)) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "Oracle could not finish parsing CITATION.cff."
        );
        goto out;
    }

    extra_ready = TRUE;
    if (yaml_document_get_root_node (&extra_document) != NULL) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "CITATION.cff contains more than one YAML document."
        );
        goto out;
    }

    *out_version = g_steal_pointer (&version);
    ok = TRUE;

out:
    g_clear_pointer (&version, g_free);

    if (extra_ready) {
        yaml_document_delete (&extra_document);
    }
    if (document_ready) {
        yaml_document_delete (&document);
    }
    if (parser_ready) {
        yaml_parser_delete (&yaml_parser);
    }

    g_free (contents);
    return ok;
}

static gboolean
check_manifest_and_version (
    const OracleInput *input,
    const OraclePinnedState *state,
    const char *snapshot_root,
    char **out_manifest_hash,
    OracleManifestPolicy *out_policy,
    GError **error
)
{
    char *manifest_path =
        g_build_filename (
            snapshot_root,
            ".atm",
            "repository.json",
            NULL
        );
    char *citation_path =
        g_build_filename (
            snapshot_root,
            "CITATION.cff",
            NULL
        );
    char *status_path =
        g_build_filename (
            snapshot_root,
            "STATUS.md",
            NULL
        );
    char *manifest_data = NULL;
    gsize manifest_length = 0;
    JsonParser *parser = NULL;
    JsonObject *root = NULL;
    char *cff_version = NULL;
    gboolean ok = FALSE;

    if (!path_beneath_root_is_safe (
            snapshot_root,
            ".atm/repository.json",
            FALSE,
            error
        ) ||
        !path_beneath_root_is_safe (
            snapshot_root,
            "CITATION.cff",
            FALSE,
            error
        ) ||
        !path_beneath_root_is_safe (
            snapshot_root,
            "STATUS.md",
            FALSE,
            error
        )) {
        goto out;
    }

    if (!g_file_get_contents (
            manifest_path,
            &manifest_data,
            &manifest_length,
            error
        )) {
        goto out;
    }

    parser = json_parser_new ();
    if (!json_parser_load_from_data (
            parser,
            manifest_data,
            manifest_length,
            error
        )) {
        goto out;
    }

    JsonNode *root_node = json_parser_get_root (parser);
    if (root_node == NULL ||
        json_node_get_node_type (root_node) != JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Repository manifest root is not an object."
        );
        goto out;
    }

    root = json_node_get_object (root_node);
    JsonNode *schema_node =
        json_object_get_member (root, "schema_version");
    JsonNode *id_node =
        json_object_get_member (root, "repository_id");
    JsonNode *acronym_node =
        json_object_get_member (root, "acronym");
    JsonNode *display_node =
        json_object_get_member (root, "display_name");
    JsonNode *version_source_node =
        json_object_get_member (root, "version_source");
    JsonNode *status_source_node =
        json_object_get_member (root, "status_source");

    if (!is_integer_node (schema_node) ||
        json_node_get_int (schema_node) !=
            ORACLE_MANIFEST_SCHEMA_VERSION ||
        !is_nonempty_string_node (id_node) ||
        !is_nonempty_string_node (acronym_node) ||
        !is_nonempty_string_node (display_node) ||
        !is_nonempty_string_node (status_source_node) ||
        g_strcmp0 (
            json_node_get_string (id_node),
            input->repository_id
        ) != 0 ||
        g_strcmp0 (
            json_node_get_string (acronym_node),
            input->acronym
        ) != 0 ||
        g_strcmp0 (
            json_node_get_string (display_node),
            input->display_name
        ) != 0 ||
        g_strcmp0 (
            json_node_get_string (status_source_node),
            "STATUS.md"
        ) != 0) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Repository manifest identity or schema does not match oracle input."
        );
        goto out;
    }

    if (version_source_node == NULL ||
        json_node_get_node_type (version_source_node) !=
            JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Repository manifest version_source is invalid."
        );
        goto out;
    }

    JsonObject *version_source =
        json_node_get_object (version_source_node);
    JsonNode *type_node =
        json_object_get_member (version_source, "type");
    JsonNode *path_node =
        json_object_get_member (version_source, "path");

    if (!is_nonempty_string_node (type_node) ||
        !is_nonempty_string_node (path_node) ||
        g_strcmp0 (
            json_node_get_string (type_node),
            "cff"
        ) != 0 ||
        g_strcmp0 (
            json_node_get_string (path_node),
            "CITATION.cff"
        ) != 0) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Repository manifest version source does not match the oracle contract."
        );
        goto out;
    }

    JsonNode *required_node =
        json_object_get_member (root, "required_paths");
    JsonNode *retrieval_node =
        json_object_get_member (root, "retrieval");

    if (required_node == NULL ||
        json_node_get_node_type (required_node) !=
            JSON_NODE_ARRAY ||
        retrieval_node == NULL ||
        json_node_get_node_type (retrieval_node) !=
            JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Repository manifest required_paths/retrieval shape is invalid."
        );
        goto out;
    }

    GPtrArray *required_paths = NULL;
    if (!copy_manifest_path_array (
            root,
            "required_paths",
            snapshot_root,
            TRUE,
            &required_paths,
            error
        )) {
        goto out;
    }
    g_ptr_array_unref (required_paths);

    JsonObject *retrieval =
        json_node_get_object (retrieval_node);
    out_policy->status_source =
        g_strdup (
            json_node_get_string (
                status_source_node
            )
        );

    if (!copy_manifest_path_array (
            retrieval,
            "canonical",
            snapshot_root,
            TRUE,
            &out_policy->canonical,
            error
        ) ||
        !copy_manifest_path_array (
            retrieval,
            "structural",
            snapshot_root,
            TRUE,
            &out_policy->structural,
            error
        ) ||
        !copy_manifest_path_array (
            retrieval,
            "evidence",
            snapshot_root,
            TRUE,
            &out_policy->evidence,
            error
        ) ||
        !copy_manifest_path_array (
            retrieval,
            "tabular",
            snapshot_root,
            TRUE,
            &out_policy->tabular,
            error
        ) ||
        !copy_manifest_path_array (
            retrieval,
            "implementation",
            snapshot_root,
            TRUE,
            &out_policy->implementation,
            error
        ) ||
        !copy_manifest_path_array (
            retrieval,
            "exclude",
            snapshot_root,
            FALSE,
            &out_policy->exclude,
            error
        )) {
        goto out;
    }

    if (!read_cff_version_independently (
            citation_path,
            &cff_version,
            error
        )) {
        goto out;
    }

    if (g_strcmp0 (cff_version, state->version) != 0) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_VERSION,
            "Persistent version '%s' differs from CITATION.cff version '%s'.",
            state->version,
            cff_version
        );
        goto out;
    }

    *out_manifest_hash =
        g_compute_checksum_for_data (
            G_CHECKSUM_SHA256,
            (const guchar *) manifest_data,
            manifest_length
        );

    if (*out_manifest_hash == NULL) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_MANIFEST,
            "Oracle could not compute manifest SHA-256."
        );
        goto out;
    }

    ok = TRUE;

out:
    if (!ok) {
        g_clear_pointer (out_manifest_hash, g_free);
        oracle_manifest_policy_clear (out_policy);
    }

    g_clear_pointer (&cff_version, g_free);
    g_clear_object (&parser);
    g_free (manifest_data);
    g_free (status_path);
    g_free (citation_path);
    g_free (manifest_path);
    return ok;
}

static gboolean
sqlite_single_int (
    sqlite3 *db,
    const char *sql,
    gint64 *out_value,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;
    int rc = sqlite3_prepare_v2 (
        db,
        sql,
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Could not prepare SQLite oracle query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    rc = sqlite3_step (statement);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "SQLite oracle query returned no row."
        );
        return FALSE;
    }

    *out_value = sqlite3_column_int64 (statement, 0);

    if (sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "SQLite oracle scalar query returned multiple rows."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    return TRUE;
}

static gboolean
check_sqlite_integrity (sqlite3 *db, GError **error)
{
    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA integrity_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Could not prepare PRAGMA integrity_check: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    if (sqlite3_step (statement) != SQLITE_ROW ||
        g_strcmp0 (
            (const char *) sqlite3_column_text (statement, 0),
            "ok"
        ) != 0 ||
        sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "PRAGMA integrity_check did not return exactly one 'ok' row."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "PRAGMA foreign_key_check;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Could not prepare PRAGMA foreign_key_check: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    if (sqlite3_step (statement) != SQLITE_DONE) {
        sqlite3_finalize (statement);
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "PRAGMA foreign_key_check reported a violation."
        );
        return FALSE;
    }

    sqlite3_finalize (statement);
    return TRUE;
}

static guint
oracle_role_bit_from_name (const char *role)
{
    if (g_strcmp0 (role, "status") == 0) {
        return ORACLE_ROLE_STATUS;
    }
    if (g_strcmp0 (role, "canonical") == 0) {
        return ORACLE_ROLE_CANONICAL;
    }
    if (g_strcmp0 (role, "structural") == 0) {
        return ORACLE_ROLE_STRUCTURAL;
    }
    if (g_strcmp0 (role, "evidence") == 0) {
        return ORACLE_ROLE_EVIDENCE;
    }
    if (g_strcmp0 (role, "tabular") == 0) {
        return ORACLE_ROLE_TABULAR;
    }
    if (g_strcmp0 (role, "implementation") == 0) {
        return ORACLE_ROLE_IMPLEMENTATION;
    }

    return 0;
}

static gboolean
oracle_check_source_roles (
    sqlite3 *db,
    sqlite3_int64 source_id,
    guint expected_roles,
    GError **error
)
{
    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2 (
            db,
            "SELECT role FROM source_roles "
            "WHERE source_id = ?1 ORDER BY role;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Oracle could not prepare source-role query: %s",
            sqlite3_errmsg (db)
        );
        return FALSE;
    }

    sqlite3_bind_int64 (
        statement,
        1,
        source_id
    );

    guint actual_roles = 0;
    int rc;

    while ((rc = sqlite3_step (statement)) == SQLITE_ROW) {
        const char *role =
            (const char *) sqlite3_column_text (
                statement,
                0
            );
        guint bit =
            oracle_role_bit_from_name (role);

        if (bit == 0 || (actual_roles & bit) != 0) {
            sqlite3_finalize (statement);
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_INTEGRITY,
                "Oracle found an invalid or duplicate source role."
            );
            return FALSE;
        }

        actual_roles |= bit;
    }

    sqlite3_finalize (statement);

    if (rc != SQLITE_DONE ||
        actual_roles != expected_roles) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "Retrieval-index source roles differ from the independently derived manifest roles."
        );
        return FALSE;
    }

    return TRUE;
}

static gboolean
oracle_check_source_provenance (
    sqlite3 *db,
    const OracleInput *input,
    const char *snapshot_root,
    const OracleManifestPolicy *policy,
    GError **error
)
{
    GPtrArray *expected =
        g_ptr_array_new_with_free_func (
            (GDestroyNotify) oracle_expected_source_free
        );
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    if (!oracle_collect_regular_files (
            snapshot_root,
            "",
            policy,
            expected,
            error
        )) {
        goto out;
    }

    g_ptr_array_sort (
        expected,
        compare_expected_source_paths
    );

    if (sqlite3_prepare_v2 (
            db,
            "SELECT id, path, sha256, byte_size, media_type, logical_source_id "
            "FROM source_files ORDER BY path;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Oracle could not prepare source provenance query: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    for (guint i = 0; i < expected->len; i++) {
        OracleExpectedSource *source =
            g_ptr_array_index (expected, i);

        if (sqlite3_step (statement) != SQLITE_ROW) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_INTEGRITY,
                "Retrieval index is missing independently derived source '%s'.",
                source->path
            );
            goto out;
        }

        sqlite3_int64 source_id =
            sqlite3_column_int64 (statement, 0);
        const char *path =
            (const char *) sqlite3_column_text (
                statement,
                1
            );
        const char *sha256 =
            (const char *) sqlite3_column_text (
                statement,
                2
            );
        sqlite3_int64 byte_size =
            sqlite3_column_int64 (
                statement,
                3
            );
        const char *media_type =
            (const char *) sqlite3_column_text (
                statement,
                4
            );
        const char *logical_source_id =
            (const char *) sqlite3_column_text (
                statement,
                5
            );
        char *expected_logical_source_id =
            g_strdup_printf (
                "%s:file:%s",
                input->repository_id,
                source->path
            );

        gboolean row_matches =
            g_strcmp0 (path, source->path) == 0 &&
            g_strcmp0 (sha256, source->sha256) == 0 &&
            byte_size ==
                (sqlite3_int64) source->byte_size &&
            g_strcmp0 (
                media_type,
                source->media_type
            ) == 0 &&
            g_strcmp0 (
                logical_source_id,
                expected_logical_source_id
            ) == 0;

        g_free (expected_logical_source_id);

        if (!row_matches) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_INTEGRITY,
                "Retrieval-index provenance differs from exact snapshot source '%s'.",
                source->path
            );
            goto out;
        }

        if (!oracle_check_source_roles (
                db,
                source_id,
                source->roles,
                error
            )) {
            goto out;
        }
    }

    if (sqlite3_step (statement) != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "Retrieval index contains source files not selected by the independent manifest policy."
        );
        goto out;
    }

    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }
    g_ptr_array_unref (expected);
    return ok;
}

static gboolean
check_index (
    const OracleInput *input,
    const OraclePinnedState *state,
    const char *snapshot_root,
    const char *manifest_hash,
    const OracleManifestPolicy *policy,
    GError **error
)
{
    char *filename =
        g_strdup_printf ("%s.sqlite", state->sha);
    char *relative =
        g_build_filename (
            "retrieval",
            input->repository_id,
            filename,
            NULL
        );
    char *index_path =
        g_build_filename (
            input->cache_root,
            relative,
            NULL
        );
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;
    gint64 value = 0;

    if (!path_beneath_root_is_safe (
            input->cache_root,
            relative,
            FALSE,
            error
        )) {
        goto out;
    }

    if (sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Could not open retrieval index read-only: %s",
            db != NULL ? sqlite3_errmsg (db) : "unknown SQLite error"
        );
        goto out;
    }

    if (sqlite3_exec (
            db,
            "PRAGMA query_only = ON;"
            "PRAGMA trusted_schema = OFF;",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Could not configure read-only oracle connection: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    if (!sqlite_single_int (
            db,
            "PRAGMA user_version;",
            &value,
            error
        ) ||
        value != ORACLE_RETRIEVAL_SCHEMA_VERSION) {
        if (error != NULL && *error == NULL) {
            g_set_error (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_INDEX,
                "Retrieval index user_version is %" G_GINT64_FORMAT
                ", expected %d.",
                value,
                ORACLE_RETRIEVAL_SCHEMA_VERSION
            );
        }
        goto out;
    }

    if (!check_sqlite_integrity (db, error)) {
        goto out;
    }

    if (!sqlite_single_int (
            db,
            "SELECT count(*) FROM snapshot_metadata;",
            &value,
            error
        ) ||
        value != 1) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                ORACLE_ERROR,
                ORACLE_ERROR_INTEGRITY,
                "snapshot_metadata is not an exact singleton."
            );
        }
        goto out;
    }

    if (sqlite3_prepare_v2 (
            db,
            "SELECT id, repository_id, repository_version, "
            "snapshot_sha, manifest_schema_version, manifest_sha256 "
            "FROM snapshot_metadata;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        g_set_error (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INDEX,
            "Could not prepare snapshot metadata oracle query: %s",
            sqlite3_errmsg (db)
        );
        goto out;
    }

    if (sqlite3_step (statement) != SQLITE_ROW) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "snapshot_metadata singleton row is missing."
        );
        goto out;
    }

    gint64 id = sqlite3_column_int64 (statement, 0);
    const char *repository_id =
        (const char *) sqlite3_column_text (statement, 1);
    const char *repository_version =
        (const char *) sqlite3_column_text (statement, 2);
    const char *snapshot_sha =
        (const char *) sqlite3_column_text (statement, 3);
    gint64 manifest_schema_version =
        sqlite3_column_int64 (statement, 4);
    const char *stored_manifest_hash =
        (const char *) sqlite3_column_text (statement, 5);

    if (id != 1 ||
        g_strcmp0 (
            repository_id,
            input->repository_id
        ) != 0 ||
        g_strcmp0 (
            repository_version,
            state->version
        ) != 0 ||
        g_strcmp0 (
            snapshot_sha,
            state->sha
        ) != 0 ||
        manifest_schema_version !=
            ORACLE_MANIFEST_SCHEMA_VERSION ||
        g_strcmp0 (
            stored_manifest_hash,
            manifest_hash
        ) != 0 ||
        sqlite3_step (statement) != SQLITE_DONE) {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_INTEGRITY,
            "Retrieval index metadata does not match the exact pinned generation."
        );
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (!oracle_check_source_provenance (
            db,
            input,
            snapshot_root,
            policy,
            error
        )) {
        goto out;
    }

    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }
    if (db != NULL) {
        sqlite3_close (db);
    }

    g_free (index_path);
    g_free (relative);
    g_free (filename);
    return ok;
}

static gboolean
oracle_check (
    const OracleInput *input,
    OraclePinnedState *out_state,
    GError **error
)
{
    OraclePinnedState state = { 0 };
    char *snapshot_relative = NULL;
    char *snapshot_root = NULL;
    char *manifest_hash = NULL;
    OracleManifestPolicy policy = { 0 };
    gboolean ok = FALSE;

    if (input == NULL ||
        !safe_component_is_valid (input->repository_id) ||
        input->acronym == NULL ||
        input->acronym[0] == '\0' ||
        input->display_name == NULL ||
        input->display_name[0] == '\0') {
        g_set_error_literal (
            error,
            ORACLE_ERROR,
            ORACLE_ERROR_ARGUMENT,
            "Independent oracle input is incomplete."
        );
        return FALSE;
    }

    if (!read_pinned_state (
            input,
            &state,
            error
        )) {
        goto out;
    }

    snapshot_relative = g_build_filename (
        "Repositories",
        input->repository_id,
        "snapshots",
        state.sha,
        NULL
    );
    snapshot_root = g_build_filename (
        input->data_root,
        snapshot_relative,
        NULL
    );

    if (!path_beneath_root_is_safe (
            input->data_root,
            snapshot_relative,
            TRUE,
            error
        )) {
        goto out;
    }

    if (!check_manifest_and_version (
            input,
            &state,
            snapshot_root,
            &manifest_hash,
            &policy,
            error
        )) {
        goto out;
    }

    if (!check_index (
            input,
            &state,
            snapshot_root,
            manifest_hash,
            &policy,
            error
        )) {
        goto out;
    }

    if (out_state != NULL) {
        out_state->sha = g_strdup (state.sha);
        out_state->version = g_strdup (state.version);
    }

    ok = TRUE;

out:
    oracle_manifest_policy_clear (&policy);
    g_free (manifest_hash);
    g_free (snapshot_root);
    g_free (snapshot_relative);
    oracle_pinned_state_clear (&state);
    return ok;
}

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf st;

    if (g_lstat (path, &st) != 0) {
        return;
    }

    if (S_ISDIR (st.st_mode) && !S_ISLNK (st.st_mode)) {
        GError *error = NULL;
        GDir *dir = g_dir_open (path, 0, &error);

        if (dir != NULL) {
            const char *name;

            while ((name = g_dir_read_name (dir)) != NULL) {
                char *child = g_build_filename (
                    path,
                    name,
                    NULL
                );
                remove_tree_best_effort (child);
                g_free (child);
            }

            g_dir_close (dir);
        }

        g_clear_error (&error);
        g_rmdir (path);
    } else {
        g_remove (path);
    }
}

static gboolean
write_text_file (
    const char *path,
    const char *contents
)
{
    char *parent = g_path_get_dirname (path);
    GError *error = NULL;
    gboolean ok =
        g_mkdir_with_parents (parent, 0700) == 0 &&
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        );

    if (!ok && error != NULL) {
        g_printerr ("fixture write failed: %s\n", error->message);
    }

    g_clear_error (&error);
    g_free (parent);
    return ok;
}

static gboolean
fixture_hash_file (
    const char *path,
    char **out_sha256,
    gint64 *out_size
)
{
    char *contents = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            &error
        )) {
        g_clear_error (&error);
        return FALSE;
    }

    *out_sha256 =
        g_compute_checksum_for_data (
            G_CHECKSUM_SHA256,
            (const guchar *) contents,
            length
        );
    *out_size = (gint64) length;
    g_free (contents);
    return *out_sha256 != NULL;
}

static gboolean
fixture_insert_source (
    sqlite3 *db,
    const char *snapshot_root,
    const char *repository_id,
    const char *relative_path,
    const char *media_type,
    const char *role_a,
    const char *role_b
)
{
    char *absolute =
        g_build_filename (
            snapshot_root,
            relative_path,
            NULL
        );
    char *sha256 = NULL;
    gint64 byte_size = 0;
    char *logical_source_id =
        g_strdup_printf (
            "%s:file:%s",
            repository_id,
            relative_path
        );
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    if (!fixture_hash_file (
            absolute,
            &sha256,
            &byte_size
        )) {
        goto out;
    }

    if (sqlite3_prepare_v2 (
            db,
            "INSERT INTO source_files("
            "path,sha256,byte_size,media_type,logical_source_id"
            ") VALUES(?1,?2,?3,?4,?5);",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        goto out;
    }

    sqlite3_bind_text (
        statement, 1, relative_path, -1, SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement, 2, sha256, -1, SQLITE_TRANSIENT
    );
    sqlite3_bind_int64 (
        statement, 3, byte_size
    );
    sqlite3_bind_text (
        statement, 4, media_type, -1, SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement, 5, logical_source_id, -1, SQLITE_TRANSIENT
    );

    if (sqlite3_step (statement) != SQLITE_DONE) {
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;
    sqlite3_int64 source_id =
        sqlite3_last_insert_rowid (db);

    const char *roles[2] = { role_a, role_b };

    for (guint i = 0; i < 2; i++) {
        if (roles[i] == NULL) {
            continue;
        }

        if (sqlite3_prepare_v2 (
                db,
                "INSERT INTO source_roles(source_id,role) "
                "VALUES(?1,?2);",
                -1,
                &statement,
                NULL
            ) != SQLITE_OK) {
            goto out;
        }

        sqlite3_bind_int64 (
            statement, 1, source_id
        );
        sqlite3_bind_text (
            statement, 2, roles[i], -1, SQLITE_STATIC
        );

        if (sqlite3_step (statement) != SQLITE_DONE) {
            goto out;
        }

        sqlite3_finalize (statement);
        statement = NULL;
    }

    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }
    g_free (logical_source_id);
    g_free (sha256);
    g_free (absolute);
    return ok;
}

static gboolean
create_fixture_index (
    const char *path,
    const char *snapshot_root,
    const char *repository_id,
    const char *version,
    const char *sha,
    const char *manifest_hash
)
{
    char *parent = g_path_get_dirname (path);
    sqlite3 *db = NULL;
    gboolean ok = FALSE;

    if (g_mkdir_with_parents (parent, 0700) != 0 ||
        sqlite3_open_v2 (
            path,
            &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            NULL
        ) != SQLITE_OK) {
        goto out;
    }

    const char *schema =
        "PRAGMA user_version = 2;"
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE snapshot_metadata("
        "id INTEGER PRIMARY KEY CHECK(id=1),"
        "repository_id TEXT NOT NULL,"
        "repository_version TEXT NOT NULL,"
        "snapshot_sha TEXT NOT NULL,"
        "manifest_schema_version INTEGER NOT NULL,"
        "manifest_sha256 TEXT NOT NULL,"
        "created_at_utc TEXT NOT NULL"
        ");"
        "CREATE TABLE source_files("
        "id INTEGER PRIMARY KEY,"
        "path TEXT NOT NULL UNIQUE,"
        "sha256 TEXT NOT NULL,"
        "byte_size INTEGER NOT NULL CHECK(byte_size >= 0),"
        "media_type TEXT NOT NULL,"
        "logical_source_id TEXT NOT NULL UNIQUE"
        ");"
        "CREATE TABLE source_roles("
        "source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,"
        "role TEXT NOT NULL,"
        "PRIMARY KEY(source_id,role)"
        ");";

    if (sqlite3_exec (
            db,
            schema,
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        goto out;
    }

    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2 (
            db,
            "INSERT INTO snapshot_metadata("
            "id,repository_id,repository_version,snapshot_sha,"
            "manifest_schema_version,manifest_sha256,created_at_utc"
            ") VALUES(1,?1,?2,?3,1,?4,'2026-09-22T00:00:00Z');",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        goto out;
    }

    sqlite3_bind_text (
        statement, 1, repository_id, -1, SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement, 2, version, -1, SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement, 3, sha, -1, SQLITE_STATIC
    );
    sqlite3_bind_text (
        statement, 4, manifest_hash, -1, SQLITE_STATIC
    );

    ok = sqlite3_step (statement) == SQLITE_DONE;
    sqlite3_finalize (statement);
    statement = NULL;

    if (!ok ||
        !fixture_insert_source (
            db,
            snapshot_root,
            repository_id,
            "CITATION.cff",
            "text/yaml",
            "canonical",
            NULL
        ) ||
        !fixture_insert_source (
            db,
            snapshot_root,
            repository_id,
            "STATUS.md",
            "text/markdown",
            "status",
            "canonical"
        )) {
        ok = FALSE;
        goto out;
    }

    ok = TRUE;

out:
    if (db != NULL) {
        sqlite3_close (db);
    }
    g_free (parent);
    return ok;
}

static gboolean
run_self_test (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-independent-oracle-XXXXXX",
        &error
    );

    if (root == NULL) {
        g_printerr (
            "oracle self-test tmpdir failed: %s\n",
            error != NULL ? error->message : "unknown error"
        );
        g_clear_error (&error);
        return FALSE;
    }

    char *data_root = g_build_filename (
        root,
        "data",
        NULL
    );
    char *cache_root = g_build_filename (
        root,
        "cache",
        NULL
    );
    char *state_root = g_build_filename (
        root,
        "state",
        NULL
    );
    const char *sha =
        "0123456789abcdef0123456789abcdef01234567";
    const char *version = "0.1.0";

    char *snapshot = g_build_filename (
        data_root,
        "Repositories",
        "ewd",
        "snapshots",
        sha,
        NULL
    );
    char *manifest_path = g_build_filename (
        snapshot,
        ".atm",
        "repository.json",
        NULL
    );
    char *citation_path = g_build_filename (
        snapshot,
        "CITATION.cff",
        NULL
    );
    char *status_path = g_build_filename (
        snapshot,
        "STATUS.md",
        NULL
    );
    char *state_path = g_build_filename (
        state_root,
        "repository-state.json",
        NULL
    );
    char *index_path = g_build_filename (
        cache_root,
        "retrieval",
        "ewd",
        "0123456789abcdef0123456789abcdef01234567.sqlite",
        NULL
    );

    const char *manifest =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"acronym\": \"EWD\",\n"
        "  \"display_name\": \"Empirical World3 Dynamics\",\n"
        "  \"version_source\": {\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": [\"CITATION.cff\", \"STATUS.md\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\", \"CITATION.cff\"],\n"
        "    \"structural\": [],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n";

    const char *citation =
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Oracle fixture\n"
        "version: 0.1.0\n";

    char *state_json = g_strdup_printf (
        "{\"schema_version\":1,\"repositories\":["
        "{\"id\":\"ewd\",\"sha\":\"%s\",\"version\":\"%s\"}]}",
        sha,
        version
    );

    gboolean fixture_ok =
        g_mkdir_with_parents (data_root, 0700) == 0 &&
        g_mkdir_with_parents (cache_root, 0700) == 0 &&
        g_mkdir_with_parents (state_root, 0700) == 0 &&
        write_text_file (manifest_path, manifest) &&
        write_text_file (citation_path, citation) &&
        write_text_file (status_path, "# Status\n") &&
        write_text_file (state_path, state_json);

    g_free (state_json);

    char *manifest_hash =
        g_compute_checksum_for_string (
            G_CHECKSUM_SHA256,
            manifest,
            -1
        );

    fixture_ok =
        fixture_ok &&
        create_fixture_index (
            index_path,
            snapshot,
            "ewd",
            version,
            sha,
            manifest_hash
        );

    OracleInput input = {
        .data_root = data_root,
        .cache_root = cache_root,
        .state_root = state_root,
        .repository_id = "ewd",
        .acronym = "EWD",
        .display_name = "Empirical World3 Dynamics"
    };

    OraclePinnedState result = { 0 };
    gboolean valid = fixture_ok &&
        oracle_check (&input, &result, &error);

    if (!valid) {
        g_printerr (
            "oracle valid fixture failed: %s\n",
            error != NULL ? error->message : "fixture setup failed"
        );
        g_clear_error (&error);
        goto out;
    }

    if (g_strcmp0 (result.sha, sha) != 0 ||
        g_strcmp0 (result.version, version) != 0) {
        g_printerr ("oracle valid fixture returned wrong identity\n");
        valid = FALSE;
        goto out;
    }

    oracle_pinned_state_clear (&result);

    char *wrong_state = g_strdup_printf (
        "{\"schema_version\":1,\"repositories\":["
        "{\"id\":\"ewd\",\"sha\":\"%s\",\"version\":\"9.9.9\"}]}",
        sha
    );
    if (!write_text_file (state_path, wrong_state)) {
        g_free (wrong_state);
        valid = FALSE;
        goto out;
    }
    g_free (wrong_state);

    if (oracle_check (&input, NULL, &error)) {
        g_printerr ("oracle accepted state/CFF version divergence\n");
        valid = FALSE;
        goto out;
    }
    g_clear_error (&error);

    state_json = g_strdup_printf (
        "{\"schema_version\":1,\"repositories\":["
        "{\"id\":\"ewd\",\"sha\":\"%s\",\"version\":\"%s\"}]}",
        sha,
        version
    );
    if (!write_text_file (state_path, state_json)) {
        g_free (state_json);
        valid = FALSE;
        goto out;
    }
    g_free (state_json);

    sqlite3 *db = NULL;
    if (sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ) != SQLITE_OK ||
        sqlite3_exec (
            db,
            "UPDATE snapshot_metadata "
            "SET snapshot_sha='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close (db);
        }
        valid = FALSE;
        goto out;
    }
    sqlite3_close (db);

    if (oracle_check (&input, NULL, &error)) {
        g_printerr ("oracle accepted index/state SHA divergence\n");
        valid = FALSE;
        goto out;
    }
    g_clear_error (&error);

    if (sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ) != SQLITE_OK ||
        sqlite3_exec (
            db,
            "UPDATE snapshot_metadata "
            "SET snapshot_sha='0123456789abcdef0123456789abcdef01234567';",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close (db);
        }
        valid = FALSE;
        goto out;
    }
    sqlite3_close (db);

    if (sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ) != SQLITE_OK ||
        sqlite3_exec (
            db,
            "UPDATE source_files "
            "SET sha256='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' "
            "WHERE path='STATUS.md';",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close (db);
        }
        valid = FALSE;
        goto out;
    }
    sqlite3_close (db);

    if (oracle_check (&input, NULL, &error)) {
        g_printerr ("oracle accepted source SHA-256 divergence\n");
        valid = FALSE;
        goto out;
    }
    g_clear_error (&error);

    char *status_hash =
        g_compute_checksum_for_string (
            G_CHECKSUM_SHA256,
            "# Status\n",
            -1
        );
    char *restore_status_hash =
        sqlite3_mprintf (
            "UPDATE source_files SET sha256='%q' "
            "WHERE path='STATUS.md';",
            status_hash
        );

    if (sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ) != SQLITE_OK ||
        sqlite3_exec (
            db,
            restore_status_hash,
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK ||
        sqlite3_exec (
            db,
            "DELETE FROM source_roles "
            "WHERE role='canonical' AND source_id=("
            "SELECT id FROM source_files WHERE path='STATUS.md'"
            ");",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close (db);
        }
        sqlite3_free (restore_status_hash);
        g_free (status_hash);
        valid = FALSE;
        goto out;
    }
    sqlite3_close (db);
    sqlite3_free (restore_status_hash);
    g_free (status_hash);

    if (oracle_check (&input, NULL, &error)) {
        g_printerr ("oracle accepted source-role divergence\n");
        valid = FALSE;
        goto out;
    }
    g_clear_error (&error);

    if (sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ) != SQLITE_OK ||
        sqlite3_exec (
            db,
            "INSERT INTO source_roles(source_id,role) "
            "SELECT id,'canonical' FROM source_files "
            "WHERE path='STATUS.md';",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close (db);
        }
        valid = FALSE;
        goto out;
    }
    sqlite3_close (db);

    if (!oracle_check (&input, NULL, &error)) {
        g_printerr (
            "oracle did not recover after provenance fixture restoration: %s\n",
            error != NULL ? error->message : "unknown error"
        );
        g_clear_error (&error);
        valid = FALSE;
        goto out;
    }

    if (!write_text_file (
            manifest_path,
            "{\n"
            "  \"schema_version\": 1,\n"
            "  \"repository_id\": \"ewd\",\n"
            "  \"acronym\": \"EWD\",\n"
            "  \"display_name\": \"Empirical World3 Dynamics\",\n"
            "  \"version_source\": {\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
            "  \"status_source\": \"STATUS.md\",\n"
            "  \"required_paths\": [\"CITATION.cff\", \"STATUS.md\"],\n"
            "  \"retrieval\": {\"canonical\": [\"STATUS.md\"], \"structural\": [], \"evidence\": [], \"tabular\": [], \"implementation\": [], \"exclude\": []}\n"
            "}\n"
        )) {
        valid = FALSE;
        goto out;
    }

    if (oracle_check (&input, NULL, &error)) {
        g_printerr ("oracle accepted manifest hash divergence\n");
        valid = FALSE;
        goto out;
    }
    g_clear_error (&error);

out:
    oracle_pinned_state_clear (&result);
    g_clear_error (&error);
    g_free (manifest_hash);
    g_free (index_path);
    g_free (state_path);
    g_free (status_path);
    g_free (citation_path);
    g_free (manifest_path);
    g_free (snapshot);
    g_free (state_root);
    g_free (cache_root);
    g_free (data_root);
    remove_tree_best_effort (root);
    g_free (root);
    return valid;
}

int
main (int argc, char **argv)
{
    gboolean self_test = FALSE;
    char *data_root = NULL;
    char *cache_root = NULL;
    char *state_root = NULL;
    char *repository_id = NULL;
    char *acronym = NULL;
    char *display_name = NULL;

    GOptionEntry entries[] = {
        {
            "self-test",
            0,
            0,
            G_OPTION_ARG_NONE,
            &self_test,
            "Run independent oracle core regression fixtures",
            NULL
        },
        {
            "data-root",
            0,
            0,
            G_OPTION_ARG_STRING,
            &data_root,
            "Repository data root",
            "PATH"
        },
        {
            "cache-root",
            0,
            0,
            G_OPTION_ARG_STRING,
            &cache_root,
            "Repository cache root",
            "PATH"
        },
        {
            "state-root",
            0,
            0,
            G_OPTION_ARG_STRING,
            &state_root,
            "Repository state root",
            "PATH"
        },
        {
            "repository-id",
            0,
            0,
            G_OPTION_ARG_STRING,
            &repository_id,
            "Expected repository id",
            "ID"
        },
        {
            "acronym",
            0,
            0,
            G_OPTION_ARG_STRING,
            &acronym,
            "Expected repository acronym",
            "TEXT"
        },
        {
            "display-name",
            0,
            0,
            G_OPTION_ARG_STRING,
            &display_name,
            "Expected repository display name",
            "TEXT"
        },
        { NULL }
    };

    GOptionContext *context = g_option_context_new (
        "- independent AtM repository state oracle"
    );
    g_option_context_add_main_entries (
        context,
        entries,
        NULL
    );

    GError *error = NULL;
    if (!g_option_context_parse (
            context,
            &argc,
            &argv,
            &error
        )) {
        g_printerr (
            "oracle argument error: %s\n",
            error->message
        );
        g_clear_error (&error);
        g_option_context_free (context);
        return 2;
    }

    g_option_context_free (context);

    if (self_test) {
        gboolean ok = run_self_test ();
        g_free (display_name);
        g_free (acronym);
        g_free (repository_id);
        g_free (state_root);
        g_free (cache_root);
        g_free (data_root);
        return ok ? 0 : 1;
    }

    OracleInput input = {
        .data_root = data_root,
        .cache_root = cache_root,
        .state_root = state_root,
        .repository_id = repository_id,
        .acronym = acronym,
        .display_name = display_name
    };

    if (data_root == NULL ||
        cache_root == NULL ||
        state_root == NULL ||
        repository_id == NULL ||
        acronym == NULL ||
        display_name == NULL) {
        g_printerr (
            "oracle requires --data-root, --cache-root, --state-root, "
            "--repository-id, --acronym and --display-name\n"
        );
        g_free (display_name);
        g_free (acronym);
        g_free (repository_id);
        g_free (state_root);
        g_free (cache_root);
        g_free (data_root);
        return 2;
    }

    OraclePinnedState result = { 0 };
    gboolean ok = oracle_check (
        &input,
        &result,
        &error
    );

    if (ok) {
        g_print (
            "ORACLE_READY id=%s sha=%s version=%s\n",
            repository_id,
            result.sha,
            result.version
        );
    } else {
        g_printerr (
            "ORACLE_REJECT id=%s reason=%s\n",
            repository_id,
            error != NULL ? error->message : "unknown error"
        );
    }

    oracle_pinned_state_clear (&result);
    g_clear_error (&error);
    g_free (display_name);
    g_free (acronym);
    g_free (repository_id);
    g_free (state_root);
    g_free (cache_root);
    g_free (data_root);
    return ok ? 0 : 1;
}
