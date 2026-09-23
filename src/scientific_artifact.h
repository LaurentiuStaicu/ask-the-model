#pragma once

#include <glib.h>

#include "scientific_control.h"
#include "scientific_evidence.h"

G_BEGIN_DECLS

#define ATM_SCIENTIFIC_ARTIFACT_SCHEMA_VERSION 1u
#define ATM_SCIENTIFIC_ARTIFACT_PROFILE_ID \
    "atm-scientific-artifact/v1"
#define ATM_SCIENTIFIC_ARTIFACT_PROFILE_VERSION "1"

typedef enum {
    ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_SOURCE_STATUS,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_DIGEST,
    ATM_SCIENTIFIC_ARTIFACT_ERROR_INTEGRITY
} AtmScientificArtifactError;

#define ATM_SCIENTIFIC_ARTIFACT_ERROR \
    (atm_scientific_artifact_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_ARTIFACT_ORIGIN_UNCLASSIFIED,
    ATM_SCIENTIFIC_ARTIFACT_ORIGIN_TYPED_EVIDENCE,
    ATM_SCIENTIFIC_ARTIFACT_ORIGIN_CONTROL_EVIDENCE
} AtmScientificArtifactOrigin;

typedef enum {
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_UNCLASSIFIED,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_TEXT,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_STRING,
    ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_NUMBER
} AtmScientificArtifactPayloadType;

typedef struct {
    guint schema_version;
    char *artifact_profile_id;
    char *artifact_profile_version;
    char *artifact_id;

    AtmScientificArtifactOrigin origin;

    char *source_profile_id;
    char *source_profile_version;

    char *repository_id;
    char *repository_version;
    char *snapshot_sha;

    char *logical_source_id;
    char *source_path;
    char *source_locator_kind;
    char *source_locator;
    char *evidence_kind;

    char *semantic_type;
    char *entity_type;
    char *relation_type;
    char *native_id;
    char *from_logical_source_id;
    char *to_logical_source_id;
    char *dataset_logical_source_id;
    char *row_key;

    AtmScientificArtifactPayloadType payload_type;
    char *text_value;
    gboolean boolean_value;
    char *string_value;
    double number_value;
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

gboolean atm_scientific_artifact_recompute_id (
    const AtmScientificArtifact *artifact,
    char **out_artifact_id,
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
