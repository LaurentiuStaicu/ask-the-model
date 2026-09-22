#include "startup_qualification.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static const char *APP_ID =
    "io.github.laurentiustaicu.ask_the_model";
static const char *RUNTIME_ID =
    "io.elementary.Platform";
static const char *RUNTIME_BRANCH = "8";

static char *
hex64 (char value)
{
    return g_strnfill (64, value);
}

static char *
fixture_text (
    const char *app_commit,
    const char *runtime_commit,
    const char *app_extensions,
    const char *runtime_extensions,
    const char *flatpak_version,
    const char *application_id,
    const char *runtime_ref
)
{
    return g_strdup_printf (
        "[Application]\n"
        "name=%s\n"
        "runtime=%s\n"
        "\n"
        "[Instance]\n"
        "app-commit=%s\n"
        "app-extensions=%s\n"
        "branch=master\n"
        "arch=x86_64\n"
        "flatpak-version=%s\n"
        "runtime-commit=%s\n"
        "runtime-extensions=%s\n",
        application_id,
        runtime_ref,
        app_commit,
        app_extensions,
        flatpak_version,
        runtime_commit,
        runtime_extensions
    );
}

static char *
write_fixture (
    const char *directory,
    const char *name,
    const char *contents
)
{
    char *path = g_build_filename (
        directory,
        name,
        NULL
    );
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);
    return path;
}

