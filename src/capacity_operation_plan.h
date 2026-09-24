#pragma once

#include <glib.h>

#include "archive_extract.h"
#include "capacity_admission_model.h"

G_BEGIN_DECLS

typedef enum {
    ATM_CAPACITY_OPERATION_FRESH_INSTALL,
    ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
    ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR
} AtmCapacityOperationKind;

typedef enum {
    ATM_CAPACITY_MUTATION_PHASE_EXTRACTION,
    ATM_CAPACITY_MUTATION_PHASE_INDEX_BUILD,
    ATM_CAPACITY_MUTATION_PHASE_STATE_COMMIT,
    ATM_CAPACITY_MUTATION_PHASE_COUNT
} AtmCapacityMutationPhaseKind;

typedef enum {
    ATM_CAPACITY_OPERATION_PLAN_ERROR_ARGUMENT
} AtmCapacityOperationPlanError;

#define ATM_CAPACITY_OPERATION_PLAN_ERROR \
    (atm_capacity_operation_plan_error_quark ())

/*
 * Evaluated before archive .part creation.
 *
 * The caller supplies the conservative archive allocation/inode prediction
 * for the cache filesystem. No archive inspection exists at this checkpoint.
 */
typedef struct {
    guint64 archive_additional_bytes;
    guint64 archive_additional_inodes;
} AtmCapacityDownloadPrediction;

/*
 * Evaluated after the completed archive exists and has been inspected, but
 * before extraction/quarantine begins.
 *
 * The archive is already consuming the newly measured filesystem
 * availability at this checkpoint, so it MUST NOT be counted again here.
 */
typedef struct {
    guint64 snapshot_additional_bytes;
    guint64 index_additional_bytes;
    guint64 index_additional_inodes;
    guint64 state_additional_bytes;
    guint64 state_additional_inodes;
} AtmCapacityMutationPrediction;

typedef struct {
    AtmCapacityOperationKind operation_kind;
    gboolean must_admit_before_quarantine;
    gsize phase_count;
    AtmCapacityPhaseRequirement phases[
        ATM_CAPACITY_MUTATION_PHASE_COUNT
    ];
} AtmCapacityMutationPlan;

GQuark atm_capacity_operation_plan_error_quark (void);

gboolean atm_capacity_download_phase_build (
    const AtmCapacityDownloadPrediction *prediction,
    AtmCapacityPhaseRequirement *out_phase,
    GError **error
);

gboolean atm_capacity_mutation_plan_build (
    AtmCapacityOperationKind operation_kind,
    const AtmArchiveInspection *archive_inspection,
    const AtmCapacityMutationPrediction *prediction,
    AtmCapacityMutationPlan *out_plan,
    GError **error
);

G_END_DECLS
