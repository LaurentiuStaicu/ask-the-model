#include "json_pointer.h"

#include <errno.h>
#include <string.h>

GQuark
atm_json_pointer_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-json-pointer-error-quark"
    );
}

static char *
decode_token (
    const char *encoded,
    GError **error
)
{
    GString *decoded = g_string_new (NULL);

    for (const char *cursor = encoded;
         *cursor != '\0';
         cursor++) {
        if (*cursor != '~') {
            g_string_append_c (decoded, *cursor);
            continue;
        }

        cursor++;

        if (*cursor == '0') {
            g_string_append_c (decoded, '~');
        } else if (*cursor == '1') {
            g_string_append_c (decoded, '/');
        } else {
            g_set_error_literal (
                error,
                ATM_JSON_POINTER_ERROR,
                ATM_JSON_POINTER_ERROR_SYNTAX,
                "JSON Pointer contains an invalid '~' escape."
            );
            g_string_free (decoded, TRUE);
            return NULL;
        }
    }

    return g_string_free (decoded, FALSE);
}

static gboolean
parse_array_index (
    const char *token,
    guint *out_index,
    GError **error
)
{
    g_return_val_if_fail (out_index != NULL, FALSE);

    if (token == NULL || token[0] == '\0') {
        g_set_error_literal (
            error,
            ATM_JSON_POINTER_ERROR,
            ATM_JSON_POINTER_ERROR_LOOKUP,
            "JSON Pointer array token is empty."
        );
        return FALSE;
    }

    if (token[0] == '0' && token[1] != '\0') {
        g_set_error_literal (
            error,
            ATM_JSON_POINTER_ERROR,
            ATM_JSON_POINTER_ERROR_SYNTAX,
            "JSON Pointer array index contains a leading zero."
        );
        return FALSE;
    }

    for (const char *cursor = token;
         *cursor != '\0';
         cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            g_set_error_literal (
                error,
                ATM_JSON_POINTER_ERROR,
                ATM_JSON_POINTER_ERROR_LOOKUP,
                "JSON Pointer array token is not a base-10 index."
            );
            return FALSE;
        }
    }

    errno = 0;
    char *end = NULL;
    guint64 value = g_ascii_strtoull (
        token,
        &end,
        10
    );

    if (errno == ERANGE ||
        end == NULL ||
        *end != '\0' ||
        value > G_MAXUINT) {
        g_set_error_literal (
            error,
            ATM_JSON_POINTER_ERROR,
            ATM_JSON_POINTER_ERROR_LOOKUP,
            "JSON Pointer array index is out of range."
        );
        return FALSE;
    }

    *out_index = (guint) value;
    return TRUE;
}

JsonNode *
atm_json_pointer_evaluate (
    JsonNode *root,
    const char *pointer,
    GError **error
)
{
    g_return_val_if_fail (root != NULL, NULL);
    g_return_val_if_fail (pointer != NULL, NULL);

    if (pointer[0] == '\0') {
        return root;
    }

    if (pointer[0] != '/') {
        g_set_error_literal (
            error,
            ATM_JSON_POINTER_ERROR,
            ATM_JSON_POINTER_ERROR_SYNTAX,
            "JSON Pointer must be empty or begin with '/'."
        );
        return NULL;
    }

    char **tokens = g_strsplit (
        pointer + 1,
        "/",
        -1
    );
    JsonNode *current = root;

    for (guint i = 0; tokens[i] != NULL; i++) {
        char *token = decode_token (
            tokens[i],
            error
        );

        if (token == NULL) {
            g_strfreev (tokens);
            return NULL;
        }

        if (json_node_get_node_type (current) ==
            JSON_NODE_OBJECT) {
            JsonObject *object = json_node_get_object (
                current
            );

            if (!json_object_has_member (
                    object,
                    token
                )) {
                g_set_error (
                    error,
                    ATM_JSON_POINTER_ERROR,
                    ATM_JSON_POINTER_ERROR_LOOKUP,
                    "JSON Pointer member '%s' does not exist.",
                    token
                );
                g_free (token);
                g_strfreev (tokens);
                return NULL;
            }

            current = json_object_get_member (
                object,
                token
            );
        } else if (
            json_node_get_node_type (current) ==
            JSON_NODE_ARRAY
        ) {
            guint index = 0;

            if (!parse_array_index (
                    token,
                    &index,
                    error
                )) {
                g_free (token);
                g_strfreev (tokens);
                return NULL;
            }

            JsonArray *array = json_node_get_array (
                current
            );

            if (index >= json_array_get_length (array)) {
                g_set_error (
                    error,
                    ATM_JSON_POINTER_ERROR,
                    ATM_JSON_POINTER_ERROR_LOOKUP,
                    "JSON Pointer array index %u is out of bounds.",
                    index
                );
                g_free (token);
                g_strfreev (tokens);
                return NULL;
            }

            current = json_array_get_element (
                array,
                index
            );
        } else {
            g_set_error_literal (
                error,
                ATM_JSON_POINTER_ERROR,
                ATM_JSON_POINTER_ERROR_LOOKUP,
                "JSON Pointer cannot descend through a scalar value."
            );
            g_free (token);
            g_strfreev (tokens);
            return NULL;
        }

        g_free (token);
    }

    g_strfreev (tokens);
    return current;
}
