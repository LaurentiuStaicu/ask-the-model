#include "presentation_normalize.h"

#include <glib.h>
#include <string.h>

static AtmPresentationDocument *
normalize_text (const char *text)
{
    AtmPresentationDocument *document = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_presentation_normalize (
            text,
            strlen (text),
            &document,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (document);

    return document;
}

static void
test_emphasis_removed_literal_star_preserved (void)
{
    AtmPresentationDocument *document = normalize_text (
        "Cost = 2 * 3, iar **rezultatul** este 6."
    );
    char *plain = atm_presentation_document_to_plain_text (
        document
    );

    g_assert_false (document->fallback);
    g_assert_cmpstr (
        plain,
        ==,
        "Cost = 2 * 3, iar rezultatul este 6."
    );

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_cbd_list_normalization (void)
{
    AtmPresentationDocument *document = normalize_text (
        "*   **Efectele expunerilor repetate:** Cum pot întâlnirile repetitive...\n"
        "*   **Timpul corecțiilor:** Cum o corecție poate avea..."
    );
    char *plain = atm_presentation_document_to_plain_text (
        document
    );

    g_assert_cmpuint (document->blocks->len, ==, 2);

    AtmPresentationBlock *first = g_ptr_array_index (
        document->blocks,
        0
    );
    AtmPresentationBlock *second = g_ptr_array_index (
        document->blocks,
        1
    );

    g_assert_cmpint (
        first->type,
        ==,
        ATM_PRESENTATION_BLOCK_LIST_ITEM
    );
    g_assert_false (first->ordered);
    g_assert_cmpuint (first->nesting_level, ==, 1);
    g_assert_cmpint (
        second->type,
        ==,
        ATM_PRESENTATION_BLOCK_LIST_ITEM
    );

    g_assert_cmpstr (
        plain,
        ==,
        "• Efectele expunerilor repetate: Cum pot întâlnirile repetitive...\n"
        "• Timpul corecțiilor: Cum o corecție poate avea..."
    );

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_heading_and_soft_break (void)
{
    AtmPresentationDocument *document = normalize_text (
        "## Formarea convingerilor\n\n"
        "Convingerile se formează\n"
        "prin interacțiunea mai multor factori."
    );
    char *plain = atm_presentation_document_to_plain_text (
        document
    );

    g_assert_cmpuint (document->blocks->len, ==, 2);

    AtmPresentationBlock *heading = g_ptr_array_index (
        document->blocks,
        0
    );

    g_assert_cmpint (
        heading->type,
        ==,
        ATM_PRESENTATION_BLOCK_HEADING
    );
    g_assert_cmpuint (heading->heading_level, ==, 2);

    g_assert_cmpstr (
        plain,
        ==,
        "Formarea convingerilor\n\n"
        "Convingerile se formează prin interacțiunea mai multor factori."
    );

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_inline_and_fenced_code_are_verbatim (void)
{
    AtmPresentationDocument *document = normalize_text (
        "Folosește \x60**x**\x60 exact.\n\n"
        "\x60\x60\x60python\n"
        "files = \"*.csv\"\n"
        "x = \"**not bold**\"\n"
        "\x60\x60\x60"
    );
    char *plain = atm_presentation_document_to_plain_text (
        document
    );

    g_assert_cmpuint (document->blocks->len, ==, 2);

    AtmPresentationBlock *paragraph = g_ptr_array_index (
        document->blocks,
        0
    );
    AtmPresentationBlock *code = g_ptr_array_index (
        document->blocks,
        1
    );

    g_assert_cmpuint (paragraph->segments->len, ==, 3);
    g_assert_cmpint (
        ((AtmPresentationSegment *) g_ptr_array_index (
            paragraph->segments,
            1
        ))->type,
        ==,
        ATM_PRESENTATION_SEGMENT_INLINE_CODE
    );
    g_assert_cmpint (
        code->type,
        ==,
        ATM_PRESENTATION_BLOCK_CODE
    );
    g_assert_cmpstr (code->info, ==, "python");

    g_assert_cmpstr (
        plain,
        ==,
        "Folosește **x** exact.\n\n"
        "files = \"*.csv\"\n"
        "x = \"**not bold**\"\n"
    );

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_html_is_inert_text (void)
{
    AtmPresentationDocument *document = normalize_text (
        "<b>text</b> <script>alert(1)</script>"
    );
    char *plain = atm_presentation_document_to_plain_text (
        document
    );

    g_assert_cmpstr (
        plain,
        ==,
        "<b>text</b> <script>alert(1)</script>"
    );

    g_free (plain);
    atm_presentation_document_free (document);
}

static void
test_invalid_utf8_uses_full_fallback (void)
{
    const char invalid[] = {
        'A',
        (char) 0xff,
        'B'
    };
    AtmPresentationDocument *document = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_presentation_normalize (
            invalid,
            sizeof (invalid),
            &document,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (document);
    g_assert_true (document->fallback);
    g_assert_cmpstr (
        document->fallback_reason,
        ==,
        "invalid-utf8"
    );

    char *plain = atm_presentation_document_to_plain_text (
        document
    );

    g_assert_true (g_utf8_validate (plain, -1, NULL));
    g_assert_nonnull (strstr (plain, "A"));
    g_assert_nonnull (strstr (plain, "B"));

    g_free (plain);
    atm_presentation_document_free (document);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/presentation/emphasis-literal-star",
        test_emphasis_removed_literal_star_preserved
    );
    g_test_add_func (
        "/presentation/cbd-list",
        test_cbd_list_normalization
    );
    g_test_add_func (
        "/presentation/heading-softbreak",
        test_heading_and_soft_break
    );
    g_test_add_func (
        "/presentation/code-verbatim",
        test_inline_and_fenced_code_are_verbatim
    );
    g_test_add_func (
        "/presentation/html-inert",
        test_html_is_inert_text
    );
    g_test_add_func (
        "/presentation/invalid-utf8",
        test_invalid_utf8_uses_full_fallback
    );

    return g_test_run ();
}
