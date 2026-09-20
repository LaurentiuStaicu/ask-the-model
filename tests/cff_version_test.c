#include "cff_version.h"

#include <glib.h>

static void
assert_version (const char *document, const char *expected)
{
    gchar *version = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_cff_extract_version (
            (const guint8 *) document,
            strlen (document),
            &version,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (version, ==, expected);

    g_free (version);
}

static void
test_plain_version (void)
{
    assert_version (
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Example\n"
        "version: 0.1.0\n",
        "0.1.0"
    );
}

static void
test_quoted_version (void)
{
    assert_version (
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Example\n"
        "version: \"2026.09\"\n",
        "2026.09"
    );
}

static void
test_nested_version_is_not_repository_version (void)
{
    gchar *version = NULL;
    GError *error = NULL;
    const char *document =
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Example\n"
        "references:\n"
        "  - type: software\n"
        "    title: Dependency\n"
        "    version: 9.9.9\n";

    g_assert_false (
        atm_cff_extract_version (
            (const guint8 *) document,
            strlen (document),
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CFF_ERROR,
        ATM_CFF_ERROR_MISSING_VERSION
    );
    g_assert_null (version);

    g_clear_error (&error);
}

static void
test_duplicate_version_rejected (void)
{
    gchar *version = NULL;
    GError *error = NULL;
    const char *document =
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Example\n"
        "version: 0.1.0\n"
        "version: 0.2.0\n";

    g_assert_false (
        atm_cff_extract_version (
            (const guint8 *) document,
            strlen (document),
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CFF_ERROR,
        ATM_CFF_ERROR_DUPLICATE_VERSION
    );
    g_assert_null (version);

    g_clear_error (&error);
}

static void
test_extra_document_rejected (void)
{
    gchar *version = NULL;
    GError *error = NULL;
    const char *document =
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Example\n"
        "version: 0.1.0\n"
        "---\n"
        "version: 0.2.0\n";

    g_assert_false (
        atm_cff_extract_version (
            (const guint8 *) document,
            strlen (document),
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CFF_ERROR,
        ATM_CFF_ERROR_EXTRA_DOCUMENT
    );
    g_assert_null (version);

    g_clear_error (&error);
}

static void
test_non_mapping_rejected (void)
{
    gchar *version = NULL;
    GError *error = NULL;
    const char *document =
        "- version\n"
        "- 0.1.0\n";

    g_assert_false (
        atm_cff_extract_version (
            (const guint8 *) document,
            strlen (document),
            &version,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CFF_ERROR,
        ATM_CFF_ERROR_ROOT
    );
    g_assert_null (version);

    g_clear_error (&error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/cff/plain-version", test_plain_version);
    g_test_add_func ("/cff/quoted-version", test_quoted_version);
    g_test_add_func (
        "/cff/nested-version-is-not-repository-version",
        test_nested_version_is_not_repository_version
    );
    g_test_add_func (
        "/cff/duplicate-version-rejected",
        test_duplicate_version_rejected
    );
    g_test_add_func (
        "/cff/extra-document-rejected",
        test_extra_document_rejected
    );
    g_test_add_func (
        "/cff/non-mapping-rejected",
        test_non_mapping_rejected
    );

    return g_test_run ();
}
