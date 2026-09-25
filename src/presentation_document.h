#ifndef ATM_PRESENTATION_DOCUMENT_H
#define ATM_PRESENTATION_DOCUMENT_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_PRESENTATION_BLOCK_PARAGRAPH = 0,
    ATM_PRESENTATION_BLOCK_HEADING,
    ATM_PRESENTATION_BLOCK_LIST_ITEM,
    ATM_PRESENTATION_BLOCK_QUOTE,
    ATM_PRESENTATION_BLOCK_CODE,
    ATM_PRESENTATION_BLOCK_SPACER
} AtmPresentationBlockType;

typedef enum {
    ATM_PRESENTATION_SEGMENT_TEXT = 0,
    ATM_PRESENTATION_SEGMENT_INLINE_CODE
} AtmPresentationSegmentType;

typedef struct {
    AtmPresentationSegmentType type;
    char *text;
} AtmPresentationSegment;

typedef struct {
    AtmPresentationBlockType type;
    GPtrArray *segments;
    guint heading_level;
    guint nesting_level;
    guint quote_depth;
    gboolean ordered;
    guint ordinal;
    char *info;
} AtmPresentationBlock;

typedef struct {
    GPtrArray *blocks;
    gboolean fallback;
    char *fallback_reason;
} AtmPresentationDocument;

AtmPresentationDocument *atm_presentation_document_new (void);
void atm_presentation_document_free (AtmPresentationDocument *document);

AtmPresentationBlock *atm_presentation_document_add_block (
    AtmPresentationDocument *document,
    AtmPresentationBlockType type
);

void atm_presentation_block_append (
    AtmPresentationBlock *block,
    AtmPresentationSegmentType type,
    const char *text,
    gsize length
);

char *atm_presentation_document_to_plain_text (
    const AtmPresentationDocument *document
);

/*
 * Read-only ABI for presentation consumers.
 *
 * Callers must treat AtmPresentationDocument as opaque and use these
 * accessors rather than depending on its internal GPtrArray layout.
 */
gboolean atm_presentation_document_is_fallback (
    const AtmPresentationDocument *document
);

const char *atm_presentation_document_fallback_reason (
    const AtmPresentationDocument *document
);

guint atm_presentation_document_block_count (
    const AtmPresentationDocument *document
);

AtmPresentationBlockType atm_presentation_document_block_type_at (
    const AtmPresentationDocument *document,
    guint block_index
);

guint atm_presentation_document_heading_level_at (
    const AtmPresentationDocument *document,
    guint block_index
);

guint atm_presentation_document_nesting_level_at (
    const AtmPresentationDocument *document,
    guint block_index
);

guint atm_presentation_document_quote_depth_at (
    const AtmPresentationDocument *document,
    guint block_index
);

gboolean atm_presentation_document_ordered_at (
    const AtmPresentationDocument *document,
    guint block_index
);

guint atm_presentation_document_ordinal_at (
    const AtmPresentationDocument *document,
    guint block_index
);

const char *atm_presentation_document_info_at (
    const AtmPresentationDocument *document,
    guint block_index
);

guint atm_presentation_document_segment_count_at (
    const AtmPresentationDocument *document,
    guint block_index
);

AtmPresentationSegmentType atm_presentation_document_segment_type_at (
    const AtmPresentationDocument *document,
    guint block_index,
    guint segment_index
);

const char *atm_presentation_document_segment_text_at (
    const AtmPresentationDocument *document,
    guint block_index,
    guint segment_index
);

G_END_DECLS

#endif
