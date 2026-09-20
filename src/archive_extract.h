#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_ARCHIVE_ERROR_OPEN,
    ATM_ARCHIVE_ERROR_FORMAT,
    ATM_ARCHIVE_ERROR_UNSAFE_PATH,
    ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY,
    ATM_ARCHIVE_ERROR_LIMIT,
    ATM_ARCHIVE_ERROR_IO,
    ATM_ARCHIVE_ERROR_PREFIX
} AtmArchiveError;

#define ATM_ARCHIVE_ERROR (atm_archive_error_quark ())

typedef struct {
    guint64 max_entries;
    guint64 max_file_bytes;
    guint64 max_total_bytes;
} AtmArchiveLimits;

GQuark atm_archive_error_quark (void);

gboolean atm_archive_extract_snapshot (
    const char *archive_path,
    const char *destination,
    const AtmArchiveLimits *limits,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
);

G_END_DECLS
