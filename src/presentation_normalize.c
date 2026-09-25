#include "presentation_normalize.h"

#include "md4c.h"

#include <string.h>

typedef struct {
    gboolean ordered;
    guint next_ordinal;
} AtmPresentationListContext;

typedef struct {
    AtmPresentationDocument *document;
    AtmPresentationBlock *current;
    GArray *lists;
    GPtrArray *list_items;
    guint quote_depth;
    guint inline_code_depth;
} AtmPresentationParseContext;

GQuark
atm_presentation_normalize_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-presentation-normalize-error"
    );
}

static AtmPresentationBlock *
current_list_item (AtmPresentationParseContext *context)
{
    if (context->list_items->len == 0) {
        return NULL;
    }

    return g_ptr_array_index (
        context->list_items,
        context->list_items->len - 1
    );
}

static AtmPresentationBlock *
new_content_block (
    AtmPresentationParseContext *context,
    AtmPresentationBlockType type
)
{
    AtmPresentationBlock *block =
        atm_presentation_document_add_block (
            context->document,
            type
        );

    block->quote_depth = context->quote_depth;
    return block;
}

static int
enter_block (
    MD_BLOCKTYPE type,
    void *detail,
    void *userdata
)
{
    AtmPresentationParseContext *context = userdata;

    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_QUOTE:
        context->quote_depth++;
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL: {
        AtmPresentationListContext list = {0};

        list.ordered = type == MD_BLOCK_OL;

        if (list.ordered) {
            const MD_BLOCK_OL_DETAIL *ordered = detail;
            list.next_ordinal = ordered->start;
        } else {
            list.next_ordinal = 1;
        }

        g_array_append_val (context->lists, list);
        break;
    }

    case MD_BLOCK_LI: {
        if (context->lists->len == 0) {
            return 1;
        }

        AtmPresentationListContext *list =
            &g_array_index (
                context->lists,
                AtmPresentationListContext,
                context->lists->len - 1
            );

        AtmPresentationBlock *block = new_content_block (
            context,
            ATM_PRESENTATION_BLOCK_LIST_ITEM
        );

        block->nesting_level = context->lists->len;
        block->ordered = list->ordered;
        block->ordinal = list->next_ordinal;

        if (list->ordered) {
            list->next_ordinal++;
        }

        g_ptr_array_add (context->list_items, block);
        context->current = block;
        break;
    }

    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *heading = detail;
        AtmPresentationBlock *block = new_content_block (
            context,
            ATM_PRESENTATION_BLOCK_HEADING
        );

        block->heading_level = heading->level;
        context->current = block;
        break;
    }

    case MD_BLOCK_CODE: {
        const MD_BLOCK_CODE_DETAIL *code = detail;
        AtmPresentationBlock *block = new_content_block (
            context,
            ATM_PRESENTATION_BLOCK_CODE
        );

        if (code != NULL &&
            code->lang.text != NULL &&
            code->lang.size > 0) {
            block->info = g_strndup (
                code->lang.text,
                code->lang.size
            );
        }

        context->current = block;
        break;
    }

    case MD_BLOCK_P: {
        AtmPresentationBlock *item = current_list_item (
            context
        );

        if (item != NULL) {
            context->current = item;
        } else if (context->quote_depth > 0) {
            context->current = new_content_block (
                context,
                ATM_PRESENTATION_BLOCK_QUOTE
            );
        } else {
            context->current = new_content_block (
                context,
                ATM_PRESENTATION_BLOCK_PARAGRAPH
            );
        }
        break;
    }

    case MD_BLOCK_HR:
        atm_presentation_document_add_block (
            context->document,
            ATM_PRESENTATION_BLOCK_SPACER
        );
        break;

    default:
        break;
    }

    return 0;
}

static int
leave_block (
    MD_BLOCKTYPE type,
    void *detail,
    void *userdata
)
{
    AtmPresentationParseContext *context = userdata;

    (void) detail;

    switch (type) {
    case MD_BLOCK_QUOTE:
        if (context->quote_depth > 0) {
            context->quote_depth--;
        }
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (context->lists->len > 0) {
            g_array_remove_index (
                context->lists,
                context->lists->len - 1
            );
        }
        context->current = current_list_item (context);
        break;

    case MD_BLOCK_LI:
        if (context->list_items->len > 0) {
            g_ptr_array_remove_index (
                context->list_items,
                context->list_items->len - 1
            );
        }
        context->current = current_list_item (context);
        break;

    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
        context->current = current_list_item (context);
        break;

    case MD_BLOCK_P:
        context->current = current_list_item (context);
        break;

    default:
        break;
    }

    return 0;
}

static int
enter_span (
    MD_SPANTYPE type,
    void *detail,
    void *userdata
)
{
    AtmPresentationParseContext *context = userdata;

    (void) detail;

    if (type == MD_SPAN_CODE) {
        context->inline_code_depth++;
    }

    return 0;
}

static int
leave_span (
    MD_SPANTYPE type,
    void *detail,
    void *userdata
)
{
    AtmPresentationParseContext *context = userdata;

    (void) detail;

    if (type == MD_SPAN_CODE &&
        context->inline_code_depth > 0) {
        context->inline_code_depth--;
    }

    return 0;
}

