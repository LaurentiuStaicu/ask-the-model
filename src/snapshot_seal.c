#include "snapshot_seal.h"

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ATM_SNAPSHOT_SEAL_SCHEMA_VERSION 1
#define ATM_SNAPSHOT_SEAL_MAX_BYTES (16 * 1024 * 1024)

typedef struct {
    char *path;
    guint64 size;
    char *sha256;
} AtmSnapshotSealFile;

GQuark
atm_snapshot_seal_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-snapshot-seal-error-quark"
    );
}

static void
seal_file_free (AtmSnapshotSealFile *file)
{
    if (file == NULL) {
        return;
    }

    g_free (file->path);
    g_free (file->sha256);
    g_free (file);
}

static gboolean
lower_hex_is_valid (
    const char *value,
    gsize expected_length
)
{
    if (value == NULL ||
        strlen (value) != expected_length) {
        return FALSE;
    }

    for (gsize i = 0; i < expected_length; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (g_ascii_isalpha (value[i]) &&
             !g_ascii_islower (value[i]))) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
identifier_is_valid (const char *value)
{
    if (value == NULL ||
        value[0] == '\0' ||
        strlen (value) > 64) {
        return FALSE;
    }

    for (const char *cursor = value;
         *cursor != '\0';
         cursor++) {
        if (!g_ascii_islower (*cursor) &&
            !g_ascii_isdigit (*cursor) &&
            *cursor != '-' &&
            *cursor != '_') {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
baseline_kind_is_valid (const char *value)
{
    return g_strcmp0 (value, "ingest") == 0 ||
        g_strcmp0 (value, "migration") == 0;
}

static char *
seal_path_for (
    const char *seal_root,
    const char *repository_id,
    const char *snapshot_sha
)
{
    char *filename = g_strdup_printf (
        "%s.json",
        snapshot_sha
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

static gboolean
hash_regular_file (
    const char *absolute_path,
    guint64 *out_size,
    char **out_sha256,
    GError **error
)
{
    int fd = -1;
    struct stat stat_buffer;
    GChecksum *checksum = NULL;
    guint8 buffer[64 * 1024];
    guint64 total = 0;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_sha256 != NULL, FALSE);
    g_return_val_if_fail (*out_sha256 == NULL, FALSE);

    fd = g_open (
        absolute_path,
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW,
        0
    );

    if (fd < 0) {
        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not open snapshot file '%s': %s.",
            absolute_path,
            g_strerror (errno)
        );
        goto out;
    }

    if (fstat (fd, &stat_buffer) != 0 ||
        !S_ISREG (stat_buffer.st_mode)) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Snapshot entry changed type while being sealed."
        );
        goto out;
    }

    checksum = g_checksum_new (G_CHECKSUM_SHA256);
    if (checksum == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "SHA-256 support is unavailable."
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
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Could not read snapshot file '%s': %s.",
                absolute_path,
                g_strerror (errno)
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

    *out_sha256 = g_strdup (
        g_checksum_get_string (checksum)
    );

    if (*out_sha256 == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not finalize snapshot file SHA-256."
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

static gboolean
collect_files_recursive (
    const char *snapshot_root,
    const char *relative_directory,
    GPtrArray *files,
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

        if (!g_utf8_validate (relative, -1, NULL)) {
            g_set_error_literal (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Snapshot contains a non-UTF-8 path."
            );
            g_free (absolute);
            g_free (relative);
            g_dir_close (directory);
            g_free (absolute_directory);
            return FALSE;
        }

        if (g_lstat (absolute, &stat_buffer) != 0) {
            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Could not inspect snapshot entry '%s': %s.",
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
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Snapshot contains unsafe entry '%s'.",
                relative
            );
            g_free (absolute);
            g_free (relative);
            g_dir_close (directory);
            g_free (absolute_directory);
            return FALSE;
        }

        if (S_ISDIR (stat_buffer.st_mode)) {
            if (!collect_files_recursive (
                    snapshot_root,
                    relative,
                    files,
                    error
                )) {
                g_free (absolute);
                g_free (relative);
                g_dir_close (directory);
                g_free (absolute_directory);
                return FALSE;
            }
        } else {
            AtmSnapshotSealFile *file =
                g_new0 (AtmSnapshotSealFile, 1);

            file->path = g_strdup (relative);

            if (!hash_regular_file (
                    absolute,
                    &file->size,
                    &file->sha256,
                    error
                )) {
                seal_file_free (file);
                g_free (absolute);
                g_free (relative);
                g_dir_close (directory);
                g_free (absolute_directory);
                return FALSE;
            }

            g_ptr_array_add (files, file);
        }

        g_free (absolute);
        g_free (relative);
    }

    g_dir_close (directory);
    g_free (absolute_directory);
    return TRUE;
}

static gint
compare_seal_files (
    gconstpointer a,
    gconstpointer b
)
{
    const AtmSnapshotSealFile *left =
        *(AtmSnapshotSealFile * const *) a;
    const AtmSnapshotSealFile *right =
        *(AtmSnapshotSealFile * const *) b;

    return g_strcmp0 (left->path, right->path);
}

static gboolean
collect_snapshot_files (
    const char *snapshot_root,
    GPtrArray **out_files,
    GError **error
)
{
    GStatBuf stat_buffer;
    GPtrArray *files = NULL;

    g_return_val_if_fail (out_files != NULL, FALSE);
    g_return_val_if_fail (*out_files == NULL, FALSE);

    if (g_lstat (snapshot_root, &stat_buffer) != 0 ||
        !S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Snapshot root is missing or is not a real directory."
        );
        return FALSE;
    }

    files = g_ptr_array_new_with_free_func (
        (GDestroyNotify) seal_file_free
    );

    if (!collect_files_recursive (
            snapshot_root,
            "",
            files,
            error
        )) {
        g_ptr_array_unref (files);
        return FALSE;
    }

    g_ptr_array_sort (
        files,
        compare_seal_files
    );

    *out_files = files;
    return TRUE;
}

static char *
root_digest_for_files (GPtrArray *files)
{
    static const guint8 prefix[] =
        "ATM-SNAPSHOT-SEAL-V1";
    GChecksum *checksum =
        g_checksum_new (G_CHECKSUM_SHA256);

    if (checksum == NULL) {
        return NULL;
    }

    g_checksum_update (
        checksum,
        prefix,
        sizeof prefix - 1
    );

    for (guint i = 0; i < files->len; i++) {
        AtmSnapshotSealFile *file =
            g_ptr_array_index (files, i);
        guint64 path_length_be = GUINT64_TO_BE (
            (guint64) strlen (file->path)
        );
        guint64 size_be = GUINT64_TO_BE (
            file->size
        );

        g_checksum_update (
            checksum,
            (const guchar *) &path_length_be,
            sizeof path_length_be
        );
        g_checksum_update (
            checksum,
            (const guchar *) file->path,
            strlen (file->path)
        );
        g_checksum_update (
            checksum,
            (const guchar *) &size_be,
            sizeof size_be
        );
        g_checksum_update (
            checksum,
            (const guchar *) file->sha256,
            64
        );
    }

    char *digest = g_strdup (
        g_checksum_get_string (checksum)
    );
    g_checksum_free (checksum);
    return digest;
}

static gboolean
validate_arguments (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    GError **error
)
{
    char *basename = NULL;
    gboolean ok = FALSE;

    if (seal_root == NULL ||
        seal_root[0] == '\0' ||
        snapshot_root == NULL ||
        snapshot_root[0] == '\0' ||
        !identifier_is_valid (repository_id) ||
        !lower_hex_is_valid (snapshot_sha, 40)) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_ARGUMENT,
            "Snapshot seal received invalid identity or path arguments."
        );
        return FALSE;
    }

    basename = g_path_get_basename (snapshot_root);

    if (g_strcmp0 (basename, snapshot_sha) != 0) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_ARGUMENT,
            "Snapshot root basename does not match the snapshot SHA."
        );
        goto out;
    }

    ok = TRUE;

out:
    g_free (basename);
    return ok;
}

static char *
serialize_seal (
    const char *repository_id,
    const char *snapshot_sha,
    const char *baseline_kind,
    const char *root_sha256,
    GPtrArray *files
)
{
    JsonBuilder *builder = json_builder_new ();
    JsonGenerator *generator = json_generator_new ();
    GDateTime *now = g_date_time_new_now_utc ();
    char *created_at = g_date_time_format_iso8601 (
        now
    );

    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "schema_version"
    );
    json_builder_add_int_value (
        builder,
        ATM_SNAPSHOT_SEAL_SCHEMA_VERSION
    );

    json_builder_set_member_name (
        builder,
        "repository_id"
    );
    json_builder_add_string_value (
        builder,
        repository_id
    );

    json_builder_set_member_name (
        builder,
        "snapshot_sha"
    );
    json_builder_add_string_value (
        builder,
        snapshot_sha
    );

    json_builder_set_member_name (
        builder,
        "baseline_kind"
    );
    json_builder_add_string_value (
        builder,
        baseline_kind
    );

    json_builder_set_member_name (
        builder,
        "created_at_utc"
    );
    json_builder_add_string_value (
        builder,
        created_at
    );

    json_builder_set_member_name (
        builder,
        "root_sha256"
    );
    json_builder_add_string_value (
        builder,
        root_sha256
    );

    json_builder_set_member_name (
        builder,
        "files"
    );
    json_builder_begin_array (builder);

    for (guint i = 0; i < files->len; i++) {
        AtmSnapshotSealFile *file =
            g_ptr_array_index (files, i);

        json_builder_begin_object (builder);

        json_builder_set_member_name (
            builder,
            "path"
        );
        json_builder_add_string_value (
            builder,
            file->path
        );

        json_builder_set_member_name (
            builder,
            "size"
        );
        json_builder_add_int_value (
            builder,
            (gint64) file->size
        );

        json_builder_set_member_name (
            builder,
            "sha256"
        );
        json_builder_add_string_value (
            builder,
            file->sha256
        );

        json_builder_end_object (builder);
    }

    json_builder_end_array (builder);
    json_builder_end_object (builder);

    json_generator_set_root (
        generator,
        json_builder_get_root (builder)
    );
    json_generator_set_pretty (
        generator,
        TRUE
    );

    char *data = json_generator_to_data (
        generator,
        NULL
    );

    g_free (created_at);
    g_date_time_unref (now);
    g_object_unref (generator);
    g_object_unref (builder);
    return data;
}

