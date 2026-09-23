#pragma once

#include <glib.h>

#include "scientific_result.h"

G_BEGIN_DECLS

#define ATM_SCIENTIFIC_REALIZATION_VIEW_SCHEMA "atm-realization-view/1"
#define ATM_SCIENTIFIC_DIRECT_RENDER_SCHEMA "atm-direct-render/1"

typedef enum {
    ATM_SCIENTIFIC_REALIZATION_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_REALIZATION_ERROR_SRA,
    ATM_SCIENTIFIC_REALIZATION_ERROR_VIEW,
    ATM_SCIENTIFIC_REALIZATION_ERROR_RENDER
} AtmScientificRealizationError;

#define ATM_SCIENTIFIC_REALIZATION_ERROR \
    (atm_scientific_realization_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_REALIZATION_BLOCK_ANSWERABILITY,
    ATM_SCIENTIFIC_REALIZATION_BLOCK_ESTABLISHED_FACT,
    ATM_SCIENTIFIC_REALIZATION_BLOCK_DERIVED_FACT,
    ATM_SCIENTIFIC_REALIZATION_BLOCK_CONSTRAINT,
    ATM_SCIENTIFIC_REALIZATION_BLOCK_CONFLICT,
    ATM_SCIENTIFIC_REALIZATION_BLOCK_LIMITATION
} AtmScientificRealizationBlockKind;

typedef enum {
    ATM_SCIENTIFIC_RENDER_LANGUAGE_RO,
    ATM_SCIENTIFIC_RENDER_LANGUAGE_EN
} AtmScientificRenderLanguage;

typedef struct AtmScientificRealizationBlock AtmScientificRealizationBlock;
typedef struct AtmScientificRealizationView AtmScientificRealizationView;
typedef struct AtmScientificDirectRender AtmScientificDirectRender;

GQuark atm_scientific_realization_error_quark (void);

AtmScientificRealizationView *atm_scientific_realization_view_new (
    const AtmSraResult *result,
    GError **error
);

void atm_scientific_realization_view_free (
    AtmScientificRealizationView *view
);

gboolean atm_scientific_realization_view_validate (
    const AtmScientificRealizationView *view,
    GError **error
);

const char *atm_scientific_realization_view_schema (
    const AtmScientificRealizationView *view
);

AtmSraAnswerability atm_scientific_realization_view_answerability (
    const AtmScientificRealizationView *view
);

const char *atm_scientific_realization_view_content_id (
    const AtmScientificRealizationView *view
);

const char *atm_scientific_realization_view_qualified_id (
    const AtmScientificRealizationView *view
);

gsize atm_scientific_realization_view_block_count (
    const AtmScientificRealizationView *view
);

const AtmScientificRealizationBlock *atm_scientific_realization_view_block_at (
    const AtmScientificRealizationView *view,
    gsize index
);

gsize atm_scientific_realization_view_mandatory_count (
    const AtmScientificRealizationView *view
);

const char *atm_scientific_realization_view_mandatory_at (
    const AtmScientificRealizationView *view,
    gsize index
);

AtmScientificRealizationBlockKind atm_scientific_realization_block_kind (
    const AtmScientificRealizationBlock *block
);

const char *atm_scientific_realization_block_id (
    const AtmScientificRealizationBlock *block
);

const char *atm_scientific_realization_block_source_id (
    const AtmScientificRealizationBlock *block
);

const char *atm_scientific_realization_block_value (
    const AtmScientificRealizationBlock *block
);

const char *atm_scientific_realization_block_reason (
    const AtmScientificRealizationBlock *block
);

const char *atm_scientific_realization_block_operation (
    const AtmScientificRealizationBlock *block
);

gsize atm_scientific_realization_block_support_count (
    const AtmScientificRealizationBlock *block
);

const char *atm_scientific_realization_block_support_at (
    const AtmScientificRealizationBlock *block,
    gsize index
);

gboolean atm_scientific_realization_render_direct (
    const AtmScientificRealizationView *view,
    AtmScientificRenderLanguage language,
    AtmScientificDirectRender **out_render,
    GError **error
);

void atm_scientific_direct_render_free (
    AtmScientificDirectRender *render
);

const char *atm_scientific_direct_render_schema (
    const AtmScientificDirectRender *render
);

const char *atm_scientific_direct_render_text (
    const AtmScientificDirectRender *render
);

const char *atm_scientific_direct_render_content_id (
    const AtmScientificDirectRender *render
);

const char *atm_scientific_direct_render_qualified_id (
    const AtmScientificDirectRender *render
);

gsize atm_scientific_direct_render_block_count (
    const AtmScientificDirectRender *render
);

const char *atm_scientific_direct_render_block_at (
    const AtmScientificDirectRender *render,
    gsize index
);

gsize atm_scientific_direct_render_support_count (
    const AtmScientificDirectRender *render
);

const char *atm_scientific_direct_render_support_at (
    const AtmScientificDirectRender *render,
    gsize index
);

G_END_DECLS
