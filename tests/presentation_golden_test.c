#include "presentation_normalize.h"

#include <glib.h>
#include <json-glib/json-glib.h>
#include <string.h>

static const char *
block_type_name (AtmPresentationBlockType type)
{
    switch (type) {
    case ATM_PRESENTATION_BLOCK_PARAGRAPH:
        return "paragraph";
    case ATM_PRESENTATION_BLOCK_HEADING:
        return "heading";
    case ATM_PRESENTATION_BLOCK_LIST_ITEM:
        return "list-item";
    case ATM_PRESENTATION_BLOCK_QUOTE:
        return "quote";
    case ATM_PRESENTATION_BLOCK_CODE:
        return "code";
    case ATM_PRESENTATION_BLOCK_SPACER:
        return "spacer";
    default:
        return "unknown";
    }
}

static void
run_case (JsonObject *object)
{
    const char *name = json_object_get_string_member (
        object,
        "name"
    );
    const char *input = json_object_get_string_member (
        object,
        "input"
    );
    const char *expected_text =
        json_object_get_string_member (
            object,
            "visible_text"
        );
    gboolean expected_fallback =
        json_object_get_boolean_member (
            object,
            "fallback"
        );
    JsonArray *expected_blocks =
        json_object_get_array_member (
            object,
            "block_types"
        );

    AtmPresentationDocument *document = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_presentation_normalize (
            input,
            strlen (input),
            &document,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (document);

    if (document->fallback != expected_fallback) {
        g_test_message (
            "case=%s expected fallback=%d actual=%d",
            name,
            expected_fallback,
            document->fallback
        );
    }
    g_assert_cmpint (
        document->fallback,
        ==,
        expected_fallback
    );

    char *visible =
        atm_presentation_document_to_plain_text (
            document
        );

    if (g_strcmp0 (visible, expected_text) != 0) {
        g_test_message (
            "case=%s\nexpected=[%s]\nactual=[%s]",
            name,
            expected_text,
            visible
        );
    }
    g_assert_cmpstr (
        visible,
        ==,
        expected_text
    );

    guint expected_len =
        json_array_get_length (expected_blocks);

    if (document->blocks->len != expected_len) {
        g_test_message (
            "case=%s expected blocks=%u actual=%u",
            name,
            expected_len,
            document->blocks->len
        );
    }
    g_assert_cmpuint (
        document->blocks->len,
        ==,
        expected_len
    );

    for (guint i = 0; i < expected_len; i++) {
        const char *expected =
            json_array_get_string_element (
                expected_blocks,
                i
            );
        AtmPresentationBlock *block =
            g_ptr_array_index (
                document->blocks,
                i
            );
        const char *actual =
            block_type_name (block->type);

        if (g_strcmp0 (actual, expected) != 0) {
            g_test_message (
                "case=%s block=%u expected=%s actual=%s",
                name,
                i,
                expected,
                actual
            );
        }

        g_assert_cmpstr (
            actual,
            ==,
            expected
        );
    }

    g_free (visible);
    atm_presentation_document_free (document);
}

static void
test_golden_corpus (void)
{
    const char *path = g_getenv (
        "ATM_PRESENTATION_GOLDEN_V1"
    );
    g_assert_nonnull (path);

    JsonParser *parser = json_parser_new ();
    GError *error = NULL;

    g_assert_true (
        json_parser_load_from_file (
            parser,
            path,
            &error
        )
    );
    g_assert_no_error (error);

    JsonNode *root = json_parser_get_root (parser);
    g_assert_true (JSON_NODE_HOLDS_OBJECT (root));

    JsonObject *root_object =
        json_node_get_object (root);

    g_assert_cmpstr (
        json_object_get_string_member (
            root_object,
            "schema"
        ),
        ==,
        "atm-presentation-golden/1"
    );

    JsonArray *cases = json_object_get_array_member (
        root_object,
        "cases"
    );
    g_assert_nonnull (cases);
    g_assert_cmpuint (
        json_array_get_length (cases),
        >=,
        20
    );

    for (guint i = 0;
         i < json_array_get_length (cases);
         i++) {
        JsonObject *object =
            json_array_get_object_element (
                cases,
                i
            );

        g_assert_nonnull (object);
        run_case (object);
    }

    g_object_unref (parser);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/presentation/golden-v1",
        test_golden_corpus
    );

    return g_test_run ();
}
