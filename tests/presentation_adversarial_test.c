#include "presentation_normalize.h"

#include <glib.h>
#include <string.h>

static AtmPresentationDocument *
normalize_bytes (const char *input, gsize length)
{
    AtmPresentationDocument *document = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_presentation_normalize (
            input,
            length,
            &document,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (document);
    return document;
}

static void
test_embedded_nul_keeps_tail (void)
{
    const char input[] = { 'A', '\0', 'B' };
    AtmPresentationDocument *document =
        normalize_bytes (input, sizeof (input));

    g_assert_true (document->fallback);
    g_assert_cmpstr (
        document->fallback_reason,
        ==,
        "invalid-utf8"
    );

    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );
    g_assert_cmpstr (
        plain,
        ==,
        "A\xEF\xBF\xBD" "B"
    );

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_deep_blockquote_is_bounded_by_input (void)
{
    GString *input = g_string_new (NULL);
    for (guint i = 0; i < 64; i++)
        g_string_append (input, "> ");
    g_string_append (input, "payload");

    AtmPresentationDocument *document =
        normalize_bytes (input->str, input->len);

    g_assert_false (document->fallback);
    g_assert_cmpuint (document->blocks->len, ==, 1);

    AtmPresentationBlock *block =
        g_ptr_array_index (document->blocks, 0);
    g_assert_cmpint (
        block->type,
        ==,
        ATM_PRESENTATION_BLOCK_QUOTE
    );
    g_assert_cmpuint (block->quote_depth, ==, 64);

    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );
    g_assert_cmpstr (plain, ==, "payload");

    g_free (plain);
    atm_presentation_document_free (document);
    g_string_free (input, TRUE);
}

static void
test_long_unmatched_delimiter_retains_payload (void)
{
    GString *input = g_string_sized_new (8192);
    for (guint i = 0; i < 4097; i++)
        g_string_append_c (input, '`');
    g_string_append (input, "payload");

    AtmPresentationDocument *document =
        normalize_bytes (input->str, input->len);
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_true (g_utf8_validate (plain, -1, NULL));
    g_assert_nonnull (strstr (plain, "payload"));

    g_free (plain);
    atm_presentation_document_free (document);
    g_string_free (input, TRUE);
}

static void
test_long_link_destination_does_not_replace_label (void)
{
    GString *input = g_string_new ("[visible](https://example.test/");
    for (guint i = 0; i < 8192; i++)
        g_string_append_c (input, 'a');
    g_string_append_c (input, ')');

    AtmPresentationDocument *document =
        normalize_bytes (input->str, input->len);
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_cmpstr (plain, ==, "visible");

    g_free (plain);
    atm_presentation_document_free (document);
    g_string_free (input, TRUE);
}

static void
test_unclosed_fence_preserves_code_text (void)
{
    const char *input =
        "```text\n"
        "**literal syntax remains code**\n"
        "tail\n";

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_cmpuint (document->blocks->len, ==, 1);
    AtmPresentationBlock *block =
        g_ptr_array_index (document->blocks, 0);
    g_assert_cmpint (
        block->type,
        ==,
        ATM_PRESENTATION_BLOCK_CODE
    );
    g_assert_nonnull (
        strstr (
            plain,
            "**literal syntax remains code**"
        )
    );
    g_assert_nonnull (strstr (plain, "tail"));

    g_free (plain);
    atm_presentation_document_free (document);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/presentation-adversarial/embedded-nul-tail",
        test_embedded_nul_keeps_tail
    );
    g_test_add_func (
        "/presentation-adversarial/deep-blockquote",
        test_deep_blockquote_is_bounded_by_input
    );
    g_test_add_func (
        "/presentation-adversarial/long-unmatched-delimiter",
        test_long_unmatched_delimiter_retains_payload
    );
    g_test_add_func (
        "/presentation-adversarial/long-link-destination",
        test_long_link_destination_does_not_replace_label
    );
    g_test_add_func (
        "/presentation-adversarial/unclosed-fence",
        test_unclosed_fence_preserves_code_text
    );

    return g_test_run ();
}
