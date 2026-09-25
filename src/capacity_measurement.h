#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_CAPACITY_MEASUREMENT_ERROR_IO,
    ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
    ATM_CAPACITY_MEASUREMENT_ERROR_OVERFLOW
} AtmCapacityMeasurementError;

#define ATM_CAPACITY_MEASUREMENT_ERROR \
    (atm_capacity_measurement_error_quark ())

typedef struct {
    guint64 device_id;
    guint64 available_bytes;
    guint64 available_inodes;
    gboolean inode_budget_known;
    guint64 fragment_size;
} AtmFilesystemCapacityMeasurement;

typedef struct {
    guint64 logical_bytes;
    guint64 allocated_bytes;
} AtmFileCapacityMeasurement;

typedef struct {
    guint64 logical_regular_bytes;
    guint64 allocated_tree_bytes;
    guint64 entries;
    guint64 regular_files;
    guint64 directories;
} AtmTreeCapacityMeasurement;

GQuark atm_capacity_measurement_error_quark (void);

gboolean atm_capacity_measure_filesystem (
    const char *path,
    AtmFilesystemCapacityMeasurement *out_measurement,
    GError **error
);

gboolean atm_capacity_measure_regular_file (
    const char *path,
    AtmFileCapacityMeasurement *out_measurement,
    GError **error
);

gboolean atm_capacity_measure_tree (
    const char *root,
    AtmTreeCapacityMeasurement *out_measurement,
    GError **error
);

G_END_DECLS
