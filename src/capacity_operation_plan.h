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
    ATM_CAPACITY_PHASE_DOWNLOAD,
    ATM_CAPACITY_PHASE_EXTRACTION,
    ATM_CAPACITY_PHASE_INDEX_BUILD,
    ATM_CAPACITY_PHASE_STATE_COMMIT,
    ATM_CAPACITY_PHASE_COUNT
} AtmCapacityOperationPhaseKind;

typedef enum {
    ATM_CAPACITY_OPERATION_PLAN_ERROR_ARGUMENT,
    ATM_CAPACITY_OPERATION_PLAN_ERROR_OVERFLOW
} AtmCapacityOperationPlanError;

#define ATM_CAPACITY_OPERATION_PLAN_ERROR \
    (atm_capacity_operation_plan_error_quark ())

typedef struct {
    guint64 archive_additional_bytes;
    guint64 archive_additional_inodes;
    guint64 snapshot_additional_bytes;
    guint64 index_additional_bytes;
    guint64 index_additional_inodes;
    guint64 state_additional_bytes;
    guint64 state_additional_inodes;
} AtmCapacityOperationPrediction;

typedef struct {
    AtmCapacityOperationKind operation_kind;
    gboolean must_admit_before_quarantine;
    gsize phase_count;
    AtmCapacityPhaseRequirement phases[ATM_CAPACITY_PHASE_COUNT];
} AtmCapacityOperationPlan;

GQuark atm_capacity_operation_plan_error_quark (void);

gboolean atm_capacity_operation_plan_build (
    AtmCapacityOperationKind operation_kind,
    const AtmArchiveInspection *archive_inspection,
    const AtmCapacityOperationPrediction *prediction,
    AtmCapacityOperationPlan *out_plan,
    GError **error
);

G_END_DECLS
