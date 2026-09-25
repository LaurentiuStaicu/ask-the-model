#include "presentation_document.h"

#include <limits.h>
#include <string.h>

#include "md4c.h"

typedef struct {
    AtmPresentationBlock *block;
    gboolean ordered;
    guint next_ordinal;
} ListContext;

typedef struct {
    AtmPresentationDocument *document;
    AtmPresentationBudget budget;
    GPtrArray *destinations; /* GPtrArray* of AtmPresentationBlock* */
    GPtrArray *list_stack;   /* ListContext* */
    AtmPresentationBlock *leaf;
    AtmPresentationBlock *table;
    AtmPresentationTableRow *table_row;
    AtmPresentationTableCell *table_cell;
    guint code_span_depth;
    AtmPresentationFallbackReason abort_reason;
} NormalizeState;

static void presentation_inline_free(gpointer data);
static void presentation_block_free(gpointer data);
static void presentation_list_item_free(gpointer data);
static void presentation_table_cell_free(gpointer data);
static void presentation_table_row_free(gpointer data);

static void
presentation_inline_free(gpointer data)
{
    AtmPresentationInline *inline_node = data;
    if (inline_node == NULL)
        return;
    g_free(inline_node->text);
    g_free(inline_node);
}

static void
presentation_list_item_free(gpointer data)
{
    AtmPresentationListItem *item = data;
    if (item == NULL)
        return;
    g_clear_pointer(&item->blocks, g_ptr_array_unref);
    g_free(item);
}

static void
presentation_table_cell_free(gpointer data)
{
    AtmPresentationTableCell *cell = data;
    if (cell == NULL)
        return;
    g_clear_pointer(&cell->inlines, g_ptr_array_unref);
    g_free(cell);
}

static void
presentation_table_row_free(gpointer data)
{
    AtmPresentationTableRow *row = data;
    if (row == NULL)
        return;
    g_clear_pointer(&row->cells, g_ptr_array_unref);
    g_free(row);
}

static void
presentation_block_free(gpointer data)
{
    AtmPresentationBlock *block = data;
    if (block == NULL)
        return;
    g_clear_pointer(&block->inlines, g_ptr_array_unref);
    g_clear_pointer(&block->children, g_ptr_array_unref);
    g_clear_pointer(&block->list_items, g_ptr_array_unref);
    g_clear_pointer(&block->table_rows, g_ptr_array_unref);
    g_free(block);
}

static AtmPresentationDocument *
presentation_document_new(void)
{
    AtmPresentationDocument *document = g_new0(AtmPresentationDocument, 1);
    document->normalizer_schema = g_strdup(ATM_PRESENTATION_NORMALIZER_SCHEMA);
    document->blocks = g_ptr_array_new_with_free_func(presentation_block_free);
    return document;
}

void
atm_presentation_document_free(AtmPresentationDocument *document)
{
    if (document == NULL)
        return;
    g_free(document->normalizer_schema);
    g_clear_pointer(&document->blocks, g_ptr_array_unref);
    g_free(document);
}

static AtmPresentationBlock *
presentation_block_new(AtmPresentationBlockKind kind)
{
    AtmPresentationBlock *block = g_new0(AtmPresentationBlock, 1);
    block->kind = kind;
    block->inlines = g_ptr_array_new_with_free_func(presentation_inline_free);
    block->children = g_ptr_array_new_with_free_func(presentation_block_free);
    block->list_items = g_ptr_array_new_with_free_func(presentation_list_item_free);
    block->table_rows = g_ptr_array_new_with_free_func(presentation_table_row_free);
    return block;
}

static GPtrArray *
current_destination(NormalizeState *state)
{
    g_return_val_if_fail(state->destinations->len > 0, NULL);
    return g_ptr_array_index(state->destinations, state->destinations->len - 1);
}

static gboolean
budget_exceeded(gsize limit, gsize value)
{
    return limit != 0 && value > limit;
}

static int
abort_with(NormalizeState *state, AtmPresentationFallbackReason reason)
{
    if (state->abort_reason == ATM_PRESENTATION_FALLBACK_NONE)
        state->abort_reason = reason;
    return 1;
}

static int
check_nesting_after_push(NormalizeState *state)
{
    guint depth = state->destinations->len - 1;
    if (depth > state->document->counters.max_nesting_depth)
        state->document->counters.max_nesting_depth = depth;
    if (state->budget.max_nesting_depth != 0 &&
        depth > state->budget.max_nesting_depth)
        return abort_with(state, ATM_PRESENTATION_FALLBACK_NESTING_BUDGET);
    return 0;
}

