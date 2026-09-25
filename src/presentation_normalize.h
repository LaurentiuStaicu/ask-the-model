#ifndef ATM_PRESENTATION_NORMALIZE_H
#define ATM_PRESENTATION_NORMALIZE_H

#include "presentation_document.h"

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_PRESENTATION_NORMALIZE_ERROR_ARGUMENT = 1
} AtmPresentationNormalizeError;

#define ATM_PRESENTATION_NORMALIZE_ERROR \
    atm_presentation_normalize_error_quark ()

GQuark atm_presentation_normalize_error_quark (void);

gboolean atm_presentation_normalize (
    const char *input,
    gsize length,
    AtmPresentationDocument **out_document,
    GError **error
);

G_END_DECLS

#endif
