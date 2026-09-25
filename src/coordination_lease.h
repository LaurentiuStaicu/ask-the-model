#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_COORDINATION_LEASE_ERROR_IO,
    ATM_COORDINATION_LEASE_ERROR_INVALID_OBJECT
} AtmCoordinationLeaseError;

typedef enum {
    ATM_COORDINATION_LEASE_MODE_SHARED,
    ATM_COORDINATION_LEASE_MODE_EXCLUSIVE
} AtmCoordinationLeaseMode;

#define ATM_COORDINATION_LEASE_ERROR \
    (atm_coordination_lease_error_quark ())

GQuark atm_coordination_lease_error_quark (void);

gboolean atm_coordination_lease_acquire_mode (
    const char *path,
    AtmCoordinationLeaseMode mode,
    gboolean nonblocking,
    gint *out_fd,
    gboolean *out_contended,
    gint64 *out_wait_us,
    GError **error
);

gboolean atm_coordination_lease_acquire (
    const char *path,
    gboolean nonblocking,
    gint *out_fd,
    gboolean *out_contended,
    gint64 *out_wait_us,
    GError **error
);

void atm_coordination_lease_release (
    gint fd
);

G_END_DECLS