gboolean
atm_snapshot_seal_create (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    const char *baseline_kind,
    char **out_seal_path,
    char **out_root_sha256,
    GError **error
)
{
    GPtrArray *files = NULL;
    char *root_sha256 = NULL;
    char *seal_path = NULL;
    char *repository_seal_root = NULL;
    char *data = NULL;
    GStatBuf stat_buffer;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_seal_path != NULL, FALSE);
    g_return_val_if_fail (*out_seal_path == NULL, FALSE);
    g_return_val_if_fail (out_root_sha256 != NULL, FALSE);
    g_return_val_if_fail (*out_root_sha256 == NULL, FALSE);

    if (!validate_arguments (
            seal_root,
            snapshot_root,
            repository_id,
            snapshot_sha,
            error
        )) {
        return FALSE;
    }

    if (!baseline_kind_is_valid (baseline_kind)) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_ARGUMENT,
            "Snapshot seal baseline kind must be 'ingest' or 'migration'."
        );
        return FALSE;
    }

    seal_path = seal_path_for (
        seal_root,
        repository_id,
        snapshot_sha
    );

    if (g_lstat (seal_path, &stat_buffer) == 0) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_EXISTS,
            "Refusing to overwrite an existing snapshot seal."
        );
        goto out;
    } else if (errno != ENOENT) {
        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not inspect snapshot seal path: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!collect_snapshot_files (
            snapshot_root,
            &files,
            error
        )) {
        goto out;
    }

    root_sha256 = root_digest_for_files (files);
    if (root_sha256 == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not compute the snapshot root digest."
        );
        goto out;
    }

    data = serialize_seal (
        repository_id,
        snapshot_sha,
        baseline_kind,
        root_sha256,
        files
    );

    if (data == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not serialize the snapshot seal."
        );
        goto out;
    }

    repository_seal_root = g_path_get_dirname (
        seal_path
    );

    if (g_mkdir_with_parents (
            repository_seal_root,
            0700
        ) != 0) {
        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not create snapshot seal directory: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!g_file_set_contents_full (
            seal_path,
            data,
            -1,
            G_FILE_SET_CONTENTS_CONSISTENT |
                G_FILE_SET_CONTENTS_DURABLE,
            0600,
            error
        )) {
        goto out;
    }

    *out_seal_path = g_steal_pointer (
        &seal_path
    );
    *out_root_sha256 = g_steal_pointer (
        &root_sha256
    );
    ok = TRUE;

out:
    g_clear_pointer (&data, g_free);
    g_clear_pointer (
        &repository_seal_root,
        g_free
    );
    g_clear_pointer (&seal_path, g_free);
    g_clear_pointer (&root_sha256, g_free);
    g_clear_pointer (&files, g_ptr_array_unref);
    return ok;
}