static int
add_block(NormalizeState *state, AtmPresentationBlock *block)
{
    guint prospective = state->document->counters.block_count + 1;
    if (state->budget.max_blocks != 0 &&
        prospective > state->budget.max_blocks) {
        presentation_block_free(block);
        return abort_with(state, ATM_PRESENTATION_FALLBACK_BLOCK_BUDGET);
    }

    GPtrArray *destination = current_destination(state);
    if (destination == NULL) {
        presentation_block_free(block);
        return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
    }

    g_ptr_array_add(destination, block);
    state->document->counters.block_count = prospective;
    return 0;
}

static int
append_inline(
    NormalizeState *state,
    GPtrArray *inlines,
    AtmPresentationInlineKind kind,
    const gchar *text,
    gsize size
)
{
    if (size == 0)
        return 0;

    if (budget_exceeded(
            state->budget.max_output_bytes,
            state->document->counters.output_bytes + size))
        return abort_with(state, ATM_PRESENTATION_FALLBACK_OUTPUT_BUDGET);

    AtmPresentationInline *last = NULL;
    if (inlines->len > 0)
        last = g_ptr_array_index(inlines, inlines->len - 1);

    if (last != NULL && last->kind == kind) {
        gchar *combined = g_malloc(strlen(last->text) + size + 1);
        gsize old_size = strlen(last->text);
        memcpy(combined, last->text, old_size);
        memcpy(combined + old_size, text, size);
        combined[old_size + size] = '\0';
        g_free(last->text);
        last->text = combined;
    } else {
        AtmPresentationInline *node = g_new0(AtmPresentationInline, 1);
        node->kind = kind;
        node->text = g_strndup(text, size);
        g_ptr_array_add(inlines, node);
    }

    state->document->counters.output_bytes += size;
    return 0;
}

static int
append_text(
    NormalizeState *state,
    MD_TEXTTYPE type,
    const MD_CHAR *text,
    MD_SIZE size
)
{
    GPtrArray *target = NULL;
    AtmPresentationInlineKind kind = ATM_PRESENTATION_INLINE_TEXT;

    if (state->table_cell != NULL)
        target = state->table_cell->inlines;
    else if (state->leaf != NULL)
        target = state->leaf->inlines;

    if (target == NULL) {
        if (size == 0)
            return 0;
        return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
    }

    if (type == MD_TEXT_NULLCHAR) {
        static const gchar replacement[] = "\xEF\xBF\xBD";
        return append_inline(
            state,
            target,
            ATM_PRESENTATION_INLINE_TEXT,
            replacement,
            sizeof(replacement) - 1
        );
    }

    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
        static const gchar newline[] = "\n";
        return append_inline(
            state,
            target,
            ATM_PRESENTATION_INLINE_TEXT,
            newline,
            1
        );
    }

    if (type == MD_TEXT_HTML || type == MD_TEXT_LATEXMATH)
        return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);

    if (state->leaf != NULL &&
        state->leaf->kind != ATM_PRESENTATION_BLOCK_CODE_BLOCK &&
        (state->code_span_depth > 0 || type == MD_TEXT_CODE))
        kind = ATM_PRESENTATION_INLINE_CODE;

    return append_inline(state, target, kind, text, size);
}

static ListContext *
current_list_context(NormalizeState *state)
{
    if (state->list_stack->len == 0)
        return NULL;
    return g_ptr_array_index(state->list_stack, state->list_stack->len - 1);
}

