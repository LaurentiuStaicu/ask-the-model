#pragma once

#include <glib.h>
#include "repository_sources.h"

G_BEGIN_DECLS

#define ATM_RETRIEVAL_INDEX_SCHEMA_VERSION 2

typedef enum {
    ATM_RETRIEVAL_INDEX_ERROR_INVALID_ID,
    ATM_RETRIEVAL_INDEX_ERROR_INVALID_SHA,
    ATM_RETRIEVAL_INDEX_ERROR_INVALID_MANIFEST_HASH,
    ATM_RETRIEVAL_INDEX_ERROR_EXISTS,
    ATM_RETRIEVAL_INDEX_ERROR_STAGING_EXISTS,
    ATM_RETRIEVAL_INDEX_ERROR_RESOURCE,
    ATM_RETRIEVAL_INDEX_ERROR_SQLITE,
    ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY,
    ATM_RETRIEVAL_INDEX_ERROR_IO,
    ATM_RETRIEVAL_INDEX_ERROR_NO_SPACE
} AtmRetrievalIndexError;

#define ATM_RETRIEVAL_INDEX_ERROR (atm_retrieval_index_error_quark ())

typedef struct {
    const char *repository_id;
    const char *repository_version;
    const char *snapshot_sha;
    gint manifest_schema_version;
    const char *manifest_sha256;
    const char *created_at_utc;
} AtmRetrievalIndexMetadata;

GQuark atm_retrieval_index_error_quark (void);

char *atm_retrieval_index_path (
    const char *cache_root,
    const char *repository_id,
    const char *sha
);

char *atm_retrieval_index_staging_path (
    const char *cache_root,
    const char *repository_id,
    const char *sha
);

gboolean atm_retrieval_index_create_empty (
    const char *cache_root,
    const AtmRetrievalIndexMetadata *metadata,
    char **out_index_path,
    GError **error
);

gboolean atm_retrieval_index_create_with_sources (
    const char *cache_root,
    const AtmRetrievalIndexMetadata *metadata,
    const AtmSourceCatalog *source_catalog,
    char **out_index_path,
    GError **error
);

gboolean atm_retrieval_index_create_with_documents (
    const char *cache_root,
    const char *snapshot_root,
    const AtmRetrievalIndexMetadata *metadata,
    const AtmSourceCatalog *source_catalog,
    char **out_index_path,
    GError **error
);

gboolean atm_retrieval_index_create_with_content (
    const char *cache_root,
    const char *snapshot_root,
    const AtmRetrievalIndexMetadata *metadata,
    const AtmSourceCatalog *source_catalog,
    char **out_index_path,
    GError **error
);

gboolean atm_retrieval_index_validate_identity (
    const char *index_path,
    const char *expected_repository_id,
    const char *expected_snapshot_sha,
    GError **error
);

gboolean atm_retrieval_index_validate_snapshot_sources (
    const char *index_path,
    const char *snapshot_root,
    const char *expected_repository_id,
    const char *expected_snapshot_sha,
    GError **error
);

G_END_DECLS
