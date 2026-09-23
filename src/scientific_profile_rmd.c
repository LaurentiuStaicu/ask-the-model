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

AtmScientificProfile *
atm_scientific_profile_rmd_v1_new (
    GError **error
)
{
    AtmScientificProfile *profile = atm_scientific_profile_new (
        "atm-profile/rmd/v1",
        "1",
        "rmd",
        error
    );

    if (profile == NULL) {
        return NULL;
    }

    if (!add_entity (
            profile,
            "stock",
            "rmd.stock_record",
            error
        ) ||
        !add_entity (
            profile,
            "flow",
            "rmd.flow_record",
            error
        ) ||
        !add_entity (
            profile,
            "auxiliary",
            "rmd.auxiliary_record",
            error
        ) ||
        !add_entity (
            profile,
            "equation",
            "rmd.equation_record",
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
