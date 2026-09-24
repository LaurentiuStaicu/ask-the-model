#include "archive_extract.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ATM_EXTRACT_BUFFER_BYTES (64 * 1024)

GQuark
atm_archive_error_quark (void)
{
    return g_quark_from_static_string ("atm-archive-error-quark");
}

static AtmArchiveError
archive_errno_error_code (
    int err_no
)
{
    return err_no == ENOSPC
        ? ATM_ARCHIVE_ERROR_NO_SPACE
        : ATM_ARCHIVE_ERROR_IO;
}

static void
set_archive_errno_error (
    GError **error,
    const char *context
)
{
    int saved_errno = errno;

    g_set_error (
        error,
        ATM_ARCHIVE_ERROR,
        archive_errno_error_code (
            saved_errno
        ),
        "%s: %s.",
        context,
        g_strerror (saved_errno)
    );
}

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);
    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static gboolean
write_all_fd (
    int fd,
    const guint8 *data,
    gsize length,
    GError **error
)
{
    gsize offset = 0;

    while (offset < length) {
        ssize_t written = write (
            fd,
            data + offset,
            length - offset
        );

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            set_archive_errno_error (
                error,
                "Could not write extracted repository file"
            );
            return FALSE;
        }

        offset += (gsize) written;
    }

    return TRUE;
}

static gboolean
normalize_entry_path (
    const char *pathname,
    char **archive_prefix,
    char **out_relative,
    gboolean *out_is_root,
    GError **error
)
{
    char *copy;
    char **parts;
    gsize part_count = 0;
    GString *relative;

    *out_relative = NULL;
    *out_is_root = FALSE;

    if (pathname == NULL || pathname[0] == '\0') {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_UNSAFE_PATH,
            "Archive entry has no pathname."
        );
        return FALSE;
    }

    if (g_path_is_absolute (pathname)) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_UNSAFE_PATH,
            "Archive entry uses an absolute pathname."
        );
        return FALSE;
    }

    copy = g_strdup (pathname);

    while (strlen (copy) > 0 &&
           copy[strlen (copy) - 1] == '/') {
        copy[strlen (copy) - 1] = '\0';
    }

    if (copy[0] == '\0') {
        g_free (copy);
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_UNSAFE_PATH,
            "Archive entry pathname is empty after normalization."
        );
        return FALSE;
    }

    parts = g_strsplit (copy, "/", -1);
    g_free (copy);

    while (parts[part_count] != NULL) {
        const char *part = parts[part_count];

        if (part[0] == '\0' ||
            strcmp (part, ".") == 0 ||
            strcmp (part, "..") == 0) {
            g_strfreev (parts);
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_UNSAFE_PATH,
                "Archive entry contains an unsafe path component."
            );
            return FALSE;
        }

        part_count++;
    }

    if (part_count == 0) {
        g_strfreev (parts);
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_UNSAFE_PATH,
            "Archive entry has no usable path component."
        );
        return FALSE;
    }

    if (*archive_prefix == NULL) {
        *archive_prefix = g_strdup (parts[0]);
    } else if (strcmp (*archive_prefix, parts[0]) != 0) {
        g_strfreev (parts);
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_PREFIX,
            "Archive contains more than one top-level prefix."
        );
        return FALSE;
    }

    if (part_count == 1) {
        *out_is_root = TRUE;
        g_strfreev (parts);
        return TRUE;
    }

    relative = g_string_new (parts[1]);

    for (gsize i = 2; i < part_count; i++) {
        g_string_append_c (relative, G_DIR_SEPARATOR);
        g_string_append (relative, parts[i]);
    }

    *out_relative = g_string_free (relative, FALSE);
    g_strfreev (parts);
    return TRUE;
}

