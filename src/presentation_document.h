#ifndef ATM_PRESENTATION_DOCUMENT_H
#define ATM_PRESENTATION_DOCUMENT_H

#include <glib.h>

G_BEGIN_DECLS

#define ATM_PRESENTATION_NORMALIZER_SCHEMA "atm.presentation.v1"

typedef enum {
    ATM_PRESENTATION_BLOCK_PARAGRAPH = 0,
    ATM_PRESENTATION_BLOCK_HEADING,
    ATM_PRESENTATION_BLOCK_QUOTE,
    ATM_PRESENTATION_BLOCK_LIST,
    ATM_PRESENTATION_BLOCK_CODE_BLOCK,
    ATM_PRESENTATION_BLOCK_TABLE,
    ATM_PRESENTATION_BLOCK_SPACER
} AtmPresentationBlockKind;

typedef enum {
    ATM_PRESENTATION_INLINE_TEXT = 0,
    ATM_PRESENTATION_INLINE_CODE
} AtmPresentationInlineKind;

typedef enum {
    ATM_PRESENTATION_FALLBACK_NONE = 0,
    ATM_PRESENTATION_FALLBACK_INVALID_UTF8,
    ATM_PRESENTATION_FALLBACK_INPUT_BUDGET,
    ATM_PRESENTATION_FALLBACK_BLOCK_BUDGET,
    ATM_PRESENTATION_FALLBACK_NESTING_BUDGET,
    ATM_PRESENTATION_FALLBACK_TABLE_BUDGET,
    ATM_PRESENTATION_FALLBACK_OUTPUT_BUDGET,
    ATM_PRESENTATION_FALLBACK_PARSER_LIMIT,
    ATM_PRESENTATION_FALLBACK_PARSER_ERROR,
    ATM_PRESENTATION_FALLBACK_INVARIANT
} AtmPresentationFallbackReason;

/*
 * Zero means "unbounded for this gate". Product defaults are deliberately not
 * frozen by PRES-02; later qualification will calibrate them from measurements.
 */
typedef struct {
    gsize max_input_bytes;
    gsize max_output_bytes;
    guint max_blocks;
    guint max_nesting_depth;
    guint max_table_rows;
    guint max_table_columns;
} AtmPresentationBudget;

typedef struct {
    gsize input_bytes;
    gsize output_bytes;
    guint block_count;
    guint max_nesting_depth;
    guint table_rows;
    guint table_columns_max;
} AtmPresentationCounters;

typedef struct {
    AtmPresentationInlineKind kind;
    gchar *text;
} AtmPresentationInline;

typedef struct _AtmPresentationBlock AtmPresentationBlock;

typedef struct {
    guint depth;
    gboolean ordered;
    guint ordinal;
    GPtrArray *blocks; /* AtmPresentationBlock* */
} AtmPresentationListItem;

typedef struct {
    GPtrArray *inlines; /* AtmPresentationInline* */
} AtmPresentationTableCell;

typedef struct {
    gboolean header;
    GPtrArray *cells; /* AtmPresentationTableCell* */
} AtmPresentationTableRow;

struct _AtmPresentationBlock {
    AtmPresentationBlockKind kind;
    guint level;
    GPtrArray *inlines;    /* AtmPresentationInline* */
    GPtrArray *children;   /* AtmPresentationBlock*; used by QUOTE */
    GPtrArray *list_items; /* AtmPresentationListItem*; used by LIST */
    GPtrArray *table_rows; /* AtmPresentationTableRow*; used by TABLE */
};

typedef struct {
    gchar *normalizer_schema;
    gboolean fallback_used;
    AtmPresentationFallbackReason fallback_reason;
    AtmPresentationCounters counters;
    GPtrArray *blocks; /* AtmPresentationBlock* */
} AtmPresentationDocument;

AtmPresentationDocument *atm_presentation_document_normalize(
    const gchar *input,
    gssize input_length,
    const AtmPresentationBudget *budget
);

void atm_presentation_document_free(AtmPresentationDocument *document);

const gchar *atm_presentation_fallback_reason_name(
    AtmPresentationFallbackReason reason
);

G_END_DECLS

#endif