static int
enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    NormalizeState *state = userdata;

    switch (type) {
    case MD_BLOCK_DOC:
        return 0;

    case MD_BLOCK_QUOTE: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_QUOTE);
        if (add_block(state, block) != 0)
            return 1;
        g_ptr_array_add(state->destinations, block->children);
        return check_nesting_after_push(state);
    }

    case MD_BLOCK_UL:
    case MD_BLOCK_OL: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_LIST);
        block->level = state->list_stack->len + 1;
        if (add_block(state, block) != 0)
            return 1;

        ListContext *context = g_new0(ListContext, 1);
        context->block = block;
        context->ordered = type == MD_BLOCK_OL;
        context->next_ordinal = context->ordered
            ? ((MD_BLOCK_OL_DETAIL *) detail)->start
            : 0;
        g_ptr_array_add(state->list_stack, context);
        return 0;
    }

    case MD_BLOCK_LI: {
        ListContext *context = current_list_context(state);
        if (context == NULL)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);

        AtmPresentationListItem *item =
            g_new0(AtmPresentationListItem, 1);
        item->depth = state->list_stack->len;
        item->ordered = context->ordered;
        item->ordinal = context->ordered ? context->next_ordinal++ : 0;
        item->blocks =
            g_ptr_array_new_with_free_func(presentation_block_free);
        g_ptr_array_add(context->block->list_items, item);
        g_ptr_array_add(state->destinations, item->blocks);
        return check_nesting_after_push(state);
    }

    case MD_BLOCK_HR: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_SPACER);
        return add_block(state, block);
    }

    case MD_BLOCK_H: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_HEADING);
        block->level = ((MD_BLOCK_H_DETAIL *) detail)->level;
        if (add_block(state, block) != 0)
            return 1;
        state->leaf = block;
        return 0;
    }

    case MD_BLOCK_CODE: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_CODE_BLOCK);
        if (add_block(state, block) != 0)
            return 1;
        state->leaf = block;
        return 0;
    }

    case MD_BLOCK_P: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_PARAGRAPH);
        if (add_block(state, block) != 0)
            return 1;
        state->leaf = block;
        return 0;
    }

    case MD_BLOCK_TABLE: {
        MD_BLOCK_TABLE_DETAIL *table_detail = detail;
        guint rows =
            table_detail->head_row_count + table_detail->body_row_count;
        guint columns = table_detail->col_count;

        if ((state->budget.max_table_rows != 0 &&
             rows > state->budget.max_table_rows) ||
            (state->budget.max_table_columns != 0 &&
             columns > state->budget.max_table_columns))
            return abort_with(state, ATM_PRESENTATION_FALLBACK_TABLE_BUDGET);

        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_TABLE);
        if (add_block(state, block) != 0)
            return 1;
        state->table = block;
        state->document->counters.table_rows += rows;
        if (columns > state->document->counters.table_columns_max)
            state->document->counters.table_columns_max = columns;
        return 0;
    }

    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
        return 0;

    case MD_BLOCK_TR: {
        if (state->table == NULL || state->table_row != NULL)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
        AtmPresentationTableRow *row =
            g_new0(AtmPresentationTableRow, 1);
        row->header = type == MD_BLOCK_THEAD;
        row->cells =
            g_ptr_array_new_with_free_func(presentation_table_cell_free);
        g_ptr_array_add(state->table->table_rows, row);
        state->table_row = row;
        return 0;
    }

    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        if (state->table_row == NULL || state->table_cell != NULL)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
        AtmPresentationTableCell *cell =
            g_new0(AtmPresentationTableCell, 1);
        cell->inlines =
            g_ptr_array_new_with_free_func(presentation_inline_free);
        g_ptr_array_add(state->table_row->cells, cell);
        state->table_cell = cell;
        if (type == MD_BLOCK_TH)
            state->table_row->header = TRUE;
        return 0;
    }

    case MD_BLOCK_HTML:
    case MD_BLOCK_FOOTNOTE_DEF_SECTION:
    case MD_BLOCK_FOOTNOTE_DEF:
    case MD_BLOCK_ADMONITION:
        return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);

    case MD_BLOCK_BLANK: {
        AtmPresentationBlock *block =
            presentation_block_new(ATM_PRESENTATION_BLOCK_SPACER);
        return add_block(state, block);
    }
    }

    return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
}

static int
leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    NormalizeState *state = userdata;
    (void) detail;

    switch (type) {
    case MD_BLOCK_DOC:
        return 0;

    case MD_BLOCK_QUOTE:
        if (state->destinations->len <= 1)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
        g_ptr_array_set_size(
            state->destinations,
            state->destinations->len - 1
        );
        return 0;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (state->list_stack->len == 0)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
        g_ptr_array_remove_index(
            state->list_stack,
            state->list_stack->len - 1
        );
        return 0;

    case MD_BLOCK_LI:
        if (state->destinations->len <= 1)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
        g_ptr_array_set_size(
            state->destinations,
            state->destinations->len - 1
        );
        return 0;

    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_P:
        state->leaf = NULL;
        return 0;

    case MD_BLOCK_TABLE:
        state->table = NULL;
        state->table_row = NULL;
        state->table_cell = NULL;
        return 0;

    case MD_BLOCK_TR:
        state->table_row = NULL;
        return 0;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        state->table_cell = NULL;
        return 0;

    case MD_BLOCK_HR:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
    case MD_BLOCK_BLANK:
        return 0;

    case MD_BLOCK_HTML:
    case MD_BLOCK_FOOTNOTE_DEF_SECTION:
    case MD_BLOCK_FOOTNOTE_DEF:
    case MD_BLOCK_ADMONITION:
        return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
    }

    return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
}

