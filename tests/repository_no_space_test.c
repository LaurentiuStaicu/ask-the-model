#include "repository_no_space.h"

#include "archive_extract.h"
#include "control_state.h"
#include "retrieval_index.h"

#include <gio/gio.h>
#include <glib.h>

#include <errno.h>

static void
assert_classified (
    GError *error,
    gboolean expected
)
{
    g_assert_cmpint (
        atm_repository_error_is_no_space (
            error
        ),
        ==,
        expected
    );
    g_clear_error (&error);
}

static void
test_gio_no_space (void)
{
    assert_classified (
        g_error_new_literal (
            G_IO_ERROR,
            G_IO_ERROR_NO_SPACE,
            "no space"
        ),
        TRUE
    );
}

static void
test_file_enospc (void)
{
    assert_classified (
        g_error_new_literal (
            G_FILE_ERROR,
            g_file_error_from_errno (ENOSPC),
            "no space"
        ),
        TRUE
    );
}

static void
test_archive_no_space (void)
{
    assert_classified (
        g_error_new_literal (
            g_quark_from_static_string (
                "atm-archive-error-quark"
            ),
            ATM_ARCHIVE_ERROR_NO_SPACE,
            "no space"
        ),
        TRUE
    );
}

static void
test_retrieval_no_space (void)
{
    assert_classified (
        g_error_new_literal (
            g_quark_from_static_string (
                "atm-retrieval-index-error-quark"
            ),
            ATM_RETRIEVAL_INDEX_ERROR_NO_SPACE,
            "no space"
        ),
        TRUE
    );
}

static void
test_control_no_space (void)
{
    assert_classified (
        g_error_new_literal (
            g_quark_from_static_string (
                "atm-control-state-error-quark"
            ),
            ATM_CONTROL_STATE_ERROR_NO_SPACE,
            "no space"
        ),
        TRUE
    );
}

static void
test_unrelated_storage_error (void)
{
    assert_classified (
        g_error_new_literal (
            g_quark_from_static_string (
                "atm-archive-error-quark"
            ),
            ATM_ARCHIVE_ERROR_IO,
            "generic io"
        ),
        FALSE
    );
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (
        &argc,
        &argv,
        NULL
    );

    g_test_add_func (
        "/repository-no-space/gio",
        test_gio_no_space
    );
    g_test_add_func (
        "/repository-no-space/file-enospc",
        test_file_enospc
    );
    g_test_add_func (
        "/repository-no-space/archive",
        test_archive_no_space
    );
    g_test_add_func (
        "/repository-no-space/retrieval",
        test_retrieval_no_space
    );
    g_test_add_func (
        "/repository-no-space/control",
        test_control_no_space
    );
    g_test_add_func (
        "/repository-no-space/unrelated",
        test_unrelated_storage_error
    );

    return g_test_run ();
}