static gboolean
inspection_register_path (
    GHashTable *paths,
    const char *path,
    gboolean directory,
    AtmArchiveInspection *inspection,
    GError **error
)
{
    gpointer existing =
        g_hash_table_lookup (
            paths,
            path
        );
    guint kind =
        directory ? 1u : 2u;

    if (existing != NULL) {
        guint existing_kind =
            GPOINTER_TO_UINT (existing);

        if (directory &&
            existing_kind == 1u) {
            return TRUE;
        }

        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Repository archive contains a duplicate or conflicting materialized path."
        );
        return FALSE;
    }

    g_hash_table_insert (
        paths,
        g_strdup (path),
        GUINT_TO_POINTER (kind)
    );

    inspection->materialized_entries++;

    if (directory) {
        inspection->directories++;
    } else {
        inspection->regular_files++;
    }

    return TRUE;
}

static gboolean
inspection_register_relative_path (
    GHashTable *paths,
    const char *relative,
    gboolean final_is_directory,
    AtmArchiveInspection *inspection,
    GError **error
)
{
    char **parts =
        g_strsplit (
            relative,
            G_DIR_SEPARATOR_S,
            -1
        );
    GString *prefix =
        g_string_new (NULL);
    gsize count = 0;

    while (parts[count] != NULL) {
        count++;
    }

    if (count == 0) {
        g_string_free (prefix, TRUE);
        g_strfreev (parts);
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Repository archive contains an empty materialized path."
        );
        return FALSE;
    }

    for (gsize i = 0; i < count; i++) {
        if (i > 0) {
            g_string_append_c (
                prefix,
                G_DIR_SEPARATOR
            );
        }

        g_string_append (
            prefix,
            parts[i]
        );

        gboolean is_final =
            i + 1 == count;
        gboolean directory =
            !is_final ||
            final_is_directory;

        if (!inspection_register_path (
                paths,
                prefix->str,
                directory,
                inspection,
                error
            )) {
            g_string_free (prefix, TRUE);
            g_strfreev (parts);
            return FALSE;
        }
    }

    g_string_free (prefix, TRUE);
    g_strfreev (parts);
    return TRUE;
}

gboolean
atm_archive_inspect_snapshot (
    const char *archive_path,
    const AtmArchiveLimits *limits,
    AtmArchiveInspection *out_inspection,
    GError **error
)
{
    struct archive *reader = NULL;
    struct archive_entry *entry = NULL;
    char *archive_prefix = NULL;
    GHashTable *paths = NULL;
    AtmArchiveInspection inspection = {
        .archive_entries = 0,
        /*
         * Extraction creates one operation-owned root directory before
         * materializing archive-relative paths beneath it.
         */
        .materialized_entries = 1,
        .regular_files = 0,
        .directories = 0,
        .logical_regular_bytes = 0,
        .largest_regular_file_bytes = 0
    };
    gboolean ok = FALSE;
    int result;

    g_return_val_if_fail (
        archive_path != NULL,
        FALSE
    );
    g_return_val_if_fail (
        limits != NULL,
        FALSE
    );
    g_return_val_if_fail (
        limits->max_entries > 0,
        FALSE
    );
    g_return_val_if_fail (
        limits->max_file_bytes > 0,
        FALSE
    );
    g_return_val_if_fail (
        limits->max_total_bytes > 0,
        FALSE
    );
    g_return_val_if_fail (
        out_inspection != NULL,
        FALSE
    );

    *out_inspection =
        (AtmArchiveInspection) { 0 };

    paths = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );

    reader = archive_read_new ();

    if (reader == NULL) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_OPEN,
            "Could not allocate libarchive reader for archive inspection."
        );
        goto out;
    }

    if (archive_read_support_filter_gzip (reader) != ARCHIVE_OK ||
        archive_read_support_format_tar (reader) != ARCHIVE_OK) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Could not enable gzip/tar support for archive inspection."
        );
        goto out;
    }

    result = archive_read_open_filename (
        reader,
        archive_path,
        64 * 1024
    );

    if (result != ARCHIVE_OK) {
        g_set_error (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_OPEN,
            "Could not open repository archive for inspection: %s.",
            archive_error_string (reader)
        );
        goto out;
    }

    while ((result = archive_read_next_header (
                reader,
                &entry
            )) == ARCHIVE_OK) {
        const char *pathname =
            archive_entry_pathname (entry);
        const char *symlink_target =
            archive_entry_symlink (entry);
        const char *hardlink_target =
            archive_entry_hardlink (entry);
        mode_t filetype =
            archive_entry_filetype (entry);
        char *relative = NULL;
        gboolean is_root = FALSE;

        inspection.archive_entries++;

        if (inspection.archive_entries >
            limits->max_entries) {
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_LIMIT,
                "Repository archive exceeds the entry-count limit."
            );
            goto out;
        }

        if (symlink_target != NULL ||
            hardlink_target != NULL) {
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
                "Repository archive contains a symbolic or hard link."
            );
            goto out;
        }

        if (!normalize_entry_path (
                pathname,
                &archive_prefix,
                &relative,
                &is_root,
                error
            )) {
            goto out;
        }

        if (is_root) {
            if (filetype != AE_IFDIR) {
                g_free (relative);
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
                    "Repository archive root entry is not a directory."
                );
                goto out;
            }

            g_free (relative);

            result =
                archive_read_data_skip (
                    reader
                );
            if (result != ARCHIVE_OK) {
                g_set_error (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_FORMAT,
                    "Could not skip repository archive root entry: %s.",
                    archive_error_string (reader)
                );
                goto out;
            }

            continue;
        }

        if (filetype == AE_IFDIR) {
            if (!inspection_register_relative_path (
                    paths,
                    relative,
                    TRUE,
                    &inspection,
                    error
                )) {
                g_free (relative);
                goto out;
            }
        } else if (filetype == AE_IFREG) {
            if (!archive_entry_size_is_set (entry)) {
                g_free (relative);
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_FORMAT,
                    "Repository archive regular file has no declared size."
                );
                goto out;
            }

            int64_t declared_size =
                archive_entry_size (entry);

            if (declared_size < 0 ||
                (guint64) declared_size >
                    limits->max_file_bytes ||
                inspection.logical_regular_bytes >
                    limits->max_total_bytes -
                    (guint64) declared_size) {
                g_free (relative);
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_LIMIT,
                    "Repository archive exceeds an extraction size limit."
                );
                goto out;
            }

            if (!inspection_register_relative_path (
                    paths,
                    relative,
                    FALSE,
                    &inspection,
                    error
                )) {
                g_free (relative);
                goto out;
            }

            inspection.logical_regular_bytes +=
                (guint64) declared_size;
            inspection.largest_regular_file_bytes =
                MAX (
                    inspection.largest_regular_file_bytes,
                    (guint64) declared_size
                );
        } else {
            g_free (relative);
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
                "Repository archive contains an unsupported special entry."
            );
            goto out;
        }

        g_free (relative);

        result =
            archive_read_data_skip (
                reader
            );

        if (result != ARCHIVE_OK) {
            g_set_error (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_FORMAT,
                "Could not skip repository archive entry data: %s.",
                archive_error_string (reader)
            );
            goto out;
        }
    }

    if (result != ARCHIVE_EOF) {
        g_set_error (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Could not finish inspecting repository archive: %s.",
            archive_error_string (reader)
        );
        goto out;
    }

    if (archive_prefix == NULL) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Repository archive contains no entries."
        );
        goto out;
    }

    *out_inspection = inspection;
    ok = TRUE;

out:
    if (reader != NULL) {
        archive_read_close (reader);
        archive_read_free (reader);
    }

    if (paths != NULL) {
        g_hash_table_destroy (paths);
    }

    g_free (archive_prefix);
    return ok;
}

