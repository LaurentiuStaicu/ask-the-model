#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_SCIENTIFIC_OPERATION_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_OPERATION_ERROR_UNKNOWN,
    ATM_SCIENTIFIC_OPERATION_ERROR_INPUT_SHAPE,
    ATM_SCIENTIFIC_OPERATION_ERROR_PRECONDITION,
    ATM_SCIENTIFIC_OPERATION_ERROR_UNIT,
    ATM_SCIENTIFIC_OPERATION_ERROR_NUMERIC
} AtmScientificOperationError;

#define ATM_SCIENTIFIC_OPERATION_ERROR \
    (atm_scientific_operation_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_OPERATION_DERIVATION,
    ATM_SCIENTIFIC_OPERATION_CHECK
} AtmScientificOperationClass;

typedef enum {
    ATM_SCIENTIFIC_OPERATION_OUTCOME_NUMERIC,
    ATM_SCIENTIFIC_OPERATION_OUTCOME_BOOLEAN,
    ATM_SCIENTIFIC_OPERATION_OUTCOME_MISSING
} AtmScientificOperationOutcome;

typedef struct {
    const char *qualified_name;
    const char *operation_id;
    guint version;
    AtmScientificOperationClass operation_class;
    const char *repository_id;
    const char *semantic_profile_id;
    const char *semantic_profile_version;
    const char *numeric_profile;
    const char *const *input_roles;
    gsize input_count;
} AtmScientificOperationDescriptor;

typedef struct {
    const char *role;
    double value;
    const char *unit;
} AtmScientificOperationInput;

typedef struct {
    const AtmScientificOperationDescriptor *descriptor;
    AtmScientificOperationOutcome outcome;
    double numeric_value;
    double secondary_numeric_value;
    gboolean boolean_value;
    char *unit;
    char *reason_code;
} AtmScientificOperationResult;

GQuark atm_scientific_operation_error_quark (void);

const AtmScientificOperationDescriptor *
atm_scientific_operation_lookup (
    const char *qualified_name
);

gsize atm_scientific_operation_count (void);

const AtmScientificOperationDescriptor *
atm_scientific_operation_at (gsize index);

gboolean atm_scientific_operation_execute (
    const char *qualified_name,
    const AtmScientificOperationInput *inputs,
    gsize input_count,
    AtmScientificOperationResult **out_result,
    GError **error
);

void atm_scientific_operation_result_free (
    AtmScientificOperationResult *result
);

G_END_DECLS
