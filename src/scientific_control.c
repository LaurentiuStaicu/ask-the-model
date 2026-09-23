#include "scientific_control.h"

#include "json_pointer.h"

#include <json-glib/json-glib.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    AtmScientificControlId control_id;
    const char *repository_id;
    const char *source_path;
    const char *json_pointer;
    AtmScientificControlValueType value_type;
} AtmScientificControlSpec;

static const AtmScientificControlSpec CONTROL_SPECS[] = {
    {
        ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
        "rmd",
        "model/dynamics/core_contract.json",
        "/behavioural_closure/active",
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN
    }
};

GQuark
atm_scientific_control_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-control-error-quark"
    );
}

void
atm_scientific_control_value_free (
    AtmScientificControlValue *value
)
{
    if (value == NULL) {
        return;
    }

    g_free (value->repository_id);
    g_free (value->snapshot_sha);
    g_free (value->source_path);
    g_free (value->json_pointer);
    g_free (value->string_value);
    g_free (value);
}

static gboolean
string_is_present (const char *value)
{
    return value != NULL && value[0] != '\0';
}

static gboolean
sha_is_valid (const char *sha)
{
    if (sha == NULL || strlen (sha) != 40) {
        return FALSE;
    }

    for (const char *cursor = sha;
         *cursor != '\0';
         cursor++) {
        if (!g_ascii_isxdigit (*cursor)) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
snapshot_binding_is_valid (
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha
)
{
    char *sha_name = g_path_get_basename (
        snapshot_root
    );
    char *snapshots_path = g_path_get_dirname (
        snapshot_root
    );
    char *snapshots_name = g_path_get_basename (
        snapshots_path
    );
    char *repository_path = g_path_get_dirname (
        snapshots_path
    );
    char *repository_name = g_path_get_basename (
        repository_path
    );
    char *repositories_path = g_path_get_dirname (
        repository_path
    );
    char *repositories_name = g_path_get_basename (
        repositories_path
    );

    gboolean valid =
        g_strcmp0 (sha_name, snapshot_sha) == 0 &&
        g_strcmp0 (snapshots_name, "snapshots") == 0 &&
        g_strcmp0 (repository_name, repository_id) == 0 &&
        g_strcmp0 (repositories_name, "Repositories") == 0;

    g_free (repositories_name);
    g_free (repositories_path);
    g_free (repository_name);
    g_free (repository_path);
    g_free (snapshots_name);
    g_free (snapshots_path);
    g_free (sha_name);

    return valid;
}

static const AtmScientificControlSpec *
find_spec (AtmScientificControlId control_id)
{
    for (gsize i = 0;
         i < G_N_ELEMENTS (CONTROL_SPECS);
         i++) {
        if (CONTROL_SPECS[i].control_id == control_id) {
            return &CONTROL_SPECS[i];
        }
    }

    return NULL;
}

static gboolean
path_component_is_safe (const char *component)
{
    return
        component != NULL &&
        component[0] != '\0' &&
        strcmp (component, ".") != 0 &&
        strcmp (component, "..") != 0;
}

static int
open_relative_regular_file (
    int root_fd,
    const char *relative_path,
    GError **error
)
{
    if (!string_is_present (relative_path) ||
        g_path_is_absolute (relative_path)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
            "Scientific control source path is not a safe relative path."
        );
        return -1;
    }

    char **parts = g_strsplit (
        relative_path,
        "/",
        -1
    );
    guint count = g_strv_length (parts);

    if (count == 0) {
        g_strfreev (parts);
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
            "Scientific control source path has no components."
        );
        return -1;
    }

    int directory_fd = dup (root_fd);

    if (directory_fd < 0) {
        g_strfreev (parts);
        g_set_error (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_IO,
            "Could not duplicate snapshot root descriptor: %s.",
            g_strerror (errno)
        );
        return -1;
    }

    for (guint i = 0; i < count; i++) {
        if (!path_component_is_safe (parts[i])) {
            close (directory_fd);
            g_strfreev (parts);
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_CONTROL_ERROR,
                ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
                "Scientific control source path contains an unsafe component."
            );
            return -1;
        }

        if (i + 1 < count) {
            int next_fd = openat (
                directory_fd,
                parts[i],
                O_RDONLY | O_DIRECTORY |
                    O_NOFOLLOW | O_CLOEXEC
            );

            if (next_fd < 0) {
                close (directory_fd);
                g_strfreev (parts);
                g_set_error (
                    error,
                    ATM_SCIENTIFIC_CONTROL_ERROR,
                    ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
                    "Could not open scientific control directory without following links: %s.",
                    g_strerror (errno)
                );
                return -1;
            }

            close (directory_fd);
            directory_fd = next_fd;
            continue;
        }

        int file_fd = openat (
            directory_fd,
            parts[i],
            O_RDONLY | O_NOFOLLOW | O_CLOEXEC
        );

        close (directory_fd);
        g_strfreev (parts);

        if (file_fd < 0) {
            g_set_error (
                error,
                ATM_SCIENTIFIC_CONTROL_ERROR,
                ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
                "Could not open scientific control source without following links: %s.",
                g_strerror (errno)
            );
            return -1;
        }

        struct stat st;

        if (fstat (file_fd, &st) != 0 ||
            !S_ISREG (st.st_mode)) {
            close (file_fd);
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_CONTROL_ERROR,
                ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
                "Scientific control source is not a regular file."
            );
            return -1;
        }

        return file_fd;
    }

    close (directory_fd);
    g_strfreev (parts);

    g_set_error_literal (
        error,
        ATM_SCIENTIFIC_CONTROL_ERROR,
        ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
        "Scientific control source path could not be opened."
    );
    return -1;
}

static gboolean
read_fd_bounded (
    int fd,
    char **out_contents,
    gsize *out_length,
    GError **error
)
{
    GByteArray *bytes = g_byte_array_new ();
    guint8 buffer[64 * 1024];

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
                ATM_SCIENTIFIC_CONTROL_ERROR,
                ATM_SCIENTIFIC_CONTROL_ERROR_IO,
                "Could not read scientific control source: %s.",
                g_strerror (errno)
            );
            g_byte_array_unref (bytes);
            return FALSE;
        }

        if (bytes->len + (gsize) count >
            ATM_SCIENTIFIC_CONTROL_MAX_BYTES) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_CONTROL_ERROR,
                ATM_SCIENTIFIC_CONTROL_ERROR_TOO_LARGE,
                "Scientific control source exceeds the size limit."
            );
            g_byte_array_unref (bytes);
            return FALSE;
        }

        g_byte_array_append (
            bytes,
            buffer,
            (guint) count
        );
    }

    g_byte_array_append (
        bytes,
        (const guint8 *) "",
        1
    );

    *out_length = bytes->len - 1;
    *out_contents = (char *) g_byte_array_free (
        bytes,
        FALSE
    );
    return TRUE;
}

static gboolean
extract_typed_value (
    JsonNode *node,
    const AtmScientificControlSpec *spec,
    AtmScientificControlValue *value,
    GError **error
)
{
    GType value_type = json_node_get_value_type (node);

    switch (spec->value_type) {
        case ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN:
            if (json_node_get_node_type (node) != JSON_NODE_VALUE ||
                value_type != G_TYPE_BOOLEAN) {
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_CONTROL_ERROR,
                    ATM_SCIENTIFIC_CONTROL_ERROR_TYPE,
                    "Scientific control value is not the expected boolean type."
                );
                return FALSE;
            }

            value->boolean_value =
                json_node_get_boolean (node);
            return TRUE;

        case ATM_SCIENTIFIC_CONTROL_VALUE_STRING:
            if (json_node_get_node_type (node) != JSON_NODE_VALUE ||
                value_type != G_TYPE_STRING) {
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_CONTROL_ERROR,
                    ATM_SCIENTIFIC_CONTROL_ERROR_TYPE,
                    "Scientific control value is not the expected string type."
                );
                return FALSE;
            }

            value->string_value = g_strdup (
                json_node_get_string (node)
            );
            return TRUE;

        case ATM_SCIENTIFIC_CONTROL_VALUE_NUMBER:
            if (json_node_get_node_type (node) != JSON_NODE_VALUE ||
                !(value_type == G_TYPE_DOUBLE ||
                  value_type == G_TYPE_INT64)) {
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_CONTROL_ERROR,
                    ATM_SCIENTIFIC_CONTROL_ERROR_TYPE,
                    "Scientific control value is not the expected numeric type."
                );
                return FALSE;
            }

            value->number_value =
                json_node_get_double (node);
            return TRUE;

        default:
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_CONTROL_ERROR,
                ATM_SCIENTIFIC_CONTROL_ERROR_TYPE,
                "Scientific control specification uses an unsupported value type."
            );
            return FALSE;
    }
}