static void
test_development_when_metadata_absent (void)
{
    GError *error = NULL;
    AtmDeploymentQualification *qualification = NULL;

    g_assert_true (
        atm_startup_qualify_deployment (
            "/definitely/not/flatpak-info",
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            1,
            &qualification,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (qualification);
    g_assert_cmpint (
        qualification->execution_mode,
        ==,
        ATM_EXECUTION_MODE_DEVELOPMENT
    );
    g_assert_false (qualification->platform_qualified);
    g_assert_null (qualification->platform_fingerprint);

    atm_deployment_qualification_free (qualification);
}

static void
test_valid_deployment_and_stable_fingerprint (void)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp (
        "atm-startup-identity-XXXXXX",
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (directory);

    char *app_commit = hex64 ('a');
    char *runtime_commit = hex64 ('b');
    char *extension_one = hex64 ('1');
    char *extension_two = hex64 ('2');
    char *runtime_extension = hex64 ('3');

    char *app_extensions_a = g_strdup_printf (
        "org.example.Zed=%s;org.example.Alpha=%s;",
        extension_two,
        extension_one
    );
    char *app_extensions_b = g_strdup_printf (
        "org.example.Alpha=%s;org.example.Zed=%s;",
        extension_one,
        extension_two
    );
    char *runtime_extensions = g_strdup_printf (
        "org.example.Runtime=%s;",
        runtime_extension
    );

    char *fixture_a = fixture_text (
        app_commit,
        runtime_commit,
        app_extensions_a,
        runtime_extensions,
        "1.16.0",
        APP_ID,
        "io.elementary.Platform/x86_64/8"
    );
    char *fixture_b = fixture_text (
        app_commit,
        runtime_commit,
        app_extensions_b,
        runtime_extensions,
        "1.18.1",
        APP_ID,
        "io.elementary.Platform/x86_64/8"
    );

    char *path_a = write_fixture (
        directory,
        "flatpak-a.info",
        fixture_a
    );
    char *path_b = write_fixture (
        directory,
        "flatpak-b.info",
        fixture_b
    );

    AtmDeploymentQualification *a = NULL;
    AtmDeploymentQualification *b = NULL;
    AtmDeploymentQualification *policy_changed = NULL;

    g_assert_true (
        atm_startup_qualify_deployment (
            path_a,
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            1,
            &a,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (a->platform_qualified);
    g_assert_cmpint (
        a->execution_mode,
        ==,
        ATM_EXECUTION_MODE_FLATPAK
    );
    g_assert_cmpstr (a->application_id, ==, APP_ID);
    g_assert_cmpstr (
        a->application_ref,
        ==,
        "app/io.github.laurentiustaicu.ask_the_model/x86_64/master"
    );
    g_assert_cmpstr (
        a->runtime_ref,
        ==,
        "io.elementary.Platform/x86_64/8"
    );
    g_assert_cmpstr (a->flatpak_version, ==, "1.16.0");
    g_assert_nonnull (a->platform_fingerprint);
    g_assert_cmpuint (
        strlen (a->platform_fingerprint),
        ==,
        64
    );
    g_assert_cmpstr (
        a->application_extensions[0],
        <,
        a->application_extensions[1]
    );

    g_assert_true (
        atm_startup_qualify_deployment (
            path_b,
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            1,
            &b,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        a->platform_fingerprint,
        ==,
        b->platform_fingerprint
    );

    g_assert_true (
        atm_startup_qualify_deployment (
            path_a,
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            2,
            &policy_changed,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        a->platform_fingerprint,
        !=,
        policy_changed->platform_fingerprint
    );

    atm_deployment_qualification_free (a);
    atm_deployment_qualification_free (b);
    atm_deployment_qualification_free (policy_changed);

    g_remove (path_a);
    g_remove (path_b);
    g_rmdir (directory);

    g_free (path_a);
    g_free (path_b);
    g_free (fixture_a);
    g_free (fixture_b);
    g_free (app_extensions_a);
    g_free (app_extensions_b);
    g_free (runtime_extensions);
    g_free (app_commit);
    g_free (runtime_commit);
    g_free (extension_one);
    g_free (extension_two);
    g_free (runtime_extension);
    g_free (directory);
}

static void
assert_invalid_fixture (
    const char *contents
)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp (
        "atm-startup-invalid-XXXXXX",
        &error
    );
    g_assert_no_error (error);

    char *path = write_fixture (
        directory,
        "flatpak.info",
        contents
    );
    AtmDeploymentQualification *qualification = NULL;

    g_assert_false (
        atm_startup_qualify_deployment (
            path,
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            1,
            &qualification,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STARTUP_QUALIFICATION_ERROR,
        ATM_STARTUP_QUALIFICATION_ERROR_METADATA
    );
    g_assert_null (qualification);

    g_clear_error (&error);
    g_remove (path);
    g_rmdir (directory);
    g_free (path);
    g_free (directory);
}

static void
test_invalid_deployment_metadata (void)
{
    char *app_commit = hex64 ('a');
    char *runtime_commit = hex64 ('b');
    char *extension = hex64 ('1');
    char *extensions = g_strdup_printf (
        "org.example.Extension=%s;",
        extension
    );

    char *wrong_app = fixture_text (
        app_commit,
        runtime_commit,
        extensions,
        "",
        "1.16.0",
        "org.example.Wrong",
        "io.elementary.Platform/x86_64/8"
    );
    assert_invalid_fixture (wrong_app);

    char *wrong_runtime = fixture_text (
        app_commit,
        runtime_commit,
        extensions,
        "",
        "1.16.0",
        APP_ID,
        "io.elementary.Platform/x86_64/7"
    );
    assert_invalid_fixture (wrong_runtime);

    char *bad_commit = fixture_text (
        "not-a-commit",
        runtime_commit,
        extensions,
        "",
        "1.16.0",
        APP_ID,
        "io.elementary.Platform/x86_64/8"
    );
    assert_invalid_fixture (bad_commit);

    char *bad_extensions = fixture_text (
        app_commit,
        runtime_commit,
        "org.example.Extension=bad;",
        "",
        "1.16.0",
        APP_ID,
        "io.elementary.Platform/x86_64/8"
    );
    assert_invalid_fixture (bad_extensions);

    g_free (wrong_app);
    g_free (wrong_runtime);
    g_free (bad_commit);
    g_free (bad_extensions);
    g_free (extensions);
    g_free (app_commit);
    g_free (runtime_commit);
    g_free (extension);
}

static void
test_app_commit_changes_fingerprint (void)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp (
        "atm-startup-commit-XXXXXX",
        &error
    );
    g_assert_no_error (error);

    char *app_a = hex64 ('a');
    char *app_c = hex64 ('c');
    char *runtime = hex64 ('b');

    char *fixture_a = fixture_text (
        app_a,
        runtime,
        "",
        "",
        "1.16.0",
        APP_ID,
        "io.elementary.Platform/x86_64/8"
    );
    char *fixture_c = fixture_text (
        app_c,
        runtime,
        "",
        "",
        "1.16.0",
        APP_ID,
        "io.elementary.Platform/x86_64/8"
    );

    char *path_a = write_fixture (
        directory,
        "a.info",
        fixture_a
    );
    char *path_c = write_fixture (
        directory,
        "c.info",
        fixture_c
    );

    AtmDeploymentQualification *a = NULL;
    AtmDeploymentQualification *c = NULL;

    g_assert_true (
        atm_startup_qualify_deployment (
            path_a,
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            1,
            &a,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_startup_qualify_deployment (
            path_c,
            APP_ID,
            RUNTIME_ID,
            RUNTIME_BRANCH,
            1,
            &c,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpstr (
        a->platform_fingerprint,
        !=,
        c->platform_fingerprint
    );

    atm_deployment_qualification_free (a);
    atm_deployment_qualification_free (c);
    g_remove (path_a);
    g_remove (path_c);
    g_rmdir (directory);
    g_free (path_a);
    g_free (path_c);
    g_free (fixture_a);
    g_free (fixture_c);
    g_free (app_a);
    g_free (app_c);
    g_free (runtime);
    g_free (directory);
}

static void
test_storage_root_valid_and_created (void)
{
    GError *error = NULL;
    char *parent = g_dir_make_tmp (
        "atm-startup-storage-XXXXXX",
        &error
    );
    g_assert_no_error (error);

    char *root = g_build_filename (
        parent,
        "Ask the Model",
        NULL
    );
    AtmStorageQualification qualification = { 0 };

    g_assert_true (
        atm_startup_qualify_storage_root (
            root,
            &qualification,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (qualification.qualified);
    g_assert_true (qualification.created);
    g_assert_cmpuint (
        qualification.owner_uid,
        ==,
        (guint64) getuid ()
    );

    AtmStorageQualification second = { 0 };
    g_assert_true (
        atm_startup_qualify_storage_root (
            root,
            &second,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (second.qualified);
    g_assert_false (second.created);

    g_rmdir (root);
    g_rmdir (parent);
    g_free (root);
    g_free (parent);
}

static void
test_storage_root_rejects_symlink (void)
{
    GError *error = NULL;
    char *parent = g_dir_make_tmp (
        "atm-startup-symlink-XXXXXX",
        &error
    );
    g_assert_no_error (error);

    char *target = g_build_filename (
        parent,
        "target",
        NULL
    );
    char *root = g_build_filename (
        parent,
        "Ask the Model",
        NULL
    );

    g_assert_cmpint (g_mkdir (target, 0700), ==, 0);
    g_assert_cmpint (symlink (target, root), ==, 0);

    AtmStorageQualification qualification = { 0 };
    g_assert_false (
        atm_startup_qualify_storage_root (
            root,
            &qualification,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STARTUP_QUALIFICATION_ERROR,
        ATM_STARTUP_QUALIFICATION_ERROR_STORAGE
    );

    g_clear_error (&error);
    g_remove (root);
    g_rmdir (target);
    g_rmdir (parent);
    g_free (root);
    g_free (target);
    g_free (parent);
}

static void
test_storage_root_rejects_unsafe_permissions (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-startup-permissions-XXXXXX",
        &error
    );
    g_assert_no_error (error);

    g_assert_cmpint (chmod (root, 0770), ==, 0);

    AtmStorageQualification qualification = { 0 };
    g_assert_false (
        atm_startup_qualify_storage_root (
            root,
            &qualification,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STARTUP_QUALIFICATION_ERROR,
        ATM_STARTUP_QUALIFICATION_ERROR_STORAGE
    );

    g_clear_error (&error);
    chmod (root, 0700);
    g_rmdir (root);
    g_free (root);
}

static void
test_storage_root_rejects_regular_file (void)
{
    GError *error = NULL;
    char *parent = g_dir_make_tmp (
        "atm-startup-file-root-XXXXXX",
        &error
    );
    g_assert_no_error (error);

    char *root = g_build_filename (
        parent,
        "Ask the Model",
        NULL
    );

    g_assert_true (
        g_file_set_contents (
            root,
            "not-a-directory",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    AtmStorageQualification qualification = { 0 };
    g_assert_false (
        atm_startup_qualify_storage_root (
            root,
            &qualification,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STARTUP_QUALIFICATION_ERROR,
        ATM_STARTUP_QUALIFICATION_ERROR_STORAGE
    );

    g_clear_error (&error);
    g_remove (root);
    g_rmdir (parent);
    g_free (root);
    g_free (parent);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/startup/deployment/development-absent",
        test_development_when_metadata_absent
    );
    g_test_add_func (
        "/startup/deployment/valid-stable-fingerprint",
        test_valid_deployment_and_stable_fingerprint
    );
    g_test_add_func (
        "/startup/deployment/invalid-metadata",
        test_invalid_deployment_metadata
    );
    g_test_add_func (
        "/startup/deployment/commit-changes-fingerprint",
        test_app_commit_changes_fingerprint
    );
    g_test_add_func (
        "/startup/storage/valid-created",
        test_storage_root_valid_and_created
    );
    g_test_add_func (
        "/startup/storage/reject-symlink",
        test_storage_root_rejects_symlink
    );
    g_test_add_func (
        "/startup/storage/reject-unsafe-permissions",
        test_storage_root_rejects_unsafe_permissions
    );
    g_test_add_func (
        "/startup/storage/reject-regular-file",
        test_storage_root_rejects_regular_file
    );

    return g_test_run ();
}
