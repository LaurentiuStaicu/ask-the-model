#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_STRUCTURED_JSON_MAX_BYTES ((gsize) 16 * 1024 * 1024)
#define ATM_STRUCTURED_JSON_MAX_DEPTH 64
#define ATM_STRUCTURED_JSON_MAX_RECORDS 100000

typedef enum {
    ATM_STRUCTURED_JSON_ERROR_IO,
    ATM_STRUCTURED_JSON_ERROR_TOO_LARGE,
    ATM_STRUCTURED_JSON_ERROR_PARSE,
    ATM_STRUCTURED_JSON_ERROR_LIMIT
} AtmStructuredJsonError;

#define ATM_STRUCTURED_JSON_ERROR (atm_structured_json_error_quark ())

typedef struct {
    char *native_id;
    char *entity_type;
    char *locator;
    char *label;
    char *payload_json;
} AtmStructuredEntity;

typedef struct {
    char *native_id;
    char *relation_type;
    char *locator;
    char *from_native_id;
    char *to_native_id;
    char *payload_json;
} AtmStructuredRelation;

typedef struct {
    GPtrArray *entities;
    GPtrArray *relations;
} AtmStructuredJsonRecords;

GQuark atm_structured_json_error_quark (void);

gboolean atm_structured_json_extract (
    const char *path,
    const char *source_path,
    AtmStructuredJsonRecords **out_records,
    GError **error
);

void atm_structured_entity_free (AtmStructuredEntity *entity);
void atm_structured_relation_free (AtmStructuredRelation *relation);
void atm_structured_json_records_free (AtmStructuredJsonRecords *records);

G_END_DECLS
