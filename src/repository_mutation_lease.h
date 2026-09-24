#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO,
    ATM_REPOSITORY_MUTATION_LEASE_ERROR_INVALID_OBJECT
} AtmRepositoryMutationLeaseError;

#define ATM_REPOSITORY_MUTATION_LEASE_ERROR \
    (atm_repository_mutation_lease_error_quark ())

GQuark atm_repository_mutation_lease_error_quark (void);

gboolean atm_repository_mutation_lease_try_acquire (
    const char *state_root,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
);

void atm_repository_mutation_lease_release (
    gint fd
);

G_END_DECLS
