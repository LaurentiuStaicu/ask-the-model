#include "presentation_document.h"

static void
atm_presentation_segment_free (AtmPresentationSegment *segment)
{
    if (segment == NULL) {
        return;
    }

    g_clear_pointer (&segment->text, g_free);
    g_free (segment);
}

static void
atm_presentation_block_free (AtmPresentationBlock *block)
{
    if (block == NULL) {
        return;
    }

    g_clear_pointer (&block->segments, g_ptr_array_unref);
    g_clear_pointer (&block->info, g_free);
    g_free (block);
}

AtmPresentationDocument *
atm_presentation_document_new (void)
{
    AtmPresentationDocument *document = g_new0 (
        AtmPresentationDocument,
        1
    );

    document->blocks = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_presentation_block_free
    );

    return document;
}

void
atm_presentation_document_free (AtmPresentationDocument *document)
{
    if (document == NULL) {
        return;
    }

    g_clear_pointer (&document->blocks, g_ptr_array_unref);
    g_clear_pointer (&document->fallback_reason, g_free);
    g_free (document);
}

AtmPresentationBlock *
atm_presentation_document_add_block (
    AtmPresentationDocument *document,
    AtmPresentationBlockType type
)
{
    g_return_val_if_fail (document != NULL, NULL);
    g_return_val_if_fail (document->blocks != NULL, NULL);

    AtmPresentationBlock *block = g_new0 (
        AtmPresentationBlock,
        1
    );

    block->type = type;
    block->segments = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_presentation_segment_free
    );

    g_ptr_array_add (document->blocks, block);

    return block;
}

void
atm_presentation_block_append (
    AtmPresentationBlock *block,
    AtmPresentationSegmentType type,
    const char *text_value,
    gsize length
)
{
    g_return_if_fail (block != NULL);
    g_return_if_fail (block->segments != NULL);

    if (text_value == NULL || length == 0) {
        return;
    }

    if (block->segments->len > 0) {
        AtmPresentationSegment *last = g_ptr_array_index (
            block->segments,
            block->segments->len - 1
        );

        if (last->type == type) {
            GString *joined = g_string_new (last->text);
            g_string_append_len (
                joined,
                text_value,
                (gssize) length
            );
            g_free (last->text);
            last->text = g_string_free (joined, FALSE);
            return;
        }
    }

    AtmPresentationSegment *segment = g_new0 (
        AtmPresentationSegment,
        1
    );

    segment->type = type;
    segment->text = g_strndup (text_value, length);

    g_ptr_array_add (block->segments, segment);
}

static void
append_block_text (
    GString *out,
    const AtmPresentationBlock *block
)
{
    for (guint i = 0; i < block->segments->len; i++) {
        AtmPresentationSegment *segment = g_ptr_array_index (
            block->segments,
            i
        );

        g_string_append (out, segment->text);
    }
}

char *
atm_presentation_document_to_plain_text (
    const AtmPresentationDocument *document
)
{
    g_return_val_if_fail (document != NULL, NULL);
    g_return_val_if_fail (document->blocks != NULL, NULL);

    GString *out = g_string_new (NULL);
    AtmPresentationBlockType previous_type =
        ATM_PRESENTATION_BLOCK_SPACER;
    gboolean have_previous = FALSE;

    for (guint i = 0; i < document->blocks->len; i++) {
        AtmPresentationBlock *block = g_ptr_array_index (
            document->blocks,
            i
        );

        if (block->type == ATM_PRESENTATION_BLOCK_SPACER) {
            if (out->len > 0 && out->str[out->len - 1] != '\n') {
                g_string_append_c (out, '\n');
            }
            if (out->len > 0) {
                g_string_append_c (out, '\n');
            }
            previous_type = block->type;
            have_previous = TRUE;
            continue;
        }

        if (have_previous && out->len > 0) {
            gboolean adjacent_list_items =
                previous_type == ATM_PRESENTATION_BLOCK_LIST_ITEM &&
                block->type == ATM_PRESENTATION_BLOCK_LIST_ITEM;

            if (out->str[out->len - 1] != '\n') {
                g_string_append_c (out, '\n');
            }

            if (!adjacent_list_items &&
                previous_type != ATM_PRESENTATION_BLOCK_SPACER) {
                g_string_append_c (out, '\n');
            }
        }

        if (block->type == ATM_PRESENTATION_BLOCK_LIST_ITEM) {
            guint level = block->nesting_level > 0
                ? block->nesting_level - 1
                : 0;

            for (guint depth = 0; depth < level; depth++) {
                g_string_append (out, "  ");
            }

            if (block->ordered) {
                g_string_append_printf (
                    out,
                    "%u. ",
                    block->ordinal
                );
            } else {
                g_string_append (out, "• ");
            }
        }

        append_block_text (out, block);
        previous_type = block->type;
        have_previous = TRUE;
    }

    return g_string_free (out, FALSE);
}