static AtmPresentationBlock *
ensure_text_block (AtmPresentationParseContext *context)
{
    if (context->current != NULL) {
        return context->current;
    }

    AtmPresentationBlock *item = current_list_item (context);

    if (item != NULL) {
        context->current = item;
        return item;
    }

    if (context->quote_depth > 0) {
        context->current = new_content_block (
            context,
            ATM_PRESENTATION_BLOCK_QUOTE
        );
    } else {
        context->current = new_content_block (
            context,
            ATM_PRESENTATION_BLOCK_PARAGRAPH
        );
    }

    return context->current;
}

static int
append_text (
    MD_TEXTTYPE type,
    const MD_CHAR *text_value,
    MD_SIZE size,
    void *userdata
)
{
    AtmPresentationParseContext *context = userdata;
    AtmPresentationBlock *block = ensure_text_block (context);

    if (block == NULL) {
        return 1;
    }

    switch (type) {
    case MD_TEXT_NULLCHAR:
        atm_presentation_block_append (
            block,
            ATM_PRESENTATION_SEGMENT_TEXT,
            "\xEF\xBF\xBD",
            3
        );
        break;

    case MD_TEXT_BR:
        atm_presentation_block_append (
            block,
            ATM_PRESENTATION_SEGMENT_TEXT,
            "\n",
            1
        );
        break;

    case MD_TEXT_SOFTBR:
        atm_presentation_block_append (
            block,
            ATM_PRESENTATION_SEGMENT_TEXT,
            " ",
            1
        );
        break;

    case MD_TEXT_CODE:
        atm_presentation_block_append (
            block,
            context->inline_code_depth > 0
                ? ATM_PRESENTATION_SEGMENT_INLINE_CODE
                : ATM_PRESENTATION_SEGMENT_TEXT,
            text_value,
            size
        );
        break;

    case MD_TEXT_NORMAL:
    case MD_TEXT_ENTITY:
    case MD_TEXT_HTML:
    case MD_TEXT_LATEXMATH:
    default:
        atm_presentation_block_append (
            block,
            ATM_PRESENTATION_SEGMENT_TEXT,
            text_value,
            size
        );
        break;
    }

    return 0;
}

static AtmPresentationDocument *
fallback_document (
    const char *safe_text,
    const char *reason
)
{
    AtmPresentationDocument *document =
        atm_presentation_document_new ();

    document->fallback = TRUE;
    document->fallback_reason = g_strdup (reason);

    AtmPresentationBlock *block =
        atm_presentation_document_add_block (
            document,
            ATM_PRESENTATION_BLOCK_PARAGRAPH
        );

    atm_presentation_block_append (
        block,
        ATM_PRESENTATION_SEGMENT_TEXT,
        safe_text,
        strlen (safe_text)
    );

    return document;
}

static char *
make_safe_fallback_text (
    const char *input,
    gsize length
)
{
    GString *without_nul = g_string_sized_new (
        length
    );

    for (gsize i = 0; i < length; i++) {
        if (input[i] == '\0') {
            g_string_append (
                without_nul,
                "\xEF\xBF\xBD"
            );
        } else {
            g_string_append_c (
                without_nul,
                input[i]
            );
        }
    }

    char *safe = g_utf8_make_valid (
        without_nul->str,
        (gssize) without_nul->len
    );
    g_string_free (without_nul, TRUE);
    return safe;
}

gboolean
atm_presentation_normalize (
    const char *input,
    gsize length,
    AtmPresentationDocument **out_document,
    GError **error
)
{
    g_return_val_if_fail (out_document != NULL, FALSE);
    g_return_val_if_fail (*out_document == NULL, FALSE);

    if (input == NULL && length != 0) {
        g_set_error (
            error,
            ATM_PRESENTATION_NORMALIZE_ERROR,
            ATM_PRESENTATION_NORMALIZE_ERROR_ARGUMENT,
            "Presentation input is NULL with non-zero length."
        );
        return FALSE;
    }

    if (length == 0) {
        *out_document = atm_presentation_document_new ();
        return TRUE;
    }

    gboolean valid_utf8 =
        g_utf8_validate (
            input,
            (gssize) length,
            NULL
        ) &&
        memchr (input, '\0', length) == NULL;

    char *safe_text = valid_utf8
        ? g_strndup (input, length)
        : make_safe_fallback_text (
            input,
            length
        );

    if (!valid_utf8) {
        *out_document = fallback_document (
            safe_text,
            "invalid-utf8"
        );
        g_free (safe_text);
        return TRUE;
    }

    if (length > G_MAXUINT) {
        *out_document = fallback_document (
            safe_text,
            "input-too-large-for-md4c"
        );
        g_free (safe_text);
        return TRUE;
    }

    AtmPresentationDocument *document =
        atm_presentation_document_new ();

    AtmPresentationParseContext context = {0};
    context.document = document;
    context.lists = g_array_new (
        FALSE,
        FALSE,
        sizeof (AtmPresentationListContext)
    );
    context.list_items = g_ptr_array_new ();

    MD_PARSER parser = {0};
    parser.abi_version = 0;
    parser.flags =
        MD_FLAG_NOINDENTEDCODEBLOCKS |
        MD_FLAG_NOHTML;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = append_text;

    int result = md_parse (
        safe_text,
        (MD_SIZE) length,
        &parser,
        &context
    );

    g_clear_pointer (&context.lists, g_array_unref);
    g_clear_pointer (
        &context.list_items,
        g_ptr_array_unref
    );

    if (result != 0) {
        atm_presentation_document_free (document);
        document = fallback_document (
            safe_text,
            "md4c-abort"
        );
    }

    g_free (safe_text);
    *out_document = document;

    return TRUE;
}
