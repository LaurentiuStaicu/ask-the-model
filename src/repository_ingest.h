#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_INGEST_ERROR_INVALID_ARGUMENT,
    ATM_INGEST_ERROR_STAGING
} AtmIngestError;

#define ATM_INGEST_ERROR (atm_ingest_error_quark ())

#define ATM_REPOSITORY_MAX_ARCHIVE_ENTRIES ((guint64) 10000)
#define ATM_REPOSITORY_MAX_FILE_BYTES ((guint64) 32 * 1024 * 1024)
#define ATM_REPOSITORY_MAX_TOTAL_BYTES ((guint64) 512 * 1024 * 1024)

GQuark atm_ingest_error_quark (void);

gboolean atm_repository_ingest_archive (
    const char *archive_path,
    const char *data_root,
    const char *repository_id,
    const char *expected_acronym,
    const char *expected_display_name,
    const char *sha,
    char **out_version,
    char **out_snapshot_path,
    GError **error
);

G_END_DECLS
