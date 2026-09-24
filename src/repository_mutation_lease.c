#include "repository_mutation_lease.h"

#include "coordination_lease.h"

GQuark
atm_repository_mutation_lease_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-repository-mutation-lease-error-quark"
    );
}

static AtmRepositoryMutationLeaseError
map_coordination_error (
    const GError *error
)
{
    if (error != NULL &&
        error->domain == ATM_COORDINATION_LEASE_ERROR &&
        error->code ==
            ATM_COORDINATION_LEASE_ERROR_INVALID_OBJECT) {
        return ATM_REPOSITORY_MUTATION_LEASE_ERROR_INVALID_OBJECT;
    }

    return ATM_REPOSITORY_MUTATION_LEASE_ERROR_IO;
}

gboolean
atm_repository_mutation_lease_try_acquire (
    const char *state_root,
    gint *out_fd,
    gboolean *out_contended,
    GError **error
)
{
    char *path = NULL;
    GError *local_error = NULL;
    gint64 wait_us = 0;
    gboolean ok;

    g_return_val_if_fail (state_root != NULL, FALSE);
    g_return_val_if_fail (state_root[0] != '\0', FALSE);
    g_return_val_if_fail (out_fd != NULL, FALSE);
    g_return_val_if_fail (out_contended != NULL, FALSE);

    path = g_build_filename (
        state_root,
        "repository-mutation.lock",
        NULL
    );

    ok = atm_coordination_lease_acquire (
        path,
        TRUE,
        out_fd,
        out_contended,
        &wait_us,
        &local_error
    );

    if (!ok) {
        g_set_error (
            error,
            ATM_REPOSITORY_MUTATION_LEASE_ERROR,
            map_coordination_error (local_error),
            "%s",
            local_error != NULL
                ? local_error->message
                : "Repository mutation coordination failed."
        );
    }

    g_clear_error (&local_error);
    g_free (path);
    return ok;
}

void
atm_repository_mutation_lease_release (
    gint fd
)
{
    atm_coordination_lease_release (fd);
}
