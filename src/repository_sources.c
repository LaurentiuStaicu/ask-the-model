#include "repository_sources.h"

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ATM_HASH_BUFFER_BYTES (64 * 1024)

typedef struct {
    JsonArray *canonical;
    JsonArray *structural;
    JsonArray *evidence;
    JsonArray *tabular;
    JsonArray *implementation;
    JsonArray *exclude;
} RetrievalPaths;

GQuark
atm_source_catalog_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-source-catalog-error-quark"
    );
}

static void
source_record_free (AtmSourceRecord *record)
{
    if (record == NULL) {
        return;
    }

    g_free (record->path);
    g_free (record->sha256);
    g_free (record->media_type);
    g_free (record);
}

void
atm_source_catalog_free (AtmSourceCatalog *catalog)
{
    if (catalog == NULL) {
        return;
    }

    g_free (catalog->manifest_sha256);
    g_clear_pointer (&catalog->files, g_ptr_array_unref);
    g_free (catalog);
}

const char *
atm_source_role_name (AtmSourceRole role)
{
    switch (role) {
        case ATM_SOURCE_ROLE_CANONICAL:
            return "canonical";
        case ATM_SOURCE_ROLE_STRUCTURAL:
            return "structural";
        case ATM_SOURCE_ROLE_EVIDENCE:
            return "evidence";
        case ATM_SOURCE_ROLE_TABULAR:
            return "tabular";
        case ATM_SOURCE_ROLE_IMPLEMENTATION:
            return "implementation";
        default:
            return NULL;
    }
}

static gboolean
path_matches_prefix (const char *path, const char *prefix)
{
    gsize length;

    if (g_strcmp0 (path, prefix) == 0) {
        return TRUE;
    }

    length = strlen (prefix);

    return g_str_has_prefix (path, prefix) &&
        path[length] == '/';
}

static gboolean
array_matches_path (JsonArray *array, const char *path)
{
    for (guint i = 0; i < json_array_get_length (array); i++) {
        const char *prefix = json_array_get_string_element (
            array,
            i
        );

        if (prefix != NULL &&
            path_matches_prefix (path, prefix)) {
            return TRUE;
        }
    }

    return FALSE;
}

static guint
roles_for_path (
    const RetrievalPaths *retrieval,
    const char *path
)
{
    guint roles = 0;

    if (array_matches_path (retrieval->canonical, path)) {
        roles |= ATM_SOURCE_ROLE_CANONICAL;
    }

    if (array_matches_path (retrieval->structural, path)) {
        roles |= ATM_SOURCE_ROLE_STRUCTURAL;
    }

    if (array_matches_path (retrieval->evidence, path)) {
        roles |= ATM_SOURCE_ROLE_EVIDENCE;
    }

    if (array_matches_path (retrieval->tabular, path)) {
        roles |= ATM_SOURCE_ROLE_TABULAR;
    }

    if (array_matches_path (retrieval->implementation, path)) {
        roles |= ATM_SOURCE_ROLE_IMPLEMENTATION;
    }

    return roles;
}

static gboolean
path_is_excluded (
    const RetrievalPaths *retrieval,
    const char *path
)
{
    return array_matches_path (retrieval->exclude, path);
}

static gint
compare_path_strings (
    gconstpointer a,
    gconstpointer b
)
{
    const char *left = *(const char *const *) a;
    const char *right = *(const char *const *) b;

    return g_strcmp0 (left, right);
}