gboolean
atm_scientific_control_read (
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    AtmScientificControlId control_id,
    AtmScientificControlValue **out_value,
    GError **error
)
{
    int root_fd = -1;
    int file_fd = -1;
    char *contents = NULL;
    gsize length = 0;
    JsonParser *parser = NULL;
    AtmScientificControlValue *value = NULL;
    gboolean ok = FALSE;

    if (!string_is_present (snapshot_root) ||
        !string_is_present (repository_id) ||
        !sha_is_valid (snapshot_sha) ||
        out_value == NULL ||
        *out_value != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_ARGUMENT,
            "Scientific control reader received invalid arguments."
        );
        return FALSE;
    }

    const AtmScientificControlSpec *spec = find_spec (
        control_id
    );

    if (spec == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_ARGUMENT,
            "Unknown scientific control identifier."
        );
        return FALSE;
    }

    if (g_strcmp0 (
            repository_id,
            spec->repository_id
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_REPOSITORY,
            "Scientific control does not belong to the selected repository."
        );
        return FALSE;
    }

    if (!snapshot_binding_is_valid (
            snapshot_root,
            repository_id,
            snapshot_sha
        )) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_SNAPSHOT,
            "Scientific control snapshot path is not bound to the selected repository and pinned SHA."
        );
        return FALSE;
    }

    root_fd = open (
        snapshot_root,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (root_fd < 0) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
            "Could not open snapshot root without following links: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    file_fd = open_relative_regular_file (
        root_fd,
        spec->source_path,
        error
    );

    if (file_fd < 0) {
        goto out;
    }

    if (!read_fd_bounded (
            file_fd,
            &contents,
            &length,
            error
        )) {
        goto out;
    }

    parser = json_parser_new ();
    GError *parse_error = NULL;

    if (!json_parser_load_from_data (
            parser,
            contents,
            length,
            &parse_error
        )) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_PARSE,
            "Could not parse scientific control JSON: %s.",
            parse_error != NULL
                ? parse_error->message
                : "unknown JSON error"
        );
        g_clear_error (&parse_error);
        goto out;
    }

    GError *pointer_error = NULL;
    JsonNode *node = atm_json_pointer_evaluate (
        json_parser_get_root (parser),
        spec->json_pointer,
        &pointer_error
    );

    if (node == NULL) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_CONTROL_ERROR,
            ATM_SCIENTIFIC_CONTROL_ERROR_POINTER,
            "Could not resolve scientific control JSON Pointer: %s.",
            pointer_error != NULL
                ? pointer_error->message
                : "unknown pointer error"
        );
        g_clear_error (&pointer_error);
        goto out;
    }

    value = g_new0 (
        AtmScientificControlValue,
        1
    );
    value->control_id = spec->control_id;
    value->repository_id = g_strdup (
        spec->repository_id
    );
    value->snapshot_sha = g_strdup (
        snapshot_sha
    );
    value->source_path = g_strdup (
        spec->source_path
    );
    value->json_pointer = g_strdup (
        spec->json_pointer
    );
    value->value_type = spec->value_type;

    if (!extract_typed_value (
            node,
            spec,
            value,
            error
        )) {
        goto out;
    }

    *out_value = g_steal_pointer (&value);
    ok = TRUE;

out:
    g_clear_pointer (
        &value,
        atm_scientific_control_value_free
    );
    g_clear_object (&parser);
    g_free (contents);

    if (file_fd >= 0) {
        close (file_fd);
    }

    if (root_fd >= 0) {
        close (root_fd);
    }

    return ok;
}