#include "scientific_canonical.h"

#include <json-glib/json-glib.h>

#include <errno.h>
#include <math.h>
#include <string.h>

static const guint8 CONTENT_DOMAIN[] =
    "ATM-SCIENTIFIC-CONTENT-v1\0";
static const guint8 QUALIFIED_DOMAIN[] =
    "ATM-QUALIFIED-ARTIFACT-v1\0";

GQuark
atm_scientific_canonical_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-canonical-error-quark"
    );
}

static gboolean
nonempty_utf8 (const char *value)
{
    return value != NULL &&
        value[0] != '\0' &&
        g_utf8_validate (value, -1, NULL);
}

static gboolean
optional_utf8 (const char *value)
{
    return value == NULL ||
        g_utf8_validate (value, -1, NULL);
}

static gboolean
lower_hex_is_valid (
    const char *value,
    gsize length
)
{
    if (value == NULL ||
        strlen (value) != length) {
        return FALSE;
    }

    for (gsize i = 0; i < length; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (value[i] >= 'A' && value[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static void
checksum_u8 (
    GChecksum *checksum,
    guint8 value
)
{
    g_checksum_update (
        checksum,
        &value,
        1
    );
}

static void
checksum_u64 (
    GChecksum *checksum,
    guint64 value
)
{
    guint64 encoded = GUINT64_TO_BE (value);

    g_checksum_update (
        checksum,
        (const guchar *) &encoded,
        sizeof encoded
    );
}

static void
checksum_i64 (
    GChecksum *checksum,
    gint64 value
)
{
    guint64 bits;

    memcpy (&bits, &value, sizeof bits);
    checksum_u64 (checksum, bits);
}

static void
checksum_string (
    GChecksum *checksum,
    const char *value
)
{
    if (value == NULL) {
        checksum_u64 (checksum, G_MAXUINT64);
        return;
    }

    gsize length = strlen (value);

    checksum_u64 (
        checksum,
        (guint64) length
    );
    g_checksum_update (
        checksum,
        (const guchar *) value,
        length
    );
}

static char *
finish_checksum (GChecksum *checksum)
{
    char *result = g_strdup (
        g_checksum_get_string (checksum)
    );

    g_checksum_free (checksum);
    return result;
}

static gint
compare_string_pointers (
    gconstpointer left,
    gconstpointer right
)
{
    const char *a = *(const char *const *) left;
    const char *b = *(const char *const *) right;

    return strcmp (a, b);
}

static gboolean
update_json_node (
    GChecksum *checksum,
    JsonNode *node,
    GError **error
)
{
    if (node == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
            "Canonical JSON contains a missing node."
        );
        return FALSE;
    }

    switch (json_node_get_node_type (node)) {
        case JSON_NODE_NULL:
            checksum_u8 (checksum, (guint8) 'n');
            return TRUE;

        case JSON_NODE_OBJECT: {
            checksum_u8 (checksum, (guint8) 'o');

            JsonObject *object = json_node_get_object (
                node
            );
            GList *members = json_object_get_members (
                object
            );
            GPtrArray *sorted = g_ptr_array_new_with_free_func (
                g_free
            );

            for (GList *item = members;
                 item != NULL;
                 item = item->next) {
                const char *name = item->data;

                if (!g_utf8_validate (name, -1, NULL)) {
                    g_list_free (members);
                    g_ptr_array_unref (sorted);
                    g_set_error_literal (
                        error,
                        ATM_SCIENTIFIC_CANONICAL_ERROR,
                        ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
                        "Canonical JSON object contains a non-UTF-8 member name."
                    );
                    return FALSE;
                }

                g_ptr_array_add (
                    sorted,
                    g_strdup (name)
                );
            }

            g_list_free (members);
            g_ptr_array_sort (
                sorted,
                compare_string_pointers
            );
            checksum_u64 (
                checksum,
                (guint64) sorted->len
            );

            for (guint i = 0; i < sorted->len; i++) {
                const char *name = g_ptr_array_index (
                    sorted,
                    i
                );

                checksum_string (checksum, name);

                if (!update_json_node (
                        checksum,
                        json_object_get_member (
                            object,
                            name
                        ),
                        error
                    )) {
                    g_ptr_array_unref (sorted);
                    return FALSE;
                }
            }

            g_ptr_array_unref (sorted);
            return TRUE;
        }

        case JSON_NODE_ARRAY: {
            checksum_u8 (checksum, (guint8) 'a');

            JsonArray *array = json_node_get_array (
                node
            );
            guint length = json_array_get_length (
                array
            );

            checksum_u64 (
                checksum,
                (guint64) length
            );

            for (guint i = 0; i < length; i++) {
                if (!update_json_node (
                        checksum,
                        json_array_get_element (
                            array,
                            i
                        ),
                        error
                    )) {
                    return FALSE;
                }
            }

            return TRUE;
        }

        case JSON_NODE_VALUE: {
            GType type = json_node_get_value_type (
                node
            );

            if (type == G_TYPE_STRING) {
                const char *value = json_node_get_string (
                    node
                );

                if (value == NULL ||
                    !g_utf8_validate (
                        value,
                        -1,
                        NULL
                    )) {
                    g_set_error_literal (
                        error,
                        ATM_SCIENTIFIC_CANONICAL_ERROR,
                        ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
                        "Canonical JSON contains a non-UTF-8 string value."
                    );
                    return FALSE;
                }

                checksum_u8 (
                    checksum,
                    (guint8) 's'
                );
                checksum_string (
                    checksum,
                    value
                );
                return TRUE;
            }

            if (type == G_TYPE_BOOLEAN) {
                checksum_u8 (
                    checksum,
                    (guint8) 'b'
                );
                checksum_u8 (
                    checksum,
                    json_node_get_boolean (node)
                        ? 1
                        : 0
                );
                return TRUE;
            }

            if (type == G_TYPE_INT64) {
                checksum_u8 (
                    checksum,
                    (guint8) 'i'
                );
                checksum_i64 (
                    checksum,
                    json_node_get_int (node)
                );
                return TRUE;
            }

            if (type == G_TYPE_DOUBLE) {
                double value = json_node_get_double (
                    node
                );
                guint64 bits = 0;

                if (!atm_scientific_binary64_bits (
                        value,
                        &bits,
                        error
                    )) {
                    return FALSE;
                }

                checksum_u8 (
                    checksum,
                    (guint8) 'f'
                );
                checksum_u64 (
                    checksum,
                    bits
                );
                return TRUE;
            }

            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_CANONICAL_ERROR,
                ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
                "Canonical JSON contains an unsupported scalar type."
            );
            return FALSE;
        }

        default:
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_CANONICAL_ERROR,
                ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
                "Canonical JSON contains an unsupported node type."
            );
            return FALSE;
    }
}

gboolean
atm_scientific_decimal_parse (
    const char *text,
    AtmScientificDecimal **out_decimal,
    GError **error
)
{
    if (text == NULL ||
        out_decimal == NULL ||
        *out_decimal != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_ARGUMENT,
            "Canonical decimal parser received invalid arguments."
        );
        return FALSE;
    }

    const char *cursor = text;
    gboolean negative = FALSE;

    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        cursor++;
    }

    if (!g_ascii_isdigit (*cursor)) {
        goto invalid_decimal;
    }

    GString *digits = g_string_new (NULL);
    guint64 fractional_digits = 0;

    while (g_ascii_isdigit (*cursor)) {
        g_string_append_c (
            digits,
            *cursor
        );
        cursor++;
    }

    if (*cursor == '.') {
        cursor++;

        if (!g_ascii_isdigit (*cursor)) {
            g_string_free (digits, TRUE);
            goto invalid_decimal;
        }

        while (g_ascii_isdigit (*cursor)) {
            g_string_append_c (
                digits,
                *cursor
            );
            fractional_digits++;
            cursor++;
        }
    }

    gint64 explicit_exponent = 0;

    if (*cursor == 'e' || *cursor == 'E') {
        cursor++;
        const char *start = cursor;

        if (*cursor == '+' || *cursor == '-') {
            cursor++;
        }

        if (!g_ascii_isdigit (*cursor)) {
            g_string_free (digits, TRUE);
            goto invalid_decimal;
        }

        while (g_ascii_isdigit (*cursor)) {
            cursor++;
        }

        char *exponent_text = g_strndup (
            start,
            cursor - start
        );
        errno = 0;
        char *end = NULL;
        explicit_exponent = g_ascii_strtoll (
            exponent_text,
            &end,
            10
        );

        gboolean exponent_ok =
            errno != ERANGE &&
            end != NULL &&
            *end == '\0';

        g_free (exponent_text);

        if (!exponent_ok) {
            g_string_free (digits, TRUE);
            goto invalid_decimal;
        }
    }

    if (*cursor != '\0' ||
        fractional_digits > G_MAXINT64) {
        g_string_free (digits, TRUE);
        goto invalid_decimal;
    }

    if (explicit_exponent <
        G_MININT64 + (gint64) fractional_digits) {
        g_string_free (digits, TRUE);
        goto invalid_decimal;
    }

    gint64 exponent =
        explicit_exponent -
        (gint64) fractional_digits;

    guint first_nonzero = 0;

    while (first_nonzero < digits->len &&
           digits->str[first_nonzero] == '0') {
        first_nonzero++;
    }

    if (first_nonzero == digits->len) {
        g_string_free (digits, TRUE);

        AtmScientificDecimal *zero = g_new0 (
            AtmScientificDecimal,
            1
        );
        zero->coefficient = g_strdup ("0");
        zero->exponent = 0;
        *out_decimal = zero;
        return TRUE;
    }

    if (first_nonzero > 0) {
        g_string_erase (
            digits,
            0,
            first_nonzero
        );
    }

    while (digits->len > 1 &&
           digits->str[digits->len - 1] == '0') {
        if (exponent == G_MAXINT64) {
            g_string_free (digits, TRUE);
            goto invalid_decimal;
        }

        g_string_truncate (
            digits,
            digits->len - 1
        );
        exponent++;
    }

    AtmScientificDecimal *decimal = g_new0 (
        AtmScientificDecimal,
        1
    );

    decimal->coefficient = negative
        ? g_strconcat (
            "-",
            digits->str,
            NULL
        )
        : g_strdup (digits->str);
    decimal->exponent = exponent;

    g_string_free (digits, TRUE);
    *out_decimal = decimal;
    return TRUE;

