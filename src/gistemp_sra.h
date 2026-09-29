#pragma once
#include "gistemp_evidence.h"
#include "scientific_result.h"
G_BEGIN_DECLS

/* Dormant source-bound SRA. This is not a VerifiedSeries or chart permission. */
typedef struct AtmGistempSra AtmGistempSra;
gboolean atm_gistemp_sra_new (const AtmGistempAdmission *admission,
    AtmGistempSra **out, GError **error);
gboolean atm_gistemp_sra_validate (const AtmGistempSra *sra, GError **error);
void atm_gistemp_sra_free (AtmGistempSra *sra);
/* Borrowed read-only finalized result; do not modify or free any fields. */
const AtmSraResult *atm_gistemp_sra_result (const AtmGistempSra *sra);
G_END_DECLS
