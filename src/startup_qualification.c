#include "startup_qualification.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

GQuark
atm_startup_qualification_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-startup-qualification-error-quark"
    );
}

static gboolean
commit_is_valid (const char *value)
{
    if (value == NULL || strlen (value) != 64) {
        return FALSE;
    }

    for (gsize i = 0; i < 64; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (g_ascii_isalpha (value[i]) &&
             !g_ascii_islower (value[i]))) {
            return FALSE;
        }
    }

    return TRUE;
}

static char *
required_string (
    GKeyFile *key_file,
    const char *group,
    const char *key,
    GError **error
)
{
    GError *local_error = NULL;
    char *value = g_key_file_get_string (
        key_file,
        group,
        key,
        &local_error
    );

    if (value == NULL || value[0] == '\0') {
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Flatpak metadata is missing required %s/%s: %s",
            group,
            key,
            local_error != NULL
                ? local_error->message
                : "empty value"
        );
        g_clear_error (&local_error);
        g_free (value);
        return NULL;
    }

    g_clear_error (&local_error);
    return value;
}

static gint
compare_string_pointers (
    gconstpointer a,
    gconstpointer b,
    gpointer user_data
)
{
    const char *left = *(const char * const *) a;
    const char *right = *(const char * const *) b;

    (void) user_data;
    return g_strcmp0 (left, right);
}

static gboolean
extension_entry_is_valid (const char *entry)
{
    const char *equals;

    if (entry == NULL || entry[0] == '\0') {
        return FALSE;
    }

    equals = strrchr (entry, '=');
    if (equals == NULL || equals == entry || equals[1] == '\0') {
        return FALSE;
    }

    return commit_is_valid (equals + 1);
}

static gboolean
read_sorted_extensions (
    GKeyFile *key_file,
    const char *key,
    char ***out_values,
    GError **error
)
{
    GError *local_error = NULL;
    gsize length = 0;
    char **values = NULL;

    g_return_val_if_fail (out_values != NULL, FALSE);
    g_return_val_if_fail (*out_values == NULL, FALSE);

    if (!g_key_file_has_key (
            key_file,
            "Instance",
            key,
            NULL
        )) {
        *out_values = g_new0 (char *, 1);
        return TRUE;
    }

    values = g_key_file_get_string_list (
        key_file,
        "Instance",
        key,
        &length,
        &local_error
    );

    if (values == NULL) {
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Could not read Flatpak Instance/%s: %s",
            key,
            local_error != NULL
                ? local_error->message
                : "invalid list"
        );
        g_clear_error (&local_error);
        return FALSE;
    }

    if (length == 0 ||
        (length == 1 && values[0][0] == '\0')) {
        g_strfreev (values);
        *out_values = g_new0 (char *, 1);
        return TRUE;
    }

    for (gsize i = 0; i < length; i++) {
        if (!extension_entry_is_valid (values[i])) {
            g_set_error (
                error,
                ATM_STARTUP_QUALIFICATION_ERROR,
                ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
                "Flatpak Instance/%s contains invalid extension identity.",
                key
            );
            g_strfreev (values);
            return FALSE;
        }
    }

    if (length > 1) {
        g_qsort_with_data (
            values,
            length,
            sizeof (char *),
            compare_string_pointers,
            NULL
        );
    }

    *out_values = values;
    return TRUE;
}

static gboolean
runtime_ref_matches (
    const char *runtime_ref,
    const char *expected_runtime_id,
    const char *expected_runtime_branch,
    const char *architecture
)
{
    char **parts = g_strsplit (runtime_ref, "/", -1);
    gboolean matches =
        g_strv_length (parts) == 3 &&
        g_strcmp0 (parts[0], expected_runtime_id) == 0 &&
        g_strcmp0 (parts[1], architecture) == 0 &&
        g_strcmp0 (parts[2], expected_runtime_branch) == 0;

    g_strfreev (parts);
    return matches;
}

static char *
build_platform_fingerprint (
    const AtmDeploymentQualification *qualification,
    guint policy_version
)
{
    GString *canonical = g_string_new (NULL);

    g_string_append_printf (
        canonical,
        "policy=%u\napplication=%s\napp-ref=%s\napp-commit=%s\n"
        "runtime-ref=%s\nruntime-commit=%s\narch=%s\nbranch=%s\n",
        policy_version,
        qualification->application_id,
        qualification->application_ref,
        qualification->application_commit,
        qualification->runtime_ref,
        qualification->runtime_commit,
        qualification->architecture,
        qualification->branch
    );

    for (
        gsize i = 0;
        qualification->application_extensions != NULL &&
            qualification->application_extensions[i] != NULL;
        i++
    ) {
        g_string_append_printf (
            canonical,
            "app-extension=%s\n",
            qualification->application_extensions[i]
        );
    }

    for (
        gsize i = 0;
        qualification->runtime_extensions != NULL &&
            qualification->runtime_extensions[i] != NULL;
        i++
    ) {
        g_string_append_printf (
            canonical,
            "runtime-extension=%s\n",
            qualification->runtime_extensions[i]
        );
    }

    char *digest = g_compute_checksum_for_string (
        G_CHECKSUM_SHA256,
        canonical->str,
        -1
    );

    g_string_free (canonical, TRUE);
    return digest;
}

