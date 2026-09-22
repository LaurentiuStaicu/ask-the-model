#include "scientific_profiles.h"

static gboolean
add_entity (
    AtmScientificProfile *profile,
    const char *selector,
    const char *semantic_type,
    GError **error
)
{
    return atm_scientific_profile_add_entity_rule (
        profile,
        selector,
        semantic_type,
        ATM_SCIENTIFIC_REQUIRE_NATIVE_ID |
        ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
        error
    );
}

static gboolean
add_dataset (
    AtmScientificProfile *profile,
    const char *dataset_logical_source_id,
    const char *semantic_type,
    GError **error
)
{
    return atm_scientific_profile_add_dataset_row_rule (
        profile,
        dataset_logical_source_id,
        semantic_type,
        ATM_SCIENTIFIC_REQUIRE_DATASET_CONTEXT |
        ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
        error
    );
}

AtmScientificProfile *
atm_scientific_profile_ewd_v1_new (
    GError **error
)
{
    AtmScientificProfile *profile = atm_scientific_profile_new (
        "atm-profile/ewd/v1",
        "1",
        "ewd",
        error
    );

    if (profile == NULL) {
        return NULL;
    }

    if (!add_entity (
            profile,
            "variable",
            "ewd.variable_record",
            error
        ) ||
        !add_entity (
            profile,
            "candidate_interface",
            "ewd.candidate_interface_record",
            error
        ) ||
        !add_entity (
            profile,
            "promotion_gate",
            "ewd.promotion_gate_record",
            error
        ) ||
        !add_dataset (
            profile,
            "ewd:dataset:data/scenarios/fit_diagnostics.csv",
            "ewd.fit_diagnostic_row",
            error
        ) ||
        !add_dataset (
            profile,
            "ewd:dataset:data/scenarios/parameter_identifiability.csv",
            "ewd.parameter_identifiability_row",
            error
        ) ||
        !atm_scientific_profile_freeze (
            profile,
            error
        )) {
        atm_scientific_profile_free (profile);
        return NULL;
    }

    return profile;
}
