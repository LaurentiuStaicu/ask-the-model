#pragma once

#include <glib.h>

#include "scientific_canonical.h"
#include "scientific_control.h"
#include "scientific_evidence.h"

G_BEGIN_DECLS

#define ATM_SCIENTIFIC_ARTIFACT_SCHEMA_ID \
    "atm-scientific-artifact/v1"

typedef enum {
    ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_STATUS,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY
} AtmScientificArtifactError;

#define ATM_SCIENTIFIC_ARTIFACT_ERROR \
    (atm_scientific_artifact_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_ARTIFACT_KIND_EVIDENCE = 1,
    ATM_SCIENTIFIC_ARTIFACT_KIND_CONTROL = 2
} AtmScientificArtifactKind;

typedef enum {
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_JSON = 1,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN = 2,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_STRING = 3,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_NUMBER = 4
} AtmScientificArtifactPayloadType;

typedef struct {
    char *schema_id;
    AtmScientificArtifactKind kind;
    char *artifact_class;

    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *source_path;
    char *locator;
    char *logical_source_id;

    char *profile_id;
    char *profile_version;

    AtmScientificArtifactPayloadType payload_type;
    char *payload;

    char *scientific_content_id;
    char *qualified_artifact_id;
    char *storage_digest;
} AtmScientificArtifact;

GQuark atm_scientific_artifact_error_quark (void);

gboolean atm_scientific_artifact_from_evidence (
    const AtmScientificEvidenceAtom *evidence,
    AtmScientificArtifact **out_artifact,
    GError **error
);

gboolean atm_scientific_artifact_from_control (
    const AtmScientificControlValue *control,
    AtmScientificArtifact **out_artifact,
    GError **error
);

gboolean atm_scientific_artifact_validate (
    const AtmScientificArtifact *artifact,
    GError **error
);

void atm_scientific_artifact_free (
    AtmScientificArtifact *artifact
);

G_END_DECLS