void
atm_deployment_qualification_free (
    AtmDeploymentQualification *qualification
)
{
    if (qualification == NULL) {
        return;
    }

    g_free (qualification->application_id);
    g_free (qualification->application_ref);
    g_free (qualification->application_commit);
    g_free (qualification->runtime_ref);
    g_free (qualification->runtime_commit);
    g_free (qualification->architecture);
    g_free (qualification->branch);
    g_free (qualification->flatpak_version);
    g_strfreev (qualification->application_extensions);
    g_strfreev (qualification->runtime_extensions);
    g_free (qualification->platform_fingerprint);
    g_free (qualification);
}

gboolean
atm_startup_qualify_deployment (
    const char *flatpak_info_path,
    const char *expected_application_id,
    const char *expected_runtime_id,
    const char *expected_runtime_branch,
    guint policy_version,
    AtmDeploymentQualification **out_qualification,
    GError **error
)
{
    GKeyFile *key_file = NULL;
    AtmDeploymentQualification *qualification = NULL;
    GError *local_error = NULL;
    char *runtime_ref = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (flatpak_info_path != NULL, FALSE);
    g_return_val_if_fail (expected_application_id != NULL, FALSE);
    g_return_val_if_fail (expected_runtime_id != NULL, FALSE);
    g_return_val_if_fail (expected_runtime_branch != NULL, FALSE);
    g_return_val_if_fail (out_qualification != NULL, FALSE);
    g_return_val_if_fail (*out_qualification == NULL, FALSE);

    key_file = g_key_file_new ();

    if (!g_key_file_load_from_file (
            key_file,
            flatpak_info_path,
            G_KEY_FILE_NONE,
            &local_error
        )) {
        if (g_error_matches (
                local_error,
                G_FILE_ERROR,
                G_FILE_ERROR_NOENT
            )) {
            qualification = g_new0 (
                AtmDeploymentQualification,
                1
            );
            qualification->execution_mode =
                ATM_EXECUTION_MODE_DEVELOPMENT;
            qualification->platform_qualified = FALSE;
            *out_qualification = qualification;
            qualification = NULL;
            g_clear_error (&local_error);
            ok = TRUE;
            goto out;
        }

        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Could not load Flatpak instance metadata: %s",
            local_error != NULL
                ? local_error->message
                : "unknown error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    qualification = g_new0 (
        AtmDeploymentQualification,
        1
    );
    qualification->execution_mode =
        ATM_EXECUTION_MODE_FLATPAK;

    qualification->application_id = required_string (
        key_file,
        "Application",
        "name",
        error
    );
    if (qualification->application_id == NULL) {
        goto out;
    }

    if (g_strcmp0 (
            qualification->application_id,
            expected_application_id
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Flatpak application identity does not match Ask the Model."
        );
        goto out;
    }

    runtime_ref = required_string (
        key_file,
        "Application",
        "runtime",
        error
    );
    if (runtime_ref == NULL) {
        goto out;
    }
    qualification->runtime_ref = g_steal_pointer (
        &runtime_ref
    );

    qualification->architecture = required_string (
        key_file,
        "Instance",
        "arch",
        error
    );
    qualification->branch = required_string (
        key_file,
        "Instance",
        "branch",
        error
    );
    qualification->flatpak_version = required_string (
        key_file,
        "Instance",
        "flatpak-version",
        error
    );
    qualification->application_commit = required_string (
        key_file,
        "Instance",
        "app-commit",
        error
    );
    qualification->runtime_commit = required_string (
        key_file,
        "Instance",
        "runtime-commit",
        error
    );

    if (qualification->architecture == NULL ||
        qualification->branch == NULL ||
        qualification->flatpak_version == NULL ||
        qualification->application_commit == NULL ||
        qualification->runtime_commit == NULL) {
        goto out;
    }

    if (!commit_is_valid (qualification->application_commit) ||
        !commit_is_valid (qualification->runtime_commit)) {
        g_set_error_literal (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Flatpak deployment commit identity is invalid."
        );
        goto out;
    }

    if (!runtime_ref_matches (
            qualification->runtime_ref,
            expected_runtime_id,
            expected_runtime_branch,
            qualification->architecture
        )) {
        g_set_error_literal (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Flatpak runtime identity does not match the supported runtime."
        );
        goto out;
    }

    qualification->application_ref = g_strdup_printf (
        "app/%s/%s/%s",
        qualification->application_id,
        qualification->architecture,
        qualification->branch
    );

    if (!read_sorted_extensions (
            key_file,
            "app-extensions",
            &qualification->application_extensions,
            error
        ) ||
        !read_sorted_extensions (
            key_file,
            "runtime-extensions",
            &qualification->runtime_extensions,
            error
        )) {
        goto out;
    }

    qualification->platform_fingerprint =
        build_platform_fingerprint (
            qualification,
            policy_version
        );

    if (qualification->platform_fingerprint == NULL) {
        g_set_error_literal (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_METADATA,
            "Could not compute the startup platform fingerprint."
        );
        goto out;
    }

    qualification->platform_qualified = TRUE;
    *out_qualification = g_steal_pointer (&qualification);
    ok = TRUE;

out:
    g_clear_error (&local_error);
    g_clear_pointer (
        &qualification,
        atm_deployment_qualification_free
    );
    g_clear_pointer (&runtime_ref, g_free);
    g_clear_pointer (&key_file, g_key_file_unref);
    return ok;
}

