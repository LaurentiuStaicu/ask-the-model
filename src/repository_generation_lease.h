#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_REPOSITORY_GENERATION_LEASE_ERROR_IO,
    ATM_REPOSITORY_GENERATION_LEASE_ERROR_INVALID_GENERATION,
    ATM_REPOSITORY_GENERATION_LEASE_ERROR_INVALID_OBJECT
} AtmRepositoryGenerationLeaseError;

#define ATM_REPOSITORY_GENERATION_LEASE_ERROR \
    (atm_repository_generation_lease_error_quark ())

GQuark atm_repository_generation_lease_error_quark (void);

gboolean atm_repository_generation_lease_try_acquire_shared (
    const char *state_root,
    gint64 generation_id,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
);

void atm_repository_generation_lease_release (
    gint fd
);

G_END_DECLS
