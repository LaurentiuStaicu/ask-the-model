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
    g_string_append (input, "prefix ");
    for (guint i = 0; i < 4097; i++)
        g_string_append_c (input, '`');
    g_string_append (input, " payload");

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

static void
test_dangerous_link_destinations_are_inert (void)
{
    const char *input =
        "[js](javascript:alert) "
        "[data](data:text/plain,x) "
        "[file](file:///tmp/x)";

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_cmpstr (plain, ==, "js data file");
    g_assert_null (strstr (plain, "javascript:"));
    g_assert_null (strstr (plain, "data:"));
    g_assert_null (strstr (plain, "file:"));

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_image_sources_are_inert (void)
{
    const char *input =
        "![remote](https://example.test/x.png) "
        "![local](file:///tmp/x.png)";

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_cmpstr (plain, ==, "remote local");
    g_assert_null (strstr (plain, "https://"));
    g_assert_null (strstr (plain, "file:"));

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_entities_remain_verbatim_text (void)
{
    const char *input = "A &amp; B &#35; C &#x41;";

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_cmpstr (plain, ==, input);

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_unicode_controls_and_joiners_are_preserved (void)
{
    const char *input =
        "é | العربية | 👩‍🔬 | "
        "\xE2\x80\xAE" "abc" "\xE2\x80\xAC";

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_true (g_utf8_validate (plain, -1, NULL));
    g_assert_cmpstr (plain, ==, input);

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_very_long_line_is_not_truncated (void)
{
    GString *input = g_string_sized_new (65540);
    for (guint i = 0; i < 65536; i++)
        g_string_append_c (input, 'x');
    g_string_append (input, "END");

    AtmPresentationDocument *document =
        normalize_bytes (input->str, input->len);
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_cmpuint (strlen (plain), ==, input->len);
    g_assert_cmpstr (plain, ==, input->str);

    g_free (plain);
    atm_presentation_document_free (document);
    g_string_free (input, TRUE);
}

static void
test_table_syntax_stays_plain_without_extension (void)
{
    const char *input =
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |";

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
        ATM_PRESENTATION_BLOCK_PARAGRAPH
    );
    g_assert_nonnull (strstr (plain, "| A | B |"));
    g_assert_nonnull (strstr (plain, "|---|---|"));
    g_assert_nonnull (strstr (plain, "| 1 | 2 |"));

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_malformed_link_retains_visible_payload (void)
{
    const char *input = "[label](javascript:";

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));
    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_false (document->fallback);
    g_assert_nonnull (strstr (plain, "label"));

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_normalizer_does_not_mutate_input (void)
{
    char input[] = "## Titlu\n\n- unu\n- doi";
    char before[sizeof (input)];
    memcpy (before, input, sizeof (input));

    AtmPresentationDocument *document =
        normalize_bytes (input, strlen (input));

    g_assert_cmpmem (
        input,
        sizeof (input),
        before,
        sizeof (before)
    );

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
    g_test_add_func (
        "/presentation-adversarial/dangerous-link-destinations",
        test_dangerous_link_destinations_are_inert
    );
    g_test_add_func (
        "/presentation-adversarial/image-sources",
        test_image_sources_are_inert
    );
    g_test_add_func (
        "/presentation-adversarial/entities-verbatim",
        test_entities_remain_verbatim_text
    );
    g_test_add_func (
        "/presentation-adversarial/unicode-controls-joiners",
        test_unicode_controls_and_joiners_are_preserved
    );
    g_test_add_func (
        "/presentation-adversarial/very-long-line",
        test_very_long_line_is_not_truncated
    );
    g_test_add_func (
        "/presentation-adversarial/table-syntax-plain",
        test_table_syntax_stays_plain_without_extension
    );
    g_test_add_func (
        "/presentation-adversarial/malformed-link",
        test_malformed_link_retains_visible_payload
    );
    g_test_add_func (
        "/presentation-adversarial/input-immutable",
        test_normalizer_does_not_mutate_input
    );

    return g_test_run ();
}
