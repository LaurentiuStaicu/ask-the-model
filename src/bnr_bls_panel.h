#pragma once

#include <glib.h>
#include "scientific_canonical.h"

G_BEGIN_DECLS

#define ATM_BNR_BLS_SOURCE_COUNT 5u
#define ATM_BNR_BLS_UPSTREAM_REPOSITORY "LaurentiuStaicu/romanian-monetary-dynamics"
#define ATM_BNR_BLS_SNAPSHOT_SHA "e43eb4e899f123d3b1aa0f6e20b7c8ed35434170"
#define ATM_BNR_BLS_PROFILE "atm-bnr-bls-canonical-panel-admission/1"
#define ATM_BNR_BLS_MISSING_QUARTER "2025-Q2"
#define ATM_BNR_BLS_MISSING_REASON "source_round_not_retained"

typedef enum {
    ATM_BNR_BLS_SOURCE_PANEL = 0,
    ATM_BNR_BLS_SOURCE_INVENTORY = 1,
    ATM_BNR_BLS_SOURCE_PROMOTION_REVIEW = 2,
    ATM_BNR_BLS_SOURCE_CROSS_ROUND_AUDIT = 3,
    ATM_BNR_BLS_SOURCE_SEMANTIC_AUDIT = 4
} AtmBnrBlsSourceIndex;

typedef enum {
    ATM_BNR_BLS_PANEL_ERROR_ARGUMENT,
    ATM_BNR_BLS_PANEL_ERROR_LIMIT,
    ATM_BNR_BLS_PANEL_ERROR_REPOSITORY,
    ATM_BNR_BLS_PANEL_ERROR_SNAPSHOT,
    ATM_BNR_BLS_PANEL_ERROR_DIGEST,
    ATM_BNR_BLS_PANEL_ERROR_AUTHORITY,
    ATM_BNR_BLS_PANEL_ERROR_SHAPE,
    ATM_BNR_BLS_PANEL_ERROR_ORDER,
    ATM_BNR_BLS_PANEL_ERROR_NUMBER,
    ATM_BNR_BLS_PANEL_ERROR_IDENTITY
} AtmBnrBlsPanelError;

#define ATM_BNR_BLS_PANEL_ERROR (atm_bnr_bls_panel_error_quark ())

typedef struct AtmBnrBlsPanel AtmBnrBlsPanel;

typedef struct {
    const char *quarter;
    guint source_row;
    const char *reference_date; /* NULL only when the source cell is empty. */
    const char *source_id;
    const char *source_kind;
    const char *source_decimal; /* Exact source token, never float-round-tripped. */
    const char *coefficient;    /* Canonical exact-decimal coefficient. */
    gint64 exponent;
} AtmBnrBlsObservation;

GQuark atm_bnr_bls_panel_error_quark (void);

/* Admits only the exact five-file authority bundle at the pinned RMD commit.
 * Inputs are copied, hashed and re-parsed; the caller retains its GBytes. */
gboolean atm_bnr_bls_panel_new (
    const char *repository,
    const char *snapshot_sha,
    GBytes *const sources[ATM_BNR_BLS_SOURCE_COUNT],
    AtmBnrBlsPanel **out_panel,
    GError **error
);

/* Rebuild the admission and candidate from the owned source bytes. */
gboolean atm_bnr_bls_panel_validate (
    const AtmBnrBlsPanel *panel,
    GError **error
);

/* Return an independently owned copy rebuilt from the retained five files. */
gboolean atm_bnr_bls_panel_rebuild (
    const AtmBnrBlsPanel *panel,
    AtmBnrBlsPanel **out_panel,
    GError **error
);

void atm_bnr_bls_panel_free (AtmBnrBlsPanel *panel);

const char *atm_bnr_bls_panel_repository (const AtmBnrBlsPanel *panel);
const char *atm_bnr_bls_panel_snapshot (const AtmBnrBlsPanel *panel);
guint atm_bnr_bls_panel_observation_count (const AtmBnrBlsPanel *panel);
gboolean atm_bnr_bls_panel_observation (
    const AtmBnrBlsPanel *panel,
    guint index,
    AtmBnrBlsObservation *out_observation
);

/* This is a qualified absence assertion, not an inserted CSV row. */
const char *atm_bnr_bls_panel_missing_quarter (const AtmBnrBlsPanel *panel);
const char *atm_bnr_bls_panel_missing_reason (const AtmBnrBlsPanel *panel);

const char *atm_bnr_bls_panel_source_path (
    const AtmBnrBlsPanel *panel,
    guint index
);
const char *atm_bnr_bls_panel_source_sha256 (
    const AtmBnrBlsPanel *panel,
    guint index
);

G_END_DECLS