invalid_decimal:
    g_set_error (
        error,
        ATM_SCIENTIFIC_CANONICAL_ERROR,
        ATM_SCIENTIFIC_CANONICAL_ERROR_DECIMAL,
        "Value '%s' is not a supported exact decimal.",
        text
    );
    return FALSE;
}

void
atm_scientific_decimal_free (
    AtmScientificDecimal *decimal
)
{
    if (decimal == NULL) {
        return;
    }

    g_free (decimal->coefficient);
    g_free (decimal);
}

gboolean
atm_scientific_binary64_bits (
    double value,
    guint64 *out_bits,
    GError **error
)
{
    if (out_bits == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_ARGUMENT,
            "Binary64 canonicalizer received no output location."
        );
        return FALSE;
    }

    if (!isfinite (value)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_NUMBER,
            "Canonical binary64 values must be finite."
        );
        return FALSE;
    }

    memcpy (
        out_bits,
        &value,
        sizeof value
    );
    return TRUE;
}

gboolean
atm_scientific_content_id_json (
    const char *artifact_class,
    const char *json_payload,
    char **out_id,
    GError **error
)
{
    if (!nonempty_utf8 (artifact_class) ||
        !nonempty_utf8 (json_payload) ||
        out_id == NULL ||
        *out_id != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_ARGUMENT,
            "Scientific content-ID JSON input is invalid."
        );
        return FALSE;
    }

    JsonParser *parser = json_parser_new ();
    GError *parse_error = NULL;

    if (!json_parser_load_from_data (
            parser,
            json_payload,
            -1,
            &parse_error
        )) {
        g_set_error (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
            "Scientific content JSON could not be parsed: %s.",
            parse_error != NULL
                ? parse_error->message
                : "unknown JSON error"
        );
        g_clear_error (&parse_error);
        g_object_unref (parser);
        return FALSE;
    }

    GChecksum *checksum = g_checksum_new (
        G_CHECKSUM_SHA256
    );

    g_checksum_update (
        checksum,
        CONTENT_DOMAIN,
        sizeof CONTENT_DOMAIN - 1
    );
    checksum_u8 (checksum, (guint8) 'j');
    checksum_string (
        checksum,
        artifact_class
    );

    if (!update_json_node (
            checksum,
            json_parser_get_root (parser),
            error
        )) {
        g_checksum_free (checksum);
        g_object_unref (parser);
        return FALSE;
    }

    *out_id = finish_checksum (checksum);
    g_object_unref (parser);
    return TRUE;
}

