#include "capacity_measurement.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

GQuark
atm_capacity_measurement_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-capacity-measurement-error-quark"
    );
}

static void
set_errno_error (
    GError **error,
    const char *context
)
{
    g_set_error (
        error,
        ATM_CAPACITY_MEASUREMENT_ERROR,
        ATM_CAPACITY_MEASUREMENT_ERROR_IO,
        "%s: %s",
        context,
        g_strerror (errno)
    );
}

static gboolean
add_u64_checked (
    guint64 left,
    guint64 right,
    guint64 *out,
    GError **error
)
{
    if (G_MAXUINT64 - left < right) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_MEASUREMENT_ERROR,
            ATM_CAPACITY_MEASUREMENT_ERROR_OVERFLOW,
            "Capacity measurement overflowed a 64-bit counter."
        );
        return FALSE;
    }

    *out = left + right;
    return TRUE;
}

static gboolean
mul_u64_checked (
    guint64 left,
    guint64 right,
    guint64 *out,
    GError **error
)
{
    if (left != 0 && right > G_MAXUINT64 / left) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_MEASUREMENT_ERROR,
            ATM_CAPACITY_MEASUREMENT_ERROR_OVERFLOW,
            "Capacity measurement overflowed a 64-bit counter."
        );
        return FALSE;
    }

    *out = left * right;
    return TRUE;
}

static gboolean
allocated_bytes_from_stat (
    const GStatBuf *stat_buffer,
    guint64 *out_bytes,
    GError **error
)
{
    if (stat_buffer->st_blocks < 0) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_MEASUREMENT_ERROR,
            ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
            "Filesystem reported a negative allocated-block count."
        );
        return FALSE;
    }

    return mul_u64_checked (
        (guint64) stat_buffer->st_blocks,
        512,
        out_bytes,
        error
    );
}

gboolean
atm_capacity_measure_filesystem (
    const char *path,
    AtmFilesystemCapacityMeasurement *out_measurement,
    GError **error
)
{
    char *candidate = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (path[0] != '\0', FALSE);
    g_return_val_if_fail (out_measurement != NULL, FALSE);

    *out_measurement =
        (AtmFilesystemCapacityMeasurement) { 0 };

    candidate = g_strdup (path);

    for (;;) {
        GStatBuf stat_buffer;

        if (g_lstat (candidate, &stat_buffer) == 0) {
            struct statvfs fs;
            guint64 fragment_size;
            guint64 available_bytes;

            if (S_ISLNK (stat_buffer.st_mode)) {
                g_set_error_literal (
                    error,
                    ATM_CAPACITY_MEASUREMENT_ERROR,
                    ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
                    "Capacity measurement refuses a symlink filesystem anchor."
                );
                goto out;
            }

            if (statvfs (candidate, &fs) != 0) {
                set_errno_error (
                    error,
                    "Could not read filesystem capacity"
                );
                goto out;
            }

            fragment_size =
                fs.f_frsize != 0
                    ? (guint64) fs.f_frsize
                    : (guint64) fs.f_bsize;

            if (fragment_size == 0 ||
                !mul_u64_checked (
                    (guint64) fs.f_bavail,
                    fragment_size,
                    &available_bytes,
                    error
                )) {
                if (fragment_size == 0 && error != NULL &&
                    *error == NULL) {
                    g_set_error_literal (
                        error,
                        ATM_CAPACITY_MEASUREMENT_ERROR,
                        ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
                        "Filesystem reported no usable allocation unit."
                    );
                }
                goto out;
            }

            out_measurement->device_id =
                (guint64) stat_buffer.st_dev;
            out_measurement->available_bytes =
                available_bytes;
            out_measurement->available_inodes =
                (guint64) fs.f_favail;
            out_measurement->fragment_size =
                fragment_size;
            ok = TRUE;
            goto out;
        }

        if (errno != ENOENT) {
            set_errno_error (
                error,
                "Could not inspect filesystem measurement path"
            );
            goto out;
        }

        char *parent = g_path_get_dirname (candidate);

        if (g_strcmp0 (parent, candidate) == 0) {
            g_set_error_literal (
                error,
                ATM_CAPACITY_MEASUREMENT_ERROR,
                ATM_CAPACITY_MEASUREMENT_ERROR_IO,
                "Could not find an existing filesystem ancestor."
            );
            g_free (parent);
            goto out;
        }

        g_free (candidate);
        candidate = parent;
    }

out:
    g_free (candidate);
    return ok;
}