static gboolean
json_node_is_string (JsonNode *node)
{
    return node != NULL &&
        json_node_get_node_type (node) ==
            JSON_NODE_VALUE &&
        json_node_get_value_type (node) ==
            G_TYPE_STRING;
}

static gboolean
json_node_is_int64 (JsonNode *node)
{
    return node != NULL &&
        json_node_get_node_type (node) ==
            JSON_NODE_VALUE &&
        json_node_get_value_type (node) ==
            G_TYPE_INT64;
}

static gboolean
stored_file_matches (
    JsonObject *object,
    AtmSnapshotSealFile *current
)
{
    JsonNode *path_node =
        json_object_get_member (
            object,
            "path"
        );
    JsonNode *size_node =
        json_object_get_member (
            object,
            "size"
        );
    JsonNode *sha_node =
        json_object_get_member (
            object,
            "sha256"
        );

    if (!json_node_is_string (path_node) ||
        !json_node_is_int64 (size_node) ||
        !json_node_is_string (sha_node)) {
        return FALSE;
    }

    const char *path =
        json_node_get_string (path_node);
    gint64 size = json_node_get_int (
        size_node
    );
    const char *sha256 =
        json_node_get_string (sha_node);

    return size >= 0 &&
        g_strcmp0 (path, current->path) == 0 &&
        (guint64) size == current->size &&
        lower_hex_is_valid (sha256, 64) &&
        g_strcmp0 (sha256, current->sha256) == 0;
}

static void
set_check_result (
    AtmSnapshotSealStatus status,
    const char *detail,
    AtmSnapshotSealStatus *out_status,
    char **out_detail
)
{
    *out_status = status;
    *out_detail = g_strdup (detail);
}

