#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_SCIENTIFIC_CONTROL_MAX_BYTES \
    ((gsize) 4 * 1024 * 1024)

typedef enum {
    ATM_SCIENTIFIC_CONTROL_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_CONTROL_ERROR_REPOSITORY,
    ATM_SCIENTIFIC_CONTROL_ERROR_SNAPSHOT,
    ATM_SCIENTIFIC_CONTROL_ERROR_PATH,
    ATM_SCIENTIFIC_CONTROL_ERROR_IO,
    ATM_SCIENTIFIC_CONTROL_ERROR_TOO_LARGE,
    ATM_SCIENTIFIC_CONTROL_ERROR_PARSE,
    ATM_SCIENTIFIC_CONTROL_ERROR_POINTER,
    ATM_SCIENTIFIC_CONTROL_ERROR_TYPE
} AtmScientificControlError;

#define ATM_SCIENTIFIC_CONTROL_ERROR \
    (atm_scientific_control_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE
} AtmScientificControlId;

typedef enum {
    ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN,
    ATM_SCIENTIFIC_CONTROL_VALUE_STRING,
    ATM_SCIENTIFIC_CONTROL_VALUE_NUMBER
} AtmScientificControlValueType;

typedef struct {
    AtmScientificControlId control_id;
    char *repository_id;
    char *snapshot_sha;
    char *source_path;
    char *json_pointer;

    AtmScientificControlValueType value_type;
    gboolean boolean_value;
    char *string_value;
    double number_value;
} AtmScientificControlValue;

GQuark atm_scientific_control_error_quark (void);

gboolean atm_scientific_control_read (
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    AtmScientificControlId control_id,
    AtmScientificControlValue **out_value,
    GError **error
);

void atm_scientific_control_value_free (
    AtmScientificControlValue *value
);

G_END_DECLS