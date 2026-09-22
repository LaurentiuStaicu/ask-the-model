#pragma once

#include <glib.h>

#include "retrieval_query.h"
#include "scientific_profile.h"

G_BEGIN_DECLS

typedef enum {
    ATM_SCIENTIFIC_EVIDENCE_ERROR_ARGUMENT,
    ATM_SCIENTIFIC_EVIDENCE_ERROR_PROFILE
} AtmScientificEvidenceError;

#define ATM_SCIENTIFIC_EVIDENCE_ERROR \
    (atm_scientific_evidence_error_quark ())

typedef enum {
    ATM_SCIENTIFIC_EVIDENCE_UNCLASSIFIED,
    ATM_SCIENTIFIC_EVIDENCE_TEXTUAL_ONLY,
    ATM_SCIENTIFIC_EVIDENCE_UNSUPPORTED,
    ATM_SCIENTIFIC_EVIDENCE_INVALID,
    ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED
} AtmScientificEvidenceStatus;

typedef struct {
    AtmScientificEvidenceStatus status;
    char *reason_code;

    char *profile_id;
    char *profile_version;

    char *repository_id;
    char *repository_version;
    char *snapshot_sha;
    char *logical_source_id;
    char *source_path;
    char *locator;
    char *evidence_kind;

    char *semantic_type;
    char *entity_type;
    char *relation_type;
    char *native_id;
    char *from_logical_source_id;
    char *to_logical_source_id;
    char *dataset_logical_source_id;
    char *row_key;

    char *raw_payload;
} AtmScientificEvidenceAtom;

GQuark atm_scientific_evidence_error_quark (void);

gboolean atm_scientific_evidence_bridge_classify (
    const AtmScientificProfile *profile,
    const AtmEvidenceRecord *record,
    AtmScientificEvidenceAtom **out_atom,
    GError **error
);

void atm_scientific_evidence_atom_free (
    AtmScientificEvidenceAtom *atom
);

G_END_DECLS
