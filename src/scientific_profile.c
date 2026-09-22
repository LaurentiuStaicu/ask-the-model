#include "scientific_profile.h"

typedef struct {
    AtmScientificRuleKind kind;
    char *selector;
    char *semantic_type;
    guint requirements;
} AtmScientificProfileRule;

struct AtmScientificProfile {
    char *profile_id;
    char *profile_version;
    char *repository_id;
    GPtrArray *rules;
    gboolean frozen;
};

GQuark
atm_scientific_profile_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-profile-error-quark"
    );
}

static gboolean
string_is_present (const char *value)
{
    return value != NULL && value[0] != '\0';
}

static void
rule_free (AtmScientificProfileRule *rule)
{
    if (rule == NULL) {
        return;
    }

    g_free (rule->selector);
    g_free (rule->semantic_type);
    g_free (rule);
}

AtmScientificProfile *
atm_scientific_profile_new (
    const char *profile_id,
    const char *profile_version,
    const char *repository_id,
    GError **error
)
{
    if (!string_is_present (profile_id) ||
        !string_is_present (profile_version) ||
        !string_is_present (repository_id)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_PROFILE_ERROR,
            ATM_SCIENTIFIC_PROFILE_ERROR_ARGUMENT,
            "Scientific profile identity and repository must be non-empty."
        );
        return NULL;
    }

    AtmScientificProfile *profile = g_new0 (
        AtmScientificProfile,
        1
    );

    profile->profile_id = g_strdup (profile_id);
    profile->profile_version = g_strdup (profile_version);
    profile->repository_id = g_strdup (repository_id);
    profile->rules = g_ptr_array_new_with_free_func (
        (GDestroyNotify) rule_free
    );

    return profile;
}

void
atm_scientific_profile_free (
    AtmScientificProfile *profile
)
{
    if (profile == NULL) {
        return;
    }

    g_free (profile->profile_id);
    g_free (profile->profile_version);
    g_free (profile->repository_id);
    g_clear_pointer (&profile->rules, g_ptr_array_unref);
    g_free (profile);
}

static gboolean
add_rule (
    AtmScientificProfile *profile,
    AtmScientificRuleKind kind,
    const char *selector,
    const char *semantic_type,
    guint requirements,
    GError **error
)
{
    g_return_val_if_fail (profile != NULL, FALSE);

    if (profile->frozen) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_PROFILE_ERROR,
            ATM_SCIENTIFIC_PROFILE_ERROR_STATE,
            "Scientific profile is frozen and cannot be modified."
        );
        return FALSE;
    }

    if (!string_is_present (selector) ||
        !string_is_present (semantic_type)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_PROFILE_ERROR,
            ATM_SCIENTIFIC_PROFILE_ERROR_ARGUMENT,
            "Scientific profile rule selector and semantic type must be non-empty."
        );
        return FALSE;
    }

    for (guint i = 0; i < profile->rules->len; i++) {
        AtmScientificProfileRule *existing = g_ptr_array_index (
            profile->rules,
            i
        );

        if (existing->kind == kind &&
            g_strcmp0 (existing->selector, selector) == 0) {
            g_set_error (
                error,
                ATM_SCIENTIFIC_PROFILE_ERROR,
                ATM_SCIENTIFIC_PROFILE_ERROR_DUPLICATE,
                "Scientific profile already contains a rule for selector '%s'.",
                selector
            );
            return FALSE;
        }
    }

    AtmScientificProfileRule *rule = g_new0 (
        AtmScientificProfileRule,
        1
    );

    rule->kind = kind;
    rule->selector = g_strdup (selector);
    rule->semantic_type = g_strdup (semantic_type);
    rule->requirements = requirements;

    g_ptr_array_add (profile->rules, rule);
    return TRUE;
}

gboolean
atm_scientific_profile_add_entity_rule (
    AtmScientificProfile *profile,
    const char *entity_type,
    const char *semantic_type,
    guint requirements,
    GError **error
)
{
    return add_rule (
        profile,
        ATM_SCIENTIFIC_RULE_ENTITY,
        entity_type,
        semantic_type,
        requirements,
        error
    );
}

gboolean
atm_scientific_profile_add_relation_rule (
    AtmScientificProfile *profile,
    const char *relation_type,
    const char *semantic_type,
    guint requirements,
    GError **error
)
{
    return add_rule (
        profile,
        ATM_SCIENTIFIC_RULE_RELATION,
        relation_type,
        semantic_type,
        requirements,
        error
    );
}

gboolean
atm_scientific_profile_add_dataset_row_rule (
    AtmScientificProfile *profile,
    const char *dataset_logical_source_id,
    const char *semantic_type,
    guint requirements,
    GError **error
)
{
    return add_rule (
        profile,
        ATM_SCIENTIFIC_RULE_DATASET_ROW,
        dataset_logical_source_id,
        semantic_type,
        requirements,
        error
    );
}

gboolean
atm_scientific_profile_freeze (
    AtmScientificProfile *profile,
    GError **error
)
{
    g_return_val_if_fail (profile != NULL, FALSE);

    if (profile->frozen) {
        return TRUE;
    }

    if (profile->rules->len == 0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_PROFILE_ERROR,
            ATM_SCIENTIFIC_PROFILE_ERROR_STATE,
            "Scientific profile cannot be frozen without rules."
        );
        return FALSE;
    }

    profile->frozen = TRUE;
    return TRUE;
}

gboolean
atm_scientific_profile_is_frozen (
    const AtmScientificProfile *profile
)
{
    return profile != NULL && profile->frozen;
}

const char *
atm_scientific_profile_id (
    const AtmScientificProfile *profile
)
{
    return profile != NULL ? profile->profile_id : NULL;
}

const char *
atm_scientific_profile_version (
    const AtmScientificProfile *profile
)
{
    return profile != NULL ? profile->profile_version : NULL;
}

const char *
atm_scientific_profile_repository_id (
    const AtmScientificProfile *profile
)
{
    return profile != NULL ? profile->repository_id : NULL;
}

gboolean
atm_scientific_profile_lookup (
    const AtmScientificProfile *profile,
    AtmScientificRuleKind kind,
    const char *selector,
    const char **out_semantic_type,
    guint *out_requirements
)
{
    g_return_val_if_fail (profile != NULL, FALSE);
    g_return_val_if_fail (out_semantic_type != NULL, FALSE);
    g_return_val_if_fail (out_requirements != NULL, FALSE);

    *out_semantic_type = NULL;
    *out_requirements = ATM_SCIENTIFIC_REQUIRE_NONE;

    if (!profile->frozen || !string_is_present (selector)) {
        return FALSE;
    }

    for (guint i = 0; i < profile->rules->len; i++) {
        AtmScientificProfileRule *rule = g_ptr_array_index (
            profile->rules,
            i
        );

        if (rule->kind == kind &&
            g_strcmp0 (rule->selector, selector) == 0) {
            *out_semantic_type = rule->semantic_type;
            *out_requirements = rule->requirements;
            return TRUE;
        }
    }

    return FALSE;
}
