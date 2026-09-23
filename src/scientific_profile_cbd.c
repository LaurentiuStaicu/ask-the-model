#include "scientific_profiles.h"

static gboolean
add_relation (
    AtmScientificProfile *profile,
    const char *relation_type,
    GError **error
)
{
    return atm_scientific_profile_add_relation_rule (
        profile,
        relation_type,
        "cbd.relation_record",
        ATM_SCIENTIFIC_REQUIRE_NATIVE_ID |
        ATM_SCIENTIFIC_REQUIRE_RELATION_ENDPOINTS |
        ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
        error
    );
}

AtmScientificProfile *
atm_scientific_profile_cbd_v1_new (
    GError **error
)
{
    AtmScientificProfile *profile = atm_scientific_profile_new (
        "atm-profile/cbd/v1",
        "1",
        "cbd",
        error
    );

    if (profile == NULL) {
        return NULL;
    }

    if (!atm_scientific_profile_add_entity_rule (
            profile,
            "variable",
            "cbd.variable_record",
            ATM_SCIENTIFIC_REQUIRE_NATIVE_ID |
            ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
            error
        ) ||
        !add_relation (profile, "CAUSAL", error) ||
        !add_relation (profile, "MODERATING", error) ||
        !add_relation (profile, "INFORMATION_FLOW", error) ||
        !add_relation (profile, "MEASUREMENT", error) ||
        !add_relation (profile, "relation", error) ||
        !atm_scientific_profile_add_dataset_row_rule (
            profile,
            "cbd:dataset:model/benchmarks/results/m1_e4_candidate_recovery_authoritative_2026-09-16.csv",
            "cbd.candidate_recovery_row",
            ATM_SCIENTIFIC_REQUIRE_DATASET_CONTEXT |
            ATM_SCIENTIFIC_REQUIRE_PAYLOAD,
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
