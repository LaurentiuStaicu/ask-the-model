#pragma once

#include <glib.h>

#include "scientific_profile.h"

G_BEGIN_DECLS

AtmScientificProfile *atm_scientific_profile_ewd_v1_new (
    GError **error
);

AtmScientificProfile *atm_scientific_profile_cbd_v1_new (
    GError **error
);

AtmScientificProfile *atm_scientific_profile_rmd_v1_new (
    GError **error
);

G_END_DECLS