gboolean
atm_snapshot_seal_check (
    const char *seal_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    AtmSnapshotSealStatus *out_status,
    char **out_detail,
    char **out_root_sha256,
    GError **error
)
{
    char *seal_path = NULL;
    GStatBuf stat_buffer;
    char *data = NULL;
    gsize data_length = 0;
    JsonParser *parser = NULL;
    JsonNode *root_node = NULL;
    JsonObject *root = NULL;
    JsonNode *schema_node = NULL;
    JsonNode *repository_node = NULL;
    JsonNode *snapshot_node = NULL;
    JsonNode *baseline_node = NULL;
    JsonNode *root_sha_node = NULL;
    JsonNode *files_node = NULL;
    GPtrArray *current_files = NULL;
    char *current_root_sha = NULL;
    GError *local_error = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (out_status != NULL, FALSE);
    g_return_val_if_fail (out_detail != NULL, FALSE);
    g_return_val_if_fail (*out_detail == NULL, FALSE);
    g_return_val_if_fail (out_root_sha256 != NULL, FALSE);
    g_return_val_if_fail (*out_root_sha256 == NULL, FALSE);

    if (!validate_arguments (
            seal_root,
            snapshot_root,
            repository_id,
            snapshot_sha,
            error
        )) {
        return FALSE;
    }

    seal_path = seal_path_for (
        seal_root,
        repository_id,
        snapshot_sha
    );

    if (g_lstat (seal_path, &stat_buffer) != 0) {
        if (errno == ENOENT) {
            set_check_result (
                ATM_SNAPSHOT_SEAL_ABSENT,
                "Snapshot seal is absent.",
                out_status,
                out_detail
            );
            ok = TRUE;
            goto out;
        }

        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not inspect snapshot seal: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!S_ISREG (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode) ||
        (guint64) stat_buffer.st_size >
            ATM_SNAPSHOT_SEAL_MAX_BYTES) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_INVALID,
            "Snapshot seal is not a safe regular file or exceeds the size limit.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    if (!g_file_get_contents (
            seal_path,
            &data,
            &data_length,
            &local_error
        )) {
        g_propagate_error (error, local_error);
        local_error = NULL;
        goto out;
    }

    parser = json_parser_new ();

    if (!json_parser_load_from_data (
            parser,
            data,
            data_length,
            &local_error
        )) {
        g_clear_error (&local_error);
        set_check_result (
            ATM_SNAPSHOT_SEAL_INVALID,
            "Snapshot seal is not valid JSON.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    root_node = json_parser_get_root (parser);

    if (root_node == NULL ||
        json_node_get_node_type (root_node) !=
            JSON_NODE_OBJECT) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_INVALID,
            "Snapshot seal root is not an object.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    root = json_node_get_object (root_node);
    schema_node = json_object_get_member (
        root,
        "schema_version"
    );
    repository_node = json_object_get_member (
        root,
        "repository_id"
    );
    snapshot_node = json_object_get_member (
        root,
        "snapshot_sha"
    );
    baseline_node = json_object_get_member (
        root,
        "baseline_kind"
    );
    root_sha_node = json_object_get_member (
        root,
        "root_sha256"
    );
    files_node = json_object_get_member (
        root,
        "files"
    );

    if (!json_node_is_int64 (schema_node) ||
        json_node_get_int (schema_node) !=
            ATM_SNAPSHOT_SEAL_SCHEMA_VERSION ||
        !json_node_is_string (repository_node) ||
        !json_node_is_string (snapshot_node) ||
        !json_node_is_string (baseline_node) ||
        !json_node_is_string (root_sha_node) ||
        files_node == NULL ||
        json_node_get_node_type (files_node) !=
            JSON_NODE_ARRAY) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_INVALID,
            "Snapshot seal does not match schema v1.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    const char *stored_repository =
        json_node_get_string (repository_node);
    const char *stored_snapshot =
        json_node_get_string (snapshot_node);
    const char *stored_baseline =
        json_node_get_string (baseline_node);
    const char *stored_root_sha =
        json_node_get_string (root_sha_node);

    if (g_strcmp0 (
            stored_repository,
            repository_id
        ) != 0 ||
        g_strcmp0 (
            stored_snapshot,
            snapshot_sha
        ) != 0 ||
        !baseline_kind_is_valid (
            stored_baseline
        ) ||
        !lower_hex_is_valid (
            stored_root_sha,
            64
        )) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_INVALID,
            "Snapshot seal identity or digest metadata is invalid.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    if (!collect_snapshot_files (
            snapshot_root,
            &current_files,
            &local_error
        )) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_MISMATCH,
            local_error != NULL
                ? local_error->message
                : "Snapshot content could not be validated.",
            out_status,
            out_detail
        );
        g_clear_error (&local_error);
        ok = TRUE;
        goto out;
    }

    current_root_sha = root_digest_for_files (
        current_files
    );

    if (current_root_sha == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not compute current snapshot root digest."
        );
        goto out;
    }

    *out_root_sha256 = g_strdup (
        current_root_sha
    );

    JsonArray *stored_files =
        json_node_get_array (files_node);

    if (json_array_get_length (stored_files) !=
        current_files->len) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_MISMATCH,
            "Snapshot file set differs from the stored seal.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    for (
        guint i = 0;
        i < current_files->len;
        i++
    ) {
        JsonNode *stored_node =
            json_array_get_element (
                stored_files,
                i
            );

        if (stored_node == NULL ||
            json_node_get_node_type (stored_node) !=
                JSON_NODE_OBJECT) {
            set_check_result (
                ATM_SNAPSHOT_SEAL_INVALID,
                "Snapshot seal contains an invalid file record.",
                out_status,
                out_detail
            );
            ok = TRUE;
            goto out;
        }

        AtmSnapshotSealFile *current =
            g_ptr_array_index (
                current_files,
                i
            );

        if (!stored_file_matches (
                json_node_get_object (stored_node),
                current
            )) {
            set_check_result (
                ATM_SNAPSHOT_SEAL_MISMATCH,
                "Snapshot file content or metadata differs from the stored seal.",
                out_status,
                out_detail
            );
            ok = TRUE;
            goto out;
        }
    }

    if (g_strcmp0 (
            current_root_sha,
            stored_root_sha
        ) != 0) {
        set_check_result (
            ATM_SNAPSHOT_SEAL_MISMATCH,
            "Snapshot aggregate digest differs from the stored seal.",
            out_status,
            out_detail
        );
        ok = TRUE;
        goto out;
    }

    set_check_result (
        ATM_SNAPSHOT_SEAL_VALID,
        "Snapshot seal matches the current local snapshot.",
        out_status,
        out_detail
    );
    ok = TRUE;

out:
    g_clear_error (&local_error);
    g_clear_pointer (
        &current_root_sha,
        g_free
    );
    g_clear_pointer (
        &current_files,
        g_ptr_array_unref
    );
    g_clear_pointer (&parser, g_object_unref);
    g_clear_pointer (&data, g_free);
    g_clear_pointer (&seal_path, g_free);
    return ok;
}
