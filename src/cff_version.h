#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_CFF_ERROR_PARSE,
    ATM_CFF_ERROR_ROOT,
    ATM_CFF_ERROR_MISSING_VERSION,
    ATM_CFF_ERROR_DUPLICATE_VERSION,
    ATM_CFF_ERROR_INVALID_VERSION,
    ATM_CFF_ERROR_EXTRA_DOCUMENT
} AtmCffError;

#define ATM_CFF_ERROR (atm_cff_error_quark ())

GQuark atm_cff_error_quark (void);

gboolean atm_cff_extract_version (
    const guint8 *data,
    gsize length,
    gchar **out_version,
    GError **error
);

G_END_DECLS
