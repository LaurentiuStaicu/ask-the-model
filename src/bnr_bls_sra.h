#pragma once

#include <glib.h>
#include "bnr_bls_panel.h"
#include "scientific_result.h"

G_BEGIN_DECLS

typedef enum {
    ATM_BNR_BLS_SRA_ERROR_ARGUMENT,
    ATM_BNR_BLS_SRA_ERROR_ADMISSION,
    ATM_BNR_BLS_SRA_ERROR_IDENTITY
} AtmBnrBlsSraError;

#define ATM_BNR_BLS_SRA_ERROR (atm_bnr_bls_sra_error_quark ())

typedef struct AtmBnrBlsSra AtmBnrBlsSra;

GQuark atm_bnr_bls_sra_error_quark (void);

/* Rebuilds an independent copy of the pinned panel, then constructs a
 * finalized, source-bound SRA. The panel remains caller-owned. */
gboolean atm_bnr_bls_sra_new (
    const AtmBnrBlsPanel *panel,
    AtmBnrBlsSra **out_sra,
    GError **error
);

gboolean atm_bnr_bls_sra_validate (
    const AtmBnrBlsSra *sra,
    GError **error
);

void atm_bnr_bls_sra_free (AtmBnrBlsSra *sra);

/* Borrowed read-only finalized result. Do not modify or free its fields. */
const AtmSraResult *atm_bnr_bls_sra_result (
    const AtmBnrBlsSra *sra
);

G_END_DECLS