static gboolean
collect_regular_paths (
    const char *snapshot_root,
    const char *relative_directory,
    GPtrArray *paths,
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
    GDir *directory = NULL;
    const char *name;

    directory = g_dir_open (
        absolute_directory,
        0,
        error
    );

    if (directory == NULL) {
        g_free (absolute_directory);
        return FALSE;
    }

    while ((name = g_dir_read_name (directory)) != NULL) {
        char *relative =
            relative_directory[0] == '\0'
                ? g_strdup (name)
                : g_build_filename (
                    relative_directory,
                    name,
                    NULL
                );
        char *absolute = g_build_filename (
            snapshot_root,
            relative,
            NULL
        );
        GStatBuf stat_buffer;

        if (g_lstat (absolute, &stat_buffer) != 0) {
            g_set_error (
                error,
                ATM_SOURCE_CATALOG_ERROR,
                ATM_SOURCE_CATALOG_ERROR_IO,
                "Could not inspect snapshot path '%s': %s.",
                relative,
                g_strerror (errno)
            );
            g_free (absolute);
            g_free (relative);
            g_dir_close (directory);
            g_free (absolute_directory);
            return FALSE;
        }

        if (S_ISLNK (stat_buffer.st_mode) ||
            (!S_ISDIR (stat_buffer.st_mode) &&
             !S_ISREG (stat_buffer.st_mode))) {
            g_set_error (
                error,
                ATM_SOURCE_CATALOG_ERROR,
                ATM_SOURCE_CATALOG_ERROR_UNSAFE_ENTRY,
                "Snapshot contains unsupported entry '%s'.",
                relative
            );
            g_free (absolute);
            g_free (relative);
            g_dir_close (directory);
            g_free (absolute_directory);
            return FALSE;
        }

        if (S_ISDIR (stat_buffer.st_mode)) {
            if (!collect_regular_paths (
                    snapshot_root,
                    relative,
                    paths,
                    error
                )) {
                g_free (absolute);
                g_free (relative);
                g_dir_close (directory);
                g_free (absolute_directory);
                return FALSE;
            }
        } else {
            g_ptr_array_add (paths, relative);
            relative = NULL;
        }

        g_free (absolute);
        g_free (relative);
    }

    g_dir_close (directory);
    g_free (absolute_directory);
    return TRUE;
}