static int
enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    NormalizeState *state = userdata;
    (void) detail;

    if (type == MD_SPAN_CODE)
        state->code_span_depth++;

    /*
     * Strong/emphasis/link/image and other spans deliberately do not create
     * presentation nodes. Their visible text is retained through text().
     */
    return 0;
}

static int
leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    NormalizeState *state = userdata;
    (void) detail;

    if (type == MD_SPAN_CODE) {
        if (state->code_span_depth == 0)
            return abort_with(state, ATM_PRESENTATION_FALLBACK_INVARIANT);
        state->code_span_depth--;
    }
    return 0;
}

static int
text_callback(
    MD_TEXTTYPE type,
    const MD_CHAR *text,
    MD_SIZE size,
    void *userdata
)
{
    return append_text(userdata, type, text, size);
}

static gboolean
inline_array_valid(GPtrArray *inlines)
{
    for (guint i = 0; i < inlines->len; i++) {
        AtmPresentationInline *node = g_ptr_array_index(inlines, i);
        if (node == NULL ||
            (node->kind != ATM_PRESENTATION_INLINE_TEXT &&
             node->kind != ATM_PRESENTATION_INLINE_CODE) ||
            node->text == NULL ||
            !g_utf8_validate(node->text, -1, NULL))
            return FALSE;
    }
    return TRUE;
}

static gboolean
block_array_valid(GPtrArray *blocks);

static gboolean
block_valid(AtmPresentationBlock *block)
{
    if (block == NULL)
        return FALSE;

    switch (block->kind) {
    case ATM_PRESENTATION_BLOCK_PARAGRAPH:
    case ATM_PRESENTATION_BLOCK_CODE_BLOCK:
        return inline_array_valid(block->inlines);

    case ATM_PRESENTATION_BLOCK_HEADING:
        return block->level >= 1 &&
               block->level <= 6 &&
               inline_array_valid(block->inlines);

    case ATM_PRESENTATION_BLOCK_QUOTE:
        return block_array_valid(block->children);

    case ATM_PRESENTATION_BLOCK_LIST:
        for (guint i = 0; i < block->list_items->len; i++) {
            AtmPresentationListItem *item =
                g_ptr_array_index(block->list_items, i);
            if (item == NULL ||
                item->depth == 0 ||
                !block_array_valid(item->blocks))
                return FALSE;
        }
        return TRUE;

    case ATM_PRESENTATION_BLOCK_TABLE:
        for (guint r = 0; r < block->table_rows->len; r++) {
            AtmPresentationTableRow *row =
                g_ptr_array_index(block->table_rows, r);
            if (row == NULL || row->cells == NULL)
                return FALSE;
            for (guint c = 0; c < row->cells->len; c++) {
                AtmPresentationTableCell *cell =
                    g_ptr_array_index(row->cells, c);
                if (cell == NULL || !inline_array_valid(cell->inlines))
                    return FALSE;
            }
        }
        return TRUE;

    case ATM_PRESENTATION_BLOCK_SPACER:
        return TRUE;
    }

    return FALSE;
}

static gboolean
block_array_valid(GPtrArray *blocks)
{
    if (blocks == NULL)
        return FALSE;
    for (guint i = 0; i < blocks->len; i++) {
        if (!block_valid(g_ptr_array_index(blocks, i)))
            return FALSE;
    }
    return TRUE;
}

static AtmPresentationDocument *
fallback_document(
    const gchar *input,
    gsize input_length,
    gboolean input_valid_utf8,
    AtmPresentationFallbackReason reason
)
{
    AtmPresentationDocument *document = presentation_document_new();
    document->fallback_used = TRUE;
    document->fallback_reason = reason;
    document->counters.input_bytes = input_length;

    AtmPresentationBlock *block =
        presentation_block_new(ATM_PRESENTATION_BLOCK_PARAGRAPH);
    gchar *display = input_valid_utf8
        ? g_strndup(input, input_length)
        : g_utf8_make_valid(input, input_length);

    AtmPresentationInline *node = g_new0(AtmPresentationInline, 1);
    node->kind = ATM_PRESENTATION_INLINE_TEXT;
    node->text = display;
    g_ptr_array_add(block->inlines, node);
    g_ptr_array_add(document->blocks, block);

    document->counters.block_count = 1;
    document->counters.output_bytes = strlen(display);
    return document;
}

