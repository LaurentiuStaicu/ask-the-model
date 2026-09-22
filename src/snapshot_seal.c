#include "snapshot_seal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ATM_SEAL_BUFFER_BYTES (64 * 1024)

typedef struct {
    char *path;
    char mode[7];
    guint64 size;
    char *sha256;
} AtmSealEntry;

GQuark
atm_snapshot_seal_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-snapshot-seal-error-quark"
    );
}

static void
seal_entry_free (AtmSealEntry *entry)
{
    if (entry == NULL) {
        return;
    }

    g_free (entry->path);
    g_free (entry->sha256);
    g_free (entry);
}

static gint
compare_entries (
    gconstpointer a,
    gconstpointer b
)
{
    const AtmSealEntry *left =
        *(AtmSealEntry * const *) a;
    const AtmSealEntry *right =
        *(AtmSealEntry * const *) b;

    return g_strcmp0 (left->path, right->path);
}

static gboolean
hash_open_file (
    int fd,
    char **out_sha256,
    guint64 *out_size,
    GError **error
)
{
    GChecksum *checksum = g_checksum_new (
        G_CHECKSUM_SHA256
    );
    guint8 buffer[ATM_SEAL_BUFFER_BYTES];
    guint64 total = 0;

    if (checksum == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "SHA-256 support is unavailable."
        );
        return FALSE;
    }

    while (TRUE) {
        ssize_t count = read (
            fd,
            buffer,
            sizeof buffer
        );

        if (count == 0) {
            break;
        }

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Could not read snapshot file: %s.",
                g_strerror (errno)
            );
            g_checksum_free (checksum);
            return FALSE;
        }

        g_checksum_update (
            checksum,
            buffer,
            (gsize) count
        );
        total += (guint64) count;
    }

    *out_sha256 = g_strdup (
        g_checksum_get_string (checksum)
    );
    *out_size = total;
    g_checksum_free (checksum);

    return *out_sha256 != NULL;
}

static gboolean
collect_directory (
    int directory_fd,
    const char *prefix,
    GPtrArray *entries,
    guint64 *total_bytes,
    GError **error
)
{
    int scan_fd = dup (directory_fd);
    DIR *directory = NULL;
    struct dirent *item;

    if (scan_fd < 0) {
        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not duplicate snapshot directory descriptor: %s.",
            g_strerror (errno)
        );
        return FALSE;
    }

    directory = fdopendir (scan_fd);
    if (directory == NULL) {
        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not enumerate snapshot directory: %s.",
            g_strerror (errno)
        );
        close (scan_fd);
        return FALSE;
    }

    while ((item = readdir (directory)) != NULL) {
        struct stat st;
        char *relative_path = NULL;

        if (strcmp (item->d_name, ".") == 0 ||
            strcmp (item->d_name, "..") == 0) {
            continue;
        }

        if (!g_utf8_validate (
                item->d_name,
                -1,
                NULL
            )) {
            g_set_error_literal (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_INVALID_NAME,
                "Snapshot contains a non-UTF-8 path name."
            );
            closedir (directory);
            return FALSE;
        }

        relative_path =
            prefix[0] == '\0'
                ? g_strdup (item->d_name)
                : g_strdup_printf (
                    "%s/%s",
                    prefix,
                    item->d_name
                );

        if (fstatat (
                directory_fd,
                item->d_name,
                &st,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Could not inspect snapshot path '%s': %s.",
                relative_path,
                g_strerror (errno)
            );
            g_free (relative_path);
            closedir (directory);
            return FALSE;
        }

        if (S_ISLNK (st.st_mode)) {
            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_UNSAFE_ENTRY,
                "Snapshot contains symlink '%s'.",
                relative_path
            );
            g_free (relative_path);
            closedir (directory);
            return FALSE;
        }

        if (S_ISDIR (st.st_mode)) {
            int child_fd = openat (
                directory_fd,
                item->d_name,
                O_RDONLY | O_DIRECTORY |
                    O_NOFOLLOW | O_CLOEXEC
            );

            if (child_fd < 0) {
                g_set_error (
                    error,
                    ATM_SNAPSHOT_SEAL_ERROR,
                    ATM_SNAPSHOT_SEAL_ERROR_IO,
                    "Could not open snapshot directory '%s': %s.",
                    relative_path,
                    g_strerror (errno)
                );
                g_free (relative_path);
                closedir (directory);
                return FALSE;
            }

            gboolean ok = collect_directory (
                child_fd,
                relative_path,
                entries,
                total_bytes,
                error
            );
            close (child_fd);
            g_free (relative_path);

            if (!ok) {
                closedir (directory);
                return FALSE;
            }

            continue;
        }

        if (!S_ISREG (st.st_mode)) {
            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_UNSAFE_ENTRY,
                "Snapshot contains unsupported entry '%s'.",
                relative_path
            );
            g_free (relative_path);
            closedir (directory);
            return FALSE;
        }

        int file_fd = openat (
            directory_fd,
            item->d_name,
            O_RDONLY | O_NOFOLLOW | O_CLOEXEC
        );

        if (file_fd < 0) {
            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Could not open snapshot file '%s': %s.",
                relative_path,
                g_strerror (errno)
            );
            g_free (relative_path);
            closedir (directory);
            return FALSE;
        }

        struct stat opened_st;
        if (fstat (file_fd, &opened_st) != 0 ||
            !S_ISREG (opened_st.st_mode) ||
            opened_st.st_dev != st.st_dev ||
            opened_st.st_ino != st.st_ino) {
            g_set_error (
                error,
                ATM_SNAPSHOT_SEAL_ERROR,
                ATM_SNAPSHOT_SEAL_ERROR_IO,
                "Snapshot file changed while sealing '%s'.",
                relative_path
            );
            close (file_fd);
            g_free (relative_path);
            closedir (directory);
            return FALSE;
        }

        AtmSealEntry *entry = g_new0 (
            AtmSealEntry,
            1
        );
        entry->path = relative_path;
        g_strlcpy (
            entry->mode,
            (opened_st.st_mode & 0111) != 0
                ? "100755"
                : "100644",
            sizeof entry->mode
        );

        if (!hash_open_file (
                file_fd,
                &entry->sha256,
                &entry->size,
                error
            )) {
            close (file_fd);
            seal_entry_free (entry);
            closedir (directory);
            return FALSE;
        }

        close (file_fd);
        *total_bytes += entry->size;
        g_ptr_array_add (entries, entry);
    }

    closedir (directory);
    return TRUE;
}

