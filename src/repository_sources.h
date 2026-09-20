#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ATM_SOURCE_ROLE_CANONICAL = 1u << 0,
    ATM_SOURCE_ROLE_STRUCTURAL = 1u << 1,
    ATM_SOURCE_ROLE_EVIDENCE = 1u << 2,
    ATM_SOURCE_ROLE_TABULAR = 1u << 3,
    ATM_SOURCE_ROLE_IMPLEMENTATION = 1u << 4
} AtmSourceRole;

typedef enum {
    ATM_SOURCE_CATALOG_ERROR_MANIFEST,
    ATM_SOURCE_CATALOG_ERROR_IDENTITY,
    ATM_SOURCE_CATALOG_ERROR_UNSAFE_ENTRY,
    ATM_SOURCE_CATALOG_ERROR_IO,
    ATM_SOURCE_CATALOG_ERROR_CHECKSUM
} AtmSourceCatalogError;

#define ATM_SOURCE_CATALOG_ERROR (atm_source_catalog_error_quark ())

typedef struct {
    char *path;
    char *sha256;
    guint64 byte_size;
    char *media_type;
    guint roles;
} AtmSourceRecord;

typedef struct {
    char *manifest_sha256;
    GPtrArray *files;
} AtmSourceCatalog;

GQuark atm_source_catalog_error_quark (void);

gboolean atm_repository_source_catalog_build (
    const char *snapshot_root,
    const char *expected_repository_id,
    AtmSourceCatalog **out_catalog,
    GError **error
);

void atm_source_catalog_free (AtmSourceCatalog *catalog);

const char *atm_source_role_name (AtmSourceRole role);

G_END_DECLS