AtmPresentationDocument *
atm_presentation_document_normalize(
    const gchar *input,
    gssize input_length,
    const AtmPresentationBudget *budget
)
{
    if (input == NULL) {
        input = "";
        input_length = 0;
    } else if (input_length < 0) {
        input_length = (gssize) strlen(input);
    }

    gsize length = (gsize) input_length;
    gboolean valid_utf8 = g_utf8_validate(input, input_length, NULL);

    if (!valid_utf8)
        return fallback_document(
            input,
            length,
            FALSE,
            ATM_PRESENTATION_FALLBACK_INVALID_UTF8
        );

    if (budget != NULL &&
        budget->max_input_bytes != 0 &&
        length > budget->max_input_bytes)
        return fallback_document(
            input,
            length,
            TRUE,
            ATM_PRESENTATION_FALLBACK_INPUT_BUDGET
        );

    if (length > G_MAXUINT)
        return fallback_document(
            input,
            length,
            TRUE,
            ATM_PRESENTATION_FALLBACK_PARSER_LIMIT
        );

    AtmPresentationDocument *document = presentation_document_new();
    document->counters.input_bytes = length;

    NormalizeState state = {0};
    state.document = document;
    if (budget != NULL)
        state.budget = *budget;
    state.destinations = g_ptr_array_new();
    state.list_stack = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(state.destinations, document->blocks);

    const MD_PARSER parser = {
        .abi_version = 0,
        .flags = MD_FLAG_NOINDENTEDCODEBLOCKS | MD_FLAG_NOHTML | MD_FLAG_TABLES,
        .enter_block = enter_block,
        .leave_block = leave_block,
        .enter_span = enter_span,
        .leave_span = leave_span,
        .text = text_callback,
        .debug_log = NULL,
        .syntax = NULL
    };

    int result = md_parse(input, (MD_SIZE) length, &parser, &state);

    AtmPresentationFallbackReason reason = state.abort_reason;
    if (result != 0 && reason == ATM_PRESENTATION_FALLBACK_NONE)
        reason = ATM_PRESENTATION_FALLBACK_PARSER_ERROR;

    if (result == 0 &&
        (state.destinations->len != 1 ||
         state.list_stack->len != 0 ||
         state.leaf != NULL ||
         state.table != NULL ||
         state.table_row != NULL ||
         state.table_cell != NULL ||
         state.code_span_depth != 0 ||
         !block_array_valid(document->blocks))) {
        reason = ATM_PRESENTATION_FALLBACK_INVARIANT;
        result = 1;
    }

    g_ptr_array_unref(state.destinations);
    g_ptr_array_unref(state.list_stack);

    if (result != 0) {
        atm_presentation_document_free(document);
        return fallback_document(input, length, TRUE, reason);
    }

    return document;
}

const gchar *
atm_presentation_fallback_reason_name(
    AtmPresentationFallbackReason reason
)
{
    switch (reason) {
    case ATM_PRESENTATION_FALLBACK_NONE:
        return "none";
    case ATM_PRESENTATION_FALLBACK_INVALID_UTF8:
        return "invalid_utf8";
    case ATM_PRESENTATION_FALLBACK_INPUT_BUDGET:
        return "input_budget";
    case ATM_PRESENTATION_FALLBACK_BLOCK_BUDGET:
        return "block_budget";
    case ATM_PRESENTATION_FALLBACK_NESTING_BUDGET:
        return "nesting_budget";
    case ATM_PRESENTATION_FALLBACK_TABLE_BUDGET:
        return "table_budget";
    case ATM_PRESENTATION_FALLBACK_OUTPUT_BUDGET:
        return "output_budget";
    case ATM_PRESENTATION_FALLBACK_PARSER_LIMIT:
        return "parser_limit";
    case ATM_PRESENTATION_FALLBACK_PARSER_ERROR:
        return "parser_error";
    case ATM_PRESENTATION_FALLBACK_INVARIANT:
        return "invariant";
    }
    return "unknown";
}