static gboolean
sha256_file (
    const char *path,
    char **out_sha256,
    guint64 *out_size,
    GError **error
)
{
    GChecksum *checksum = NULL;
    int fd = -1;
    guint8 buffer[ATM_HASH_BUFFER_BYTES];
    guint64 total = 0;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_sha256 != NULL, FALSE);
    g_return_val_if_fail (*out_sha256 == NULL, FALSE);

    fd = g_open (
        path,
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW,
        0
    );

    if (fd < 0) {
        g_set_error (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_IO,
            "Could not open source file for hashing: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    checksum = g_checksum_new (G_CHECKSUM_SHA256);

    if (checksum == NULL) {
        g_set_error_literal (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_CHECKSUM,
            "SHA-256 checksum support is unavailable."
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
                ATM_SOURCE_CATALOG_ERROR,
                ATM_SOURCE_CATALOG_ERROR_IO,
                "Could not read source file for hashing: %s.",
                g_strerror (errno)
            );
            goto out;
        }

        g_checksum_update (
            checksum,
            buffer,
            count
        );
        total += (guint64) count;
    }

    *out_sha256 = g_strdup (
        g_checksum_get_string (checksum)
    );

    if (*out_sha256 == NULL) {
        g_set_error_literal (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_CHECKSUM,
            "Could not finalize SHA-256 source hash."
        );
        goto out;
    }

    if (out_size != NULL) {
        *out_size = total;
    }

    ok = TRUE;

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

static const char *
media_type_for_path (const char *path)
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

gboolean
atm_repository_source_catalog_build (
    const char *snapshot_root,
    const char *expected_repository_id,
    AtmSourceCatalog **out_catalog,
    GError **error
)
{
    char *manifest_path = NULL;
    JsonParser *parser = NULL;
    JsonNode *root_node;
    JsonObject *root;
    JsonNode *retrieval_node;
    JsonObject *retrieval_object;
    RetrievalPaths retrieval = { 0 };
    GPtrArray *all_paths = NULL;
    AtmSourceCatalog *catalog = NULL;
    GError *local_error = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (snapshot_root != NULL, FALSE);
    g_return_val_if_fail (expected_repository_id != NULL, FALSE);
    g_return_val_if_fail (out_catalog != NULL, FALSE);
    g_return_val_if_fail (*out_catalog == NULL, FALSE);

    manifest_path = g_build_filename (
        snapshot_root,
        ".atm",
        "repository.json",
        NULL
    );

    parser = json_parser_new ();
    if (!json_parser_load_from_file (
            parser,
            manifest_path,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_MANIFEST,
            "Could not parse repository manifest: %s",
            local_error != NULL
                ? local_error->message
                : "unknown JSON error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    root_node = json_parser_get_root (parser);
    if (root_node == NULL ||
        json_node_get_node_type (root_node) != JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_MANIFEST,
            "Repository manifest root is not an object."
        );
        goto out;
    }

    root = json_node_get_object (root_node);

    if (!json_object_has_member (root, "repository_id") ||
        g_strcmp0 (
            json_object_get_string_member (
                root,
                "repository_id"
            ),
            expected_repository_id
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_IDENTITY,
            "Repository manifest identity does not match the catalog."
        );
        goto out;
    }

    retrieval_node = json_object_get_member (
        root,
        "retrieval"
    );

    if (retrieval_node == NULL ||
        json_node_get_node_type (retrieval_node) !=
            JSON_NODE_OBJECT) {
        g_set_error_literal (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_MANIFEST,
            "Repository manifest has no retrieval object."
        );
        goto out;
    }

    retrieval_object = json_node_get_object (retrieval_node);

    retrieval.canonical = json_object_get_array_member (
        retrieval_object,
        "canonical"
    );
    retrieval.structural = json_object_get_array_member (
        retrieval_object,
        "structural"
    );
    retrieval.evidence = json_object_get_array_member (
        retrieval_object,
        "evidence"
    );
    retrieval.tabular = json_object_get_array_member (
        retrieval_object,
        "tabular"
    );
    retrieval.implementation = json_object_get_array_member (
        retrieval_object,
        "implementation"
    );
    retrieval.exclude = json_object_get_array_member (
        retrieval_object,
        "exclude"
    );

    if (retrieval.canonical == NULL ||
        retrieval.structural == NULL ||
        retrieval.evidence == NULL ||
        retrieval.tabular == NULL ||
        retrieval.implementation == NULL ||
        retrieval.exclude == NULL) {
        g_set_error_literal (
            error,
            ATM_SOURCE_CATALOG_ERROR,
            ATM_SOURCE_CATALOG_ERROR_MANIFEST,
            "Repository retrieval manifest is incomplete."
        );
        goto out;
    }

    catalog = g_new0 (AtmSourceCatalog, 1);
    catalog->files = g_ptr_array_new_with_free_func (
        (GDestroyNotify) source_record_free
    );

    if (!sha256_file (
            manifest_path,
            &catalog->manifest_sha256,
            NULL,
            error
        )) {
        goto out;
    }

    all_paths = g_ptr_array_new_with_free_func (g_free);

    if (!collect_regular_paths (
            snapshot_root,
            "",
            all_paths,
            error
        )) {
        goto out;
    }

    g_ptr_array_sort (
        all_paths,
        compare_path_strings
    );

    for (guint i = 0; i < all_paths->len; i++) {
        const char *relative = g_ptr_array_index (
            all_paths,
            i
        );
        guint roles;
        char *absolute;
        AtmSourceRecord *record;

        if (path_is_excluded (&retrieval, relative)) {
            continue;
        }

        roles = roles_for_path (&retrieval, relative);

        if (roles == 0) {
            continue;
        }

        absolute = g_build_filename (
            snapshot_root,
            relative,
            NULL
        );
        record = g_new0 (AtmSourceRecord, 1);
        record->path = g_strdup (relative);
        record->roles = roles;
        record->media_type = g_strdup (
            media_type_for_path (relative)
        );

        if (!sha256_file (
                absolute,
                &record->sha256,
                &record->byte_size,
                error
            )) {
            g_free (absolute);
            source_record_free (record);
            goto out;
        }

        g_free (absolute);
        g_ptr_array_add (catalog->files, record);
    }

    *out_catalog = g_steal_pointer (&catalog);
    ok = TRUE;

out:
    g_clear_pointer (&all_paths, g_ptr_array_unref);
    g_clear_pointer (&catalog, atm_source_catalog_free);
    g_clear_object (&parser);
    g_clear_pointer (&manifest_path, g_free);
    return ok;
}
