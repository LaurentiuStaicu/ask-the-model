#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ATM_INGEST_MAX_ENTRIES ((guint64) 10000)
#define ATM_INGEST_MAX_FILE_BYTES ((guint64) 32 * 1024 * 1024)
#define ATM_INGEST_MAX_TOTAL_BYTES ((guint64) 512 * 1024 * 1024)

typedef enum {
    ATM_INGEST_ERROR_INVALID_ARCHIVE,
    ATM_INGEST_ERROR_STAGING_EXISTS,
    ATM_INGEST_ERROR_IO
} AtmIngestError;

#define ATM_INGEST_ERROR (atm_ingest_error_quark ())

GQuark atm_ingest_error_quark (void);

gboolean atm_repository_ingest_archive_cancellable (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    GCancellable *cancellable,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
);

gboolean atm_repository_ingest_archive (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
);

void atm_repository_ingest_archive_async (
    const char *data_root,
    const char *archive_path,
    const char *repository_id,
    const char *repository_acronym,
    const char *repository_display_name,
    const char *sha,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data
);

gboolean atm_repository_ingest_archive_finish (
    GAsyncResult *result,
    char **out_version,
    char **out_snapshot_path,
    guint64 *out_entries,
    guint64 *out_total_bytes,
    GError **error
);

G_END_DECLS
