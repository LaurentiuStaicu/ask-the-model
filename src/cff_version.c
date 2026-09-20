#include "cff_version.h"

#include <yaml.h>

#include <string.h>

GQuark
atm_cff_error_quark (void)
{
    return g_quark_from_static_string ("atm-cff-error-quark");
}

static gboolean
node_scalar_equals (yaml_node_t *node, const char *expected)
{
    gsize expected_length;

    if (node == NULL || node->type != YAML_SCALAR_NODE) {
        return FALSE;
    }

    expected_length = strlen (expected);

    return node->data.scalar.length == expected_length &&
        memcmp (
            node->data.scalar.value,
            expected,
            expected_length
        ) == 0;
}

gboolean
atm_cff_extract_version (
    const guint8 *data,
    gsize length,
    gchar **out_version,
    GError **error
)
{
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_document_t extra_document;
    yaml_node_t *root;
    yaml_node_t *extra_root;
    yaml_node_pair_t *pair;
    gchar *version = NULL;
    gboolean parser_initialized = FALSE;
    gboolean document_loaded = FALSE;
    gboolean extra_document_loaded = FALSE;
    gboolean ok = FALSE;

    g_return_val_if_fail (data != NULL, FALSE);
    g_return_val_if_fail (out_version != NULL, FALSE);
    g_return_val_if_fail (*out_version == NULL, FALSE);

    if (!yaml_parser_initialize (&parser)) {
        g_set_error_literal (
            error,
            ATM_CFF_ERROR,
            ATM_CFF_ERROR_PARSE,
            "Could not initialize the YAML parser."
        );
        return FALSE;
    }

    parser_initialized = TRUE;
    yaml_parser_set_input_string (
        &parser,
        (const unsigned char *) data,
        length
    );

    if (!yaml_parser_load (&parser, &document)) {
        g_set_error (
            error,
            ATM_CFF_ERROR,
            ATM_CFF_ERROR_PARSE,
            "Could not parse CITATION.cff YAML at line %lu, column %lu.",
            (unsigned long) parser.problem_mark.line + 1,
            (unsigned long) parser.problem_mark.column + 1
        );
        goto out;
    }

    document_loaded = TRUE;
    root = yaml_document_get_root_node (&document);

    if (root == NULL || root->type != YAML_MAPPING_NODE) {
        g_set_error_literal (
            error,
            ATM_CFF_ERROR,
            ATM_CFF_ERROR_ROOT,
            "CITATION.cff must contain one top-level YAML mapping."
        );
        goto out;
    }

    for (
        pair = root->data.mapping.pairs.start;
        pair < root->data.mapping.pairs.top;
        pair++
    ) {
        yaml_node_t *key =
            yaml_document_get_node (&document, pair->key);
        yaml_node_t *value =
            yaml_document_get_node (&document, pair->value);

        if (!node_scalar_equals (key, "version")) {
            continue;
        }

        if (version != NULL) {
            g_set_error_literal (
                error,
                ATM_CFF_ERROR,
                ATM_CFF_ERROR_DUPLICATE_VERSION,
                "CITATION.cff contains more than one top-level version key."
            );
            goto out;
        }

        if (value == NULL || value->type != YAML_SCALAR_NODE) {
            g_set_error_literal (
                error,
                ATM_CFF_ERROR,
                ATM_CFF_ERROR_INVALID_VERSION,
                "CITATION.cff version must be a scalar value."
            );
            goto out;
        }

        version = g_strndup (
            (const gchar *) value->data.scalar.value,
            value->data.scalar.length
        );
        g_strstrip (version);

        if (version[0] == '\0') {
            g_set_error_literal (
                error,
                ATM_CFF_ERROR,
                ATM_CFF_ERROR_INVALID_VERSION,
                "CITATION.cff version must not be empty."
            );
            goto out;
        }
    }

    if (version == NULL) {
        g_set_error_literal (
            error,
            ATM_CFF_ERROR,
            ATM_CFF_ERROR_MISSING_VERSION,
            "CITATION.cff has no top-level version key."
        );
        goto out;
    }

    if (!yaml_parser_load (&parser, &extra_document)) {
        g_set_error (
            error,
            ATM_CFF_ERROR,
            ATM_CFF_ERROR_PARSE,
            "Could not finish parsing the CITATION.cff YAML stream at line %lu, column %lu.",
            (unsigned long) parser.problem_mark.line + 1,
            (unsigned long) parser.problem_mark.column + 1
        );
        goto out;
    }

    extra_document_loaded = TRUE;
    extra_root = yaml_document_get_root_node (&extra_document);

    if (extra_root != NULL) {
        g_set_error_literal (
            error,
            ATM_CFF_ERROR,
            ATM_CFF_ERROR_EXTRA_DOCUMENT,
            "CITATION.cff must contain exactly one YAML document."
        );
        goto out;
    }

    *out_version = g_steal_pointer (&version);
    ok = TRUE;

out:
    g_clear_pointer (&version, g_free);

    if (extra_document_loaded) {
        yaml_document_delete (&extra_document);
    }

    if (document_loaded) {
        yaml_document_delete (&document);
    }

    if (parser_initialized) {
        yaml_parser_delete (&parser);
    }

    return ok;
}
