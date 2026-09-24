#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_CAPACITY_ROOT_DATA = 0,
    ATM_CAPACITY_ROOT_CACHE = 1,
    ATM_CAPACITY_ROOT_STATE = 2,
    ATM_CAPACITY_ROOT_COUNT = 3
} AtmCapacityRootKind;

typedef enum {
    ATM_CAPACITY_ADMISSION_ERROR_ARGUMENT,
    ATM_CAPACITY_ADMISSION_ERROR_INCONSISTENT_FILESYSTEM,
    ATM_CAPACITY_ADMISSION_ERROR_OVERFLOW
} AtmCapacityAdmissionError;

#define ATM_CAPACITY_ADMISSION_ERROR \
    (atm_capacity_admission_error_quark ())

typedef struct {
    guint64 device_id;
    guint64 available_bytes;
    guint64 available_inodes;
    gboolean inode_budget_known;
} AtmCapacityRootAvailability;

typedef struct {
    guint64 bytes[ATM_CAPACITY_ROOT_COUNT];
    guint64 inodes[ATM_CAPACITY_ROOT_COUNT];
} AtmCapacityPhaseRequirement;

typedef struct {
    guint64 bytes[ATM_CAPACITY_ROOT_COUNT];
    guint64 inodes[ATM_CAPACITY_ROOT_COUNT];
} AtmCapacityReservePolicy;

typedef struct {
    guint64 device_id;
    guint root_mask;
    guint64 available_bytes;
    guint64 available_inodes;
    gboolean inode_budget_known;
    guint64 operation_peak_bytes;
    guint64 operation_peak_inodes;
    guint64 reserve_bytes;
    guint64 reserve_inodes;
    guint64 required_bytes;
    guint64 required_inodes;
    gboolean bytes_sufficient;
    gboolean inodes_sufficient;
} AtmCapacityDeviceDecision;

typedef struct {
    guint device_count;
    AtmCapacityDeviceDecision devices[ATM_CAPACITY_ROOT_COUNT];
    gboolean admitted;
} AtmCapacityAdmissionDecision;

GQuark atm_capacity_admission_error_quark (void);

gboolean atm_capacity_admission_evaluate (
    const AtmCapacityRootAvailability roots[ATM_CAPACITY_ROOT_COUNT],
    const AtmCapacityPhaseRequirement *phases,
    gsize phase_count,
    const AtmCapacityReservePolicy *reserve_policy,
    AtmCapacityAdmissionDecision *out_decision,
    GError **error
);

G_END_DECLS
