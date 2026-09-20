#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_MARKDOWN_MAX_BYTES ((gsize) 16 * 1024 * 1024)

typedef enum {
    ATM_MARKDOWN_ERROR_IO,
    ATM_MARKDOWN_ERROR_TOO_LARGE,
    ATM_MARKDOWN_ERROR_ENCODING
} AtmMarkdownError;

#define ATM_MARKDOWN_ERROR (atm_markdown_error_quark ())

typedef struct {
    guint ordinal;
    guint start_line;
    guint end_line;
    char *heading_path;
    char *title;
    char *body;
} AtmDocumentSection;

GQuark atm_markdown_error_quark (void);

gboolean atm_markdown_extract_sections (
    const char *path,
    GPtrArray **out_sections,
    GError **error
);

void atm_document_section_free (AtmDocumentSection *section);

G_END_DECLS