gboolean
atm_snapshot_seal_compute (
    const char *snapshot_root,
    char **out_sha256,
    guint64 *out_file_count,
    guint64 *out_total_bytes,
    GError **error
)
{
    int root_fd = -1;
    GPtrArray *entries = NULL;
    GChecksum *seal = NULL;
    guint64 total_bytes = 0;
    gboolean ok = FALSE;

    g_return_val_if_fail (snapshot_root != NULL, FALSE);
    g_return_val_if_fail (out_sha256 != NULL, FALSE);
    g_return_val_if_fail (*out_sha256 == NULL, FALSE);

    root_fd = open (
        snapshot_root,
        O_RDONLY | O_DIRECTORY |
            O_NOFOLLOW | O_CLOEXEC
    );

    if (root_fd < 0) {
        g_set_error (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not open snapshot root for sealing: %s.",
            g_strerror (errno)
        );
        goto out;
    }

    entries = g_ptr_array_new_with_free_func (
        (GDestroyNotify) seal_entry_free
    );

    if (!collect_directory (
            root_fd,
            "",
            entries,
            &total_bytes,
            error
        )) {
        goto out;
    }

    g_ptr_array_sort (entries, compare_entries);

    seal = g_checksum_new (G_CHECKSUM_SHA256);
    if (seal == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not initialize snapshot seal checksum."
        );
        goto out;
    }

    static const char header[] =
        "ATM-SNAPSHOT-SEAL-v1\0";
    g_checksum_update (
        seal,
        (const guchar *) header,
        sizeof header - 1
    );

    for (guint i = 0; i < entries->len; i++) {
        AtmSealEntry *entry =
            g_ptr_array_index (entries, i);
        char size_buffer[32];

        g_snprintf (
            size_buffer,
            sizeof size_buffer,
            "%" G_GUINT64_FORMAT,
            entry->size
        );

        g_checksum_update (
            seal,
            (const guchar *) entry->mode,
            strlen (entry->mode) + 1
        );
        g_checksum_update (
            seal,
            (const guchar *) entry->path,
            strlen (entry->path) + 1
        );
        g_checksum_update (
            seal,
            (const guchar *) size_buffer,
            strlen (size_buffer) + 1
        );
        g_checksum_update (
            seal,
            (const guchar *) entry->sha256,
            strlen (entry->sha256)
        );
        g_checksum_update (
            seal,
            (const guchar *) "\n",
            1
        );
    }

    *out_sha256 = g_strdup (
        g_checksum_get_string (seal)
    );
    if (*out_sha256 == NULL) {
        g_set_error_literal (
            error,
            ATM_SNAPSHOT_SEAL_ERROR,
            ATM_SNAPSHOT_SEAL_ERROR_IO,
            "Could not finalize snapshot seal."
        );
        goto out;
    }

    if (out_file_count != NULL) {
        *out_file_count = entries->len;
    }
    if (out_total_bytes != NULL) {
        *out_total_bytes = total_bytes;
    }

    ok = TRUE;

out:
    if (!ok) {
        g_clear_pointer (out_sha256, g_free);
    }
    g_clear_pointer (&seal, g_checksum_free);
    g_clear_pointer (&entries, g_ptr_array_unref);
    if (root_fd >= 0) {
        close (root_fd);
    }
    return ok;
}