gboolean
atm_startup_qualify_storage_root (
    const char *storage_root,
    AtmStorageQualification *out_qualification,
    GError **error
)
{
    gboolean created = FALSE;
    int directory_fd = -1;
    int probe_fd = -1;
    char probe_name[96] = { 0 };
    struct stat stat_buffer;
    gboolean ok = FALSE;

    g_return_val_if_fail (storage_root != NULL, FALSE);
    g_return_val_if_fail (out_qualification != NULL, FALSE);

    memset (out_qualification, 0, sizeof *out_qualification);

    if (g_mkdir (storage_root, 0700) == 0) {
        created = TRUE;
    } else if (errno != EEXIST) {
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "Could not create the AtM storage root: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    directory_fd = g_open (
        storage_root,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC,
        0
    );

    if (directory_fd < 0) {
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "AtM storage root is not a safe real directory: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (fstat (directory_fd, &stat_buffer) != 0) {
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "Could not inspect the opened AtM storage root: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        stat_buffer.st_uid != getuid () ||
        (stat_buffer.st_mode & 0022) != 0) {
        g_set_error_literal (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "AtM storage root ownership or permissions are unsafe."
        );
        goto out;
    }

    for (guint attempt = 0; attempt < 16; attempt++) {
        g_snprintf (
            probe_name,
            sizeof probe_name,
            ".atm-startup-probe-%08x-%02u",
            g_random_int (),
            attempt
        );

        probe_fd = openat (
            directory_fd,
            probe_name,
            O_WRONLY | O_CREAT | O_EXCL |
                O_NOFOLLOW | O_CLOEXEC,
            0600
        );

        if (probe_fd >= 0) {
            break;
        }

        if (errno != EEXIST) {
            g_set_error (
                error,
                ATM_STARTUP_QUALIFICATION_ERROR,
                ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
                "Could not create a storage write probe: %s.",
                g_strerror (errno)
            );
            goto out;
        }
    }

    if (probe_fd < 0) {
        g_set_error_literal (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "Could not allocate a unique storage write probe."
        );
        goto out;
    }

    {
        const char probe_data[] = "ok\n";
        ssize_t written = write (
            probe_fd,
            probe_data,
            sizeof probe_data - 1
        );

        if (written != (ssize_t) (sizeof probe_data - 1) ||
            fsync (probe_fd) != 0) {
            g_set_error (
                error,
                ATM_STARTUP_QUALIFICATION_ERROR,
                ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
                "AtM storage write probe failed: %s.",
                g_strerror (errno)
            );
            goto out;
        }
    }

    if (close (probe_fd) != 0) {
        probe_fd = -1;
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "Could not close the storage write probe: %s.",
            g_strerror (errno)
        );
        goto out;
    }
    probe_fd = -1;

    if (unlinkat (
            directory_fd,
            probe_name,
            0
        ) != 0) {
        g_set_error (
            error,
            ATM_STARTUP_QUALIFICATION_ERROR,
            ATM_STARTUP_QUALIFICATION_ERROR_STORAGE,
            "Could not remove the storage write probe: %s.",
            g_strerror (errno)
        );
        goto out;
    }
    probe_name[0] = '\0';

    out_qualification->qualified = TRUE;
    out_qualification->created = created;
    out_qualification->mode =
        (guint32) (stat_buffer.st_mode & 07777);
    out_qualification->owner_uid =
        (guint64) stat_buffer.st_uid;
    ok = TRUE;

out:
    if (probe_fd >= 0) {
        close (probe_fd);
    }

    if (probe_name[0] != '\0' &&
        directory_fd >= 0) {
        unlinkat (
            directory_fd,
            probe_name,
            0
        );
    }

    if (directory_fd >= 0) {
        close (directory_fd);
    }

    return ok;
}
