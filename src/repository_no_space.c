#include "repository_no_space.h"

#include "archive_extract.h"
#include "control_state.h"
#include "retrieval_index.h"

#include <gio/gio.h>

#include <errno.h>

static gboolean
matches_private_domain (
    const GError *error,
    const char *domain_name,
    gint code
)
{
    return error != NULL &&
        error->domain ==
            g_quark_from_static_string (
                domain_name
            ) &&
        error->code == code;
}

gboolean
atm_repository_error_is_no_space (
    const GError *error
)
{
    if (error == NULL) {
        return FALSE;
    }

    if (g_error_matches (
            error,
            G_IO_ERROR,
            G_IO_ERROR_NO_SPACE
        ) ||
        g_error_matches (
            error,
            G_FILE_ERROR,
            g_file_error_from_errno (ENOSPC)
        )) {
        return TRUE;
    }

    return
        matches_private_domain (
            error,
            "atm-archive-error-quark",
            ATM_ARCHIVE_ERROR_NO_SPACE
        ) ||
        matches_private_domain (
            error,
            "atm-retrieval-index-error-quark",
            ATM_RETRIEVAL_INDEX_ERROR_NO_SPACE
        ) ||
        matches_private_domain (
            error,
            "atm-control-state-error-quark",
            ATM_CONTROL_STATE_ERROR_NO_SPACE
        );
}