gboolean
atm_capacity_measure_regular_file (
    const char *path,
    AtmFileCapacityMeasurement *out_measurement,
    GError **error
)
{
    GStatBuf stat_buffer;
    guint64 allocated_bytes;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (path[0] != '\0', FALSE);
    g_return_val_if_fail (out_measurement != NULL, FALSE);

    *out_measurement =
        (AtmFileCapacityMeasurement) { 0 };

    if (g_lstat (path, &stat_buffer) != 0) {
        set_errno_error (
            error,
            "Could not inspect measured file"
        );
        return FALSE;
    }

    if (!S_ISREG (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode) ||
        stat_buffer.st_size < 0) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_MEASUREMENT_ERROR,
            ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
            "Capacity file measurement requires a real regular file."
        );
        return FALSE;
    }

    if (!allocated_bytes_from_stat (
            &stat_buffer,
            &allocated_bytes,
            error
        )) {
        return FALSE;
    }

    out_measurement->logical_bytes =
        (guint64) stat_buffer.st_size;
    out_measurement->allocated_bytes =
        allocated_bytes;
    return TRUE;
}

static gboolean
measure_tree_node (
    const char *path,
    gboolean is_root,
    AtmTreeCapacityMeasurement *measurement,
    GError **error
)
{
    GStatBuf stat_buffer;
    guint64 allocated_bytes;
    guint64 next;

    if (g_lstat (path, &stat_buffer) != 0) {
        set_errno_error (
            error,
            "Could not inspect measured tree entry"
        );
        return FALSE;
    }

    if (S_ISLNK (stat_buffer.st_mode) ||
        (!S_ISREG (stat_buffer.st_mode) &&
         !S_ISDIR (stat_buffer.st_mode))) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_MEASUREMENT_ERROR,
            ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
            "Capacity tree measurement refuses symlinks and special filesystem objects."
        );
        return FALSE;
    }

    if (!allocated_bytes_from_stat (
            &stat_buffer,
            &allocated_bytes,
            error
        ) ||
        !add_u64_checked (
            measurement->allocated_tree_bytes,
            allocated_bytes,
            &next,
            error
        )) {
        return FALSE;
    }
    measurement->allocated_tree_bytes = next;

    if (!is_root) {
        if (!add_u64_checked (
                measurement->entries,
                1,
                &next,
                error
            )) {
            return FALSE;
        }
        measurement->entries = next;
    }

    if (S_ISREG (stat_buffer.st_mode)) {
        if (stat_buffer.st_size < 0) {
            g_set_error_literal (
                error,
                ATM_CAPACITY_MEASUREMENT_ERROR,
                ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
                "Filesystem reported a negative regular-file size."
            );
            return FALSE;
        }

        if (!add_u64_checked (
                measurement->logical_regular_bytes,
                (guint64) stat_buffer.st_size,
                &next,
                error
            )) {
            return FALSE;
        }
        measurement->logical_regular_bytes = next;

        if (!add_u64_checked (
                measurement->regular_files,
                1,
                &next,
                error
            )) {
            return FALSE;
        }
        measurement->regular_files = next;
        return TRUE;
    }

    if (!is_root) {
        if (!add_u64_checked (
                measurement->directories,
                1,
                &next,
                error
            )) {
            return FALSE;
        }
        measurement->directories = next;
    }

    GDir *directory = g_dir_open (
        path,
        0,
        error
    );

    if (directory == NULL) {
        return FALSE;
    }

    const char *name;
    while ((name = g_dir_read_name (directory)) != NULL) {
        char *child = g_build_filename (
            path,
            name,
            NULL
        );
        gboolean child_ok = measure_tree_node (
            child,
            FALSE,
            measurement,
            error
        );
        g_free (child);

        if (!child_ok) {
            g_dir_close (directory);
            return FALSE;
        }
    }

    g_dir_close (directory);
    return TRUE;
}

gboolean
atm_capacity_measure_tree (
    const char *root,
    AtmTreeCapacityMeasurement *out_measurement,
    GError **error
)
{
    GStatBuf stat_buffer;

    g_return_val_if_fail (root != NULL, FALSE);
    g_return_val_if_fail (root[0] != '\0', FALSE);
    g_return_val_if_fail (out_measurement != NULL, FALSE);

    *out_measurement =
        (AtmTreeCapacityMeasurement) { 0 };

    if (g_lstat (root, &stat_buffer) != 0) {
        set_errno_error (
            error,
            "Could not inspect measured tree root"
        );
        return FALSE;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_set_error_literal (
            error,
            ATM_CAPACITY_MEASUREMENT_ERROR,
            ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT,
            "Capacity tree measurement requires a real directory root."
        );
        return FALSE;
    }

    return measure_tree_node (
        root,
        TRUE,
        out_measurement,
        error
    );
}
