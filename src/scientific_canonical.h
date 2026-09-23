#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_SCIENTIFIC_CANONICAL_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_CANONICAL_ERROR_DECIMAL,
    ATM_SCIENTIFIC_CANONICAL_ERROR_JSON,
    ATM_SCIENTIFIC_CANONICAL_ERROR_NUMBER
} AtmScientificCanonicalError;

#define ATM_SCIENTIFIC_CANONICAL_ERROR \
    (atm_scientific_canonical_error_quark ())

typedef struct {
    char *coefficient;
    gint64 exponent;
} AtmScientificDecimal;

GQuark atm_scientific_canonical_error_quark (void);

gboolean atm_scientific_decimal_parse (
    const char *text,
    AtmScientificDecimal **out_decimal,
    GError **error
);

void atm_scientific_decimal_free (
    AtmScientificDecimal *decimal
);

gboolean atm_scientific_binary64_bits (
    double value,
    guint64 *out_bits,
    GError **error
);

gboolean atm_scientific_content_id_json (
    const char *artifact_class,
    const char *json_payload,
    char **out_id,
    GError **error
);

gboolean atm_scientific_content_id_scalar (
    const char *artifact_class,
    const char *scalar_type,
    const char *canonical_value,
    char **out_id,
    GError **error
);

gboolean atm_scientific_qualified_artifact_id (
    const char *scientific_content_id,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path,
    const char *locator,
    const char *logical_source_id,
    const char *profile_id,
    const char *profile_version,
    char **out_id,
    GError **error
);

G_END_DECLS
