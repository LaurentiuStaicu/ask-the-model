#include <glib.h>

#include "presentation_document.h"

static gchar *
join_inlines(GPtrArray *inlines)
{
    GString *joined = g_string_new(NULL);
    for (guint i = 0; i < inlines->len; i++) {
        AtmPresentationInline *node = g_ptr_array_index(inlines, i);
        g_string_append(joined, node->text);
    }
    return g_string_free(joined, FALSE);
}

static void
test_basic_projection(void)
{
    const gchar *input =
        "# Heading\n\n"
        "Hello **bold** and *emphasis* plus "
        "[link](https://example.test) and `code`.";

    AtmPresentationDocument *document =
        atm_presentation_document_normalize(input, -1, NULL);

    g_assert_nonnull(document);
    g_assert_false(document->fallback_used);
    g_assert_cmpstr(
        document->normalizer_schema,
        ==,
        ATM_PRESENTATION_NORMALIZER_SCHEMA
    );
    g_assert_cmpuint(document->blocks->len, ==, 2);

    AtmPresentationBlock *heading =
        g_ptr_array_index(document->blocks, 0);
    AtmPresentationBlock *paragraph =
        g_ptr_array_index(document->blocks, 1);

    g_assert_cmpint(
        heading->kind,
        ==,
        ATM_PRESENTATION_BLOCK_HEADING
    );
    g_assert_cmpuint(heading->level, ==, 1);

    gchar *heading_text = join_inlines(heading->inlines);
    g_assert_cmpstr(heading_text, ==, "Heading");
    g_free(heading_text);

    g_assert_cmpint(
        paragraph->kind,
        ==,
        ATM_PRESENTATION_BLOCK_PARAGRAPH
    );
    g_assert_cmpuint(paragraph->inlines->len, ==, 3);

    AtmPresentationInline *code =
        g_ptr_array_index(paragraph->inlines, 1);
    g_assert_cmpint(code->kind, ==, ATM_PRESENTATION_INLINE_CODE);
    g_assert_cmpstr(code->text, ==, "code");

    gchar *paragraph_text = join_inlines(paragraph->inlines);
    g_assert_cmpstr(
        paragraph_text,
        ==,
        "Hello bold and emphasis plus link and code."
    );
    g_free(paragraph_text);

    atm_presentation_document_free(document);
}

static void
test_structural_blocks(void)
{
    const gchar *input =
        "> Quoted text\n\n"
        "- first\n"
        "  - nested\n\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | `2` |\n\n"
        "---\n";

    AtmPresentationDocument *document =
        atm_presentation_document_normalize(input, -1, NULL);

    g_assert_false(document->fallback_used);
    g_assert_cmpuint(document->blocks->len, ==, 4);

    AtmPresentationBlock *quote =
        g_ptr_array_index(document->blocks, 0);
    AtmPresentationBlock *list =
        g_ptr_array_index(document->blocks, 1);
    AtmPresentationBlock *table =
        g_ptr_array_index(document->blocks, 2);
    AtmPresentationBlock *spacer =
        g_ptr_array_index(document->blocks, 3);

    g_assert_cmpint(quote->kind, ==, ATM_PRESENTATION_BLOCK_QUOTE);
    g_assert_cmpuint(quote->children->len, ==, 1);

    g_assert_cmpint(list->kind, ==, ATM_PRESENTATION_BLOCK_LIST);
    g_assert_cmpuint(list->list_items->len, ==, 1);

    AtmPresentationListItem *item =
        g_ptr_array_index(list->list_items, 0);
    g_assert_cmpuint(item->blocks->len, >=, 2);

    AtmPresentationBlock *nested_list = NULL;
    for (guint i = 0; i < item->blocks->len; i++) {
        AtmPresentationBlock *candidate =
            g_ptr_array_index(item->blocks, i);
        if (candidate->kind == ATM_PRESENTATION_BLOCK_LIST) {
            nested_list = candidate;
            break;
        }
    }
    g_assert_nonnull(nested_list);
    g_assert_cmpuint(nested_list->level, ==, 2);

    g_assert_cmpint(table->kind, ==, ATM_PRESENTATION_BLOCK_TABLE);
    g_assert_cmpuint(table->table_rows->len, ==, 2);
    AtmPresentationTableRow *body_row =
        g_ptr_array_index(table->table_rows, 1);
    AtmPresentationTableCell *code_cell =
        g_ptr_array_index(body_row->cells, 1);
    g_assert_cmpuint(code_cell->inlines->len, ==, 1);
    AtmPresentationInline *cell_inline =
        g_ptr_array_index(code_cell->inlines, 0);
    g_assert_cmpint(
        cell_inline->kind,
        ==,
        ATM_PRESENTATION_INLINE_CODE
    );

    g_assert_cmpint(spacer->kind, ==, ATM_PRESENTATION_BLOCK_SPACER);

    atm_presentation_document_free(document);
}

