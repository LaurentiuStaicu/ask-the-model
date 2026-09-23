#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_JSON_POINTER_ERROR_SYNTAX,
    ATM_JSON_POINTER_ERROR_LOOKUP
} AtmJsonPointerError;

#define ATM_JSON_POINTER_ERROR \
    (atm_json_pointer_error_quark ())

GQuark atm_json_pointer_error_quark (void);

JsonNode *atm_json_pointer_evaluate (
    JsonNode *root,
    const char *pointer,
    GError **error
);

G_END_DECLS
