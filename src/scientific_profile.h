#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_SCIENTIFIC_PROFILE_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_PROFILE_ERROR_STATE,
    ATM_SCIENTIFIC_PROFILE_ERROR_DUPLICATE
} AtmScientificProfileError;

#define ATM_SCIENTIFIC_PROFILE_ERROR \
    (atm_scientific_profile_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_RULE_ENTITY,
    ATM_SCIENTIFIC_RULE_RELATION,
    ATM_SCIENTIFIC_RULE_DATASET_ROW
} AtmScientificRuleKind;

typedef enum {
    ATM_SCIENTIFIC_REQUIRE_NONE = 0,
    ATM_SCIENTIFIC_REQUIRE_NATIVE_ID = 1u << 0,
    ATM_SCIENTIFIC_REQUIRE_RELATION_ENDPOINTS = 1u << 1,
    ATM_SCIENTIFIC_REQUIRE_DATASET_CONTEXT = 1u << 2,
    ATM_SCIENTIFIC_REQUIRE_PAYLOAD = 1u << 3
} AtmScientificRuleRequirements;

typedef struct AtmScientificProfile AtmScientificProfile;

GQuark atm_scientific_profile_error_quark (void);

AtmScientificProfile *atm_scientific_profile_new (
    const char *profile_id,
    const char *profile_version,
    const char *repository_id,
    GError **error
);

void atm_scientific_profile_free (
    AtmScientificProfile *profile
);

gboolean atm_scientific_profile_add_entity_rule (
    AtmScientificProfile *profile,
    const char *entity_type,
    const char *semantic_type,
    guint requirements,
    GError **error
);

gboolean atm_scientific_profile_add_relation_rule (
    AtmScientificProfile *profile,
    const char *relation_type,
    const char *semantic_type,
    guint requirements,
    GError **error
);

gboolean atm_scientific_profile_add_dataset_row_rule (
    AtmScientificProfile *profile,
    const char *dataset_logical_source_id,
    const char *semantic_type,
    guint requirements,
    GError **error
);

gboolean atm_scientific_profile_freeze (
    AtmScientificProfile *profile,
    GError **error
);

gboolean atm_scientific_profile_is_frozen (
    const AtmScientificProfile *profile
);

const char *atm_scientific_profile_id (
    const AtmScientificProfile *profile
);

const char *atm_scientific_profile_version (
    const AtmScientificProfile *profile
);

const char *atm_scientific_profile_repository_id (
    const AtmScientificProfile *profile
);

gboolean atm_scientific_profile_lookup (
    const AtmScientificProfile *profile,
    AtmScientificRuleKind kind,
    const char *selector,
    const char **out_semantic_type,
    guint *out_requirements
);

G_END_DECLS