static void
test_input_budget_falls_back_without_truncation(void)
{
    const gchar *input = "alpha beta gamma";
    AtmPresentationBudget budget = {0};
    budget.max_input_bytes = 4;

    AtmPresentationDocument *document =
        atm_presentation_document_normalize(input, -1, &budget);

    g_assert_true(document->fallback_used);
    g_assert_cmpint(
        document->fallback_reason,
        ==,
        ATM_PRESENTATION_FALLBACK_INPUT_BUDGET
    );
    g_assert_cmpuint(document->blocks->len, ==, 1);

    AtmPresentationBlock *block =
        g_ptr_array_index(document->blocks, 0);
    gchar *text = join_inlines(block->inlines);
    g_assert_cmpstr(text, ==, input);
    g_free(text);

    atm_presentation_document_free(document);
}

static void
test_block_budget_falls_back_to_full_input(void)
{
    const gchar *input = "one\n\ntwo";
    AtmPresentationBudget budget = {0};
    budget.max_blocks = 1;

    AtmPresentationDocument *document =
        atm_presentation_document_normalize(input, -1, &budget);

    g_assert_true(document->fallback_used);
    g_assert_cmpint(
        document->fallback_reason,
        ==,
        ATM_PRESENTATION_FALLBACK_BLOCK_BUDGET
    );

    AtmPresentationBlock *block =
        g_ptr_array_index(document->blocks, 0);
    gchar *text = join_inlines(block->inlines);
    g_assert_cmpstr(text, ==, input);
    g_free(text);

    atm_presentation_document_free(document);
}

static void
test_invalid_utf8_uses_safe_full_fallback(void)
{
    const gchar input[] = { 'A', (gchar) 0xff, 'B' };

    AtmPresentationDocument *document =
        atm_presentation_document_normalize(input, 3, NULL);

    g_assert_true(document->fallback_used);
    g_assert_cmpint(
        document->fallback_reason,
        ==,
        ATM_PRESENTATION_FALLBACK_INVALID_UTF8
    );
    g_assert_cmpuint(document->counters.input_bytes, ==, 3);

    AtmPresentationBlock *block =
        g_ptr_array_index(document->blocks, 0);
    gchar *text = join_inlines(block->inlines);
    g_assert_true(g_utf8_validate(text, -1, NULL));
    g_assert_true(g_str_has_prefix(text, "A"));
    g_assert_true(g_str_has_suffix(text, "B"));
    g_free(text);

    atm_presentation_document_free(document);
}

static void
test_html_and_indented_code_are_not_active_features(void)
{
    const gchar *input =
        "<b>literal</b>\n\n"
        "    not a code block\n";

    AtmPresentationDocument *document =
        atm_presentation_document_normalize(input, -1, NULL);

    g_assert_false(document->fallback_used);

    for (guint i = 0; i < document->blocks->len; i++) {
        AtmPresentationBlock *block =
            g_ptr_array_index(document->blocks, i);
        g_assert_cmpint(
            block->kind,
            !=,
            ATM_PRESENTATION_BLOCK_CODE_BLOCK
        );
    }

    atm_presentation_document_free(document);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func(
        "/presentation/basic-projection",
        test_basic_projection
    );
    g_test_add_func(
        "/presentation/structural-blocks",
        test_structural_blocks
    );
    g_test_add_func(
        "/presentation/input-budget-fallback",
        test_input_budget_falls_back_without_truncation
    );
    g_test_add_func(
        "/presentation/block-budget-fallback",
        test_block_budget_falls_back_to_full_input
    );
    g_test_add_func(
        "/presentation/invalid-utf8-fallback",
        test_invalid_utf8_uses_safe_full_fallback
    );
    g_test_add_func(
        "/presentation/parser-flags",
        test_html_and_indented_code_are_not_active_features
    );

    return g_test_run();
}