gboolean
atm_scientific_content_id_scalar (
    const char *artifact_class,
    const char *scalar_type,
    const char *canonical_value,
    char **out_id,
    GError **error
)
{
    if (!nonempty_utf8 (artifact_class) ||
        !nonempty_utf8 (scalar_type) ||
        !nonempty_utf8 (canonical_value) ||
        out_id == NULL ||
        *out_id != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_ARGUMENT,
            "Scientific content-ID scalar input is invalid."
        );
        return FALSE;
    }

    GChecksum *checksum = g_checksum_new (
        G_CHECKSUM_SHA256
    );

    g_checksum_update (
        checksum,
        CONTENT_DOMAIN,
        sizeof CONTENT_DOMAIN - 1
    );
    checksum_u8 (checksum, (guint8) 'v');
    checksum_string (
        checksum,
        artifact_class
    );
    checksum_string (
        checksum,
        scalar_type
    );
    checksum_string (
        checksum,
        canonical_value
    );

    *out_id = finish_checksum (checksum);
    return TRUE;
}

gboolean
atm_scientific_qualified_artifact_id (
    const char *scientific_content_id,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path,
    const char *locator,
    const char *logical_source_id,
    const char *profile_id,
    const char *profile_version,
    char **out_id,
    GError **error
)
{
    if (!lower_hex_is_valid (
            scientific_content_id,
            64
        ) ||
        !nonempty_utf8 (repository_id) ||
        !optional_utf8 (repository_version) ||
        !lower_hex_is_valid (snapshot_sha, 40) ||
        !nonempty_utf8 (source_path) ||
        !nonempty_utf8 (locator) ||
        !optional_utf8 (logical_source_id) ||
        !optional_utf8 (profile_id) ||
        !optional_utf8 (profile_version) ||
        out_id == NULL ||
        *out_id != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_CANONICAL_ERROR,
            ATM_SCIENTIFIC_CANONICAL_ERROR_ARGUMENT,
            "Qualified-artifact identity input is invalid."
        );
        return FALSE;
    }

    GChecksum *checksum = g_checksum_new (
        G_CHECKSUM_SHA256
    );

    g_checksum_update (
        checksum,
        QUALIFIED_DOMAIN,
        sizeof QUALIFIED_DOMAIN - 1
    );
    checksum_string (
        checksum,
        scientific_content_id
    );
    checksum_string (
        checksum,
        repository_id
    );
    checksum_string (
        checksum,
        repository_version
    );
    checksum_string (
        checksum,
        snapshot_sha
    );
    checksum_string (
        checksum,
        source_path
    );
    checksum_string (
        checksum,
        locator
    );
    checksum_string (
        checksum,
        logical_source_id
    );
    checksum_string (
        checksum,
        profile_id
    );
    checksum_string (
        checksum,
        profile_version
    );

    *out_id = finish_checksum (checksum);
    return TRUE;
}