gboolean
atm_archive_extract_snapshot_cancellable (
    const char *archive_path,
    const char *destination,
    const AtmArchiveLimits *limits,
    GCancellable *cancellable,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    struct archive *reader = NULL;
    struct archive_entry *entry = NULL;
    char *archive_prefix = NULL;
    guint64 entry_count = 0;
    guint64 total_bytes = 0;
    gboolean destination_created = FALSE;
    gboolean ok = FALSE;
    int result;

    g_return_val_if_fail (archive_path != NULL, FALSE);
    g_return_val_if_fail (destination != NULL, FALSE);
    g_return_val_if_fail (limits != NULL, FALSE);
    g_return_val_if_fail (limits->max_entries > 0, FALSE);
    g_return_val_if_fail (limits->max_file_bytes > 0, FALSE);
    g_return_val_if_fail (limits->max_total_bytes > 0, FALSE);

    if (out_entries != NULL) {
        *out_entries = 0;
    }

    if (out_total_bytes != NULL) {
        *out_total_bytes = 0;
    }

    if (cancellable != NULL &&
        g_cancellable_set_error_if_cancelled (cancellable, error)) {
        return FALSE;
    }

    if (g_mkdir (destination, 0700) != 0) {
        set_archive_errno_error (
            error,
            "Could not create empty extraction directory"
        );
        return FALSE;
    }

    destination_created = TRUE;
    reader = archive_read_new ();

    if (reader == NULL) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_OPEN,
            "Could not allocate libarchive reader."
        );
        goto out;
    }

    if (archive_read_support_filter_gzip (reader) != ARCHIVE_OK ||
        archive_read_support_format_tar (reader) != ARCHIVE_OK) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Could not enable gzip/tar support in libarchive."
        );
        goto out;
    }

    result = archive_read_open_filename (
        reader,
        archive_path,
        64 * 1024
    );

    if (result != ARCHIVE_OK) {
        g_set_error (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_OPEN,
            "Could not open repository archive: %s.",
            archive_error_string (reader)
        );
        goto out;
    }

    while ((result = archive_read_next_header (reader, &entry)) == ARCHIVE_OK) {
        if (cancellable != NULL &&
            g_cancellable_set_error_if_cancelled (cancellable, error)) {
            goto out;
        }

        const char *pathname = archive_entry_pathname (entry);
        const char *symlink_target = archive_entry_symlink (entry);
        const char *hardlink_target = archive_entry_hardlink (entry);
        mode_t filetype = archive_entry_filetype (entry);
        char *relative = NULL;
        gboolean is_root = FALSE;

        entry_count++;
        if (entry_count > limits->max_entries) {
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_LIMIT,
                "Repository archive exceeds the entry-count limit."
            );
            goto out;
        }

        if (symlink_target != NULL || hardlink_target != NULL) {
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
                "Repository archive contains a symbolic or hard link."
            );
            goto out;
        }

        if (!normalize_entry_path (
                pathname,
                &archive_prefix,
                &relative,
                &is_root,
                error
            )) {
            goto out;
        }

        if (is_root) {
            if (filetype != AE_IFDIR) {
                g_free (relative);
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
                    "Repository archive root entry is not a directory."
                );
                goto out;
            }

            g_free (relative);
            continue;
        }

        char *output_path = g_build_filename (
            destination,
            relative,
            NULL
        );

        if (filetype == AE_IFDIR) {
            if (g_mkdir_with_parents (output_path, 0700) != 0) {
                set_archive_errno_error (
                    error,
                    "Could not create extracted repository directory"
                );
                g_free (output_path);
                g_free (relative);
                goto out;
            }
        } else if (filetype == AE_IFREG) {
            int64_t declared_size;
            char *parent;
            int fd;
            guint64 file_bytes = 0;
            guint8 buffer[ATM_EXTRACT_BUFFER_BYTES];

            if (!archive_entry_size_is_set (entry)) {
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_FORMAT,
                    "Repository archive regular file has no declared size."
                );
                g_free (output_path);
                g_free (relative);
                goto out;
            }

            declared_size = archive_entry_size (entry);

            if (declared_size < 0 ||
                (guint64) declared_size > limits->max_file_bytes ||
                total_bytes + (guint64) declared_size >
                    limits->max_total_bytes) {
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_LIMIT,
                    "Repository archive exceeds an extraction size limit."
                );
                g_free (output_path);
                g_free (relative);
                goto out;
            }

            parent = g_path_get_dirname (output_path);
            if (g_mkdir_with_parents (parent, 0700) != 0) {
                set_archive_errno_error (
                    error,
                    "Could not create parent directory for extracted file"
                );
                g_free (parent);
                g_free (output_path);
                g_free (relative);
                goto out;
            }
            g_free (parent);

            fd = g_open (
                output_path,
                O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW,
                0600
            );

            if (fd < 0) {
                set_archive_errno_error (
                    error,
                    "Could not create extracted repository file"
                );
                g_free (output_path);
                g_free (relative);
                goto out;
            }

            while (TRUE) {
                if (cancellable != NULL &&
                    g_cancellable_set_error_if_cancelled (
                        cancellable,
                        error
                    )) {
                    close (fd);
                    g_remove (output_path);
                    g_free (output_path);
                    g_free (relative);
                    goto out;
                }

                la_ssize_t count = archive_read_data (
                    reader,
                    buffer,
                    sizeof buffer
                );

                if (count == 0) {
                    break;
                }

                if (count < 0) {
                    g_set_error (
                        error,
                        ATM_ARCHIVE_ERROR,
                        ATM_ARCHIVE_ERROR_FORMAT,
                        "Could not read archive entry data: %s.",
                        archive_error_string (reader)
                    );
                    close (fd);
                    g_remove (output_path);
                    g_free (output_path);
                    g_free (relative);
                    goto out;
                }

                file_bytes += (guint64) count;
                total_bytes += (guint64) count;

                if (file_bytes > limits->max_file_bytes ||
                    total_bytes > limits->max_total_bytes) {
                    g_set_error_literal (
                        error,
                        ATM_ARCHIVE_ERROR,
                        ATM_ARCHIVE_ERROR_LIMIT,
                        "Repository archive exceeded an extraction limit while decompressing."
                    );
                    close (fd);
                    g_remove (output_path);
                    g_free (output_path);
                    g_free (relative);
                    goto out;
                }

                if (!write_all_fd (
                        fd,
                        buffer,
                        (gsize) count,
                        error
                    )) {
                    close (fd);
                    g_remove (output_path);
                    g_free (output_path);
                    g_free (relative);
                    goto out;
                }
            }

            if (close (fd) != 0) {
                set_archive_errno_error (
                    error,
                    "Could not close extracted repository file"
                );
                g_remove (output_path);
                g_free (output_path);
                g_free (relative);
                goto out;
            }

            if (file_bytes != (guint64) declared_size) {
                g_set_error_literal (
                    error,
                    ATM_ARCHIVE_ERROR,
                    ATM_ARCHIVE_ERROR_FORMAT,
                    "Extracted file size does not match archive metadata."
                );
                g_remove (output_path);
                g_free (output_path);
                g_free (relative);
                goto out;
            }
        } else {
            g_set_error_literal (
                error,
                ATM_ARCHIVE_ERROR,
                ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
                "Repository archive contains an unsupported special entry."
            );
            g_free (output_path);
            g_free (relative);
            goto out;
        }

        g_free (output_path);
        g_free (relative);
    }

    if (result != ARCHIVE_EOF) {
        g_set_error (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Could not finish reading repository archive: %s.",
            archive_error_string (reader)
        );
        goto out;
    }

    if (archive_prefix == NULL) {
        g_set_error_literal (
            error,
            ATM_ARCHIVE_ERROR,
            ATM_ARCHIVE_ERROR_FORMAT,
            "Repository archive contains no entries."
        );
        goto out;
    }

    if (cancellable != NULL &&
        g_cancellable_set_error_if_cancelled (cancellable, error)) {
        goto out;
    }

    if (out_entries != NULL) {
        *out_entries = entry_count;
    }

    if (out_total_bytes != NULL) {
        *out_total_bytes = total_bytes;
    }

    ok = TRUE;

out:
    if (reader != NULL) {
        archive_read_close (reader);
        archive_read_free (reader);
    }

    g_free (archive_prefix);

    if (!ok && destination_created) {
        remove_tree_best_effort (destination);
    }

    return ok;
}

gboolean
atm_archive_extract_snapshot (
    const char *archive_path,
    const char *destination,
    const AtmArchiveLimits *limits,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
)
{
    return atm_archive_extract_snapshot_cancellable (
        archive_path,
        destination,
        limits,
        NULL,
        out_entries,
        out_total_bytes,
        error
    );
}
