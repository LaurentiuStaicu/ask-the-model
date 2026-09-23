#pragma once

#include <glib.h>

#include "scientific_artifact.h"
#include "scientific_operation.h"

G_BEGIN_DECLS

#define ATM_SRA_SCHEMA_ID "atm-sra/1"

typedef enum {
    ATM_SRA_ERROR_ARGUMENT,
    ATM_SRA_ERROR_STATE,
    ATM_SRA_ERROR_SHAPE,
    ATM_SRA_ERROR_SUPPORT,
    ATM_SRA_ERROR_DERIVATION,
    ATM_SRA_ERROR_IDENTITY
} AtmSraError;

#define ATM_SRA_ERROR (atm_sra_error_quark ())

typedef enum {
    ATM_SRA_ANSWERED,
    ATM_SRA_PARTIAL,
    ATM_SRA_NOT_ANSWERABLE,
    ATM_SRA_BLOCKED
} AtmSraAnswerability;

typedef enum {
    ATM_SRA_CONSTRAINT_PASS,
    ATM_SRA_CONSTRAINT_DENY,
    ATM_SRA_CONSTRAINT_NOT_EVALUATED
} AtmSraConstraintStatus;

typedef struct {
    char *fact_id;
    char *fact_type;
    char *subject_id;
    char *attribute;
    char *value;
    char *unit;
    char *dimension;
    char *qualifiers_json;
    char *qualifiers_content_id;
    GPtrArray *support;
} AtmSraEstablishedFact;

typedef struct {
    char *role;
    char *fact_id;
} AtmSraOperationBinding;

typedef struct {
    char *fact_id;
    char *fact_type;
    char *subject_id;
    char *attribute;

    AtmScientificOperationOutcome outcome;
    char *value;
    char *unit;
    char *dimension;
    char *reason_code;
    gboolean has_binary64_bits;
    guint64 binary64_bits;

    char *operation;
    char *operation_numeric_profile;
    GPtrArray *input_bindings;
    GPtrArray *support;
} AtmSraDerivedFact;

typedef struct {
    char *constraint_id;
    AtmSraConstraintStatus status;
    GPtrArray *support;
    char *reason_code;
} AtmSraConstraintResult;

typedef struct {
    char *conflict_id;
    char *reason_code;
    GPtrArray *support;
} AtmSraConflict;

typedef struct {
    char *schema;
    GPtrArray *canonical_obligations;
    GPtrArray *repository_snapshots;
    GPtrArray *evidence_atom_ids;
    GPtrArray *control_evidence_ids;
    GPtrArray *semantic_profiles;
    GPtrArray *operations;
    char *numeric_profile;
    GPtrArray *temporal_dependencies;
    GPtrArray *stochastic_dependencies;
    char *scientific_content_id;
    char *qualified_artifact_id;
} AtmSraQualificationEnvelope;

typedef struct {
    AtmSraAnswerability answerability;
    GPtrArray *established_facts;
    GPtrArray *derived_facts;
    GPtrArray *constraint_results;
    GPtrArray *conflicts;
    GPtrArray *limitations;
    AtmSraQualificationEnvelope *qualification;
    gboolean finalized;
} AtmSraResult;

GQuark atm_sra_error_quark (void);

AtmSraResult *atm_sra_result_new (
    AtmSraAnswerability answerability
);

void atm_sra_result_free (
    AtmSraResult *result
);

AtmSraEstablishedFact *atm_sra_established_fact_new (
    const char *fact_id,
    const char *fact_type,
    const char *subject_id,
    const char *attribute,
    const char *value,
    const char *unit,
    const char *dimension,
    const char *qualifiers_json
);

void atm_sra_established_fact_free (
    AtmSraEstablishedFact *fact
);

gboolean atm_sra_fact_add_support (
    AtmSraEstablishedFact *fact,
    const char *qualified_artifact_id,
    GError **error
);

gboolean atm_sra_result_add_fact (
    AtmSraResult *result,
    AtmSraEstablishedFact *fact,
    GError **error
);

gboolean atm_sra_result_derive_fact (
    AtmSraResult *result,
    const char *qualified_operation,
    const AtmSraOperationBinding *bindings,
    gsize binding_count,
    const char *fact_id,
    const char *fact_type,
    const char *subject_id,
    const char *attribute,
    const char *dimension,
    GError **error
);

gboolean atm_sra_derived_fact_validate (
    const AtmSraResult *result,
    const AtmSraDerivedFact *derived,
    GError **error
);

void atm_sra_derived_fact_free (
    AtmSraDerivedFact *fact
);

AtmSraConstraintResult *atm_sra_constraint_new (
    const char *constraint_id,
    AtmSraConstraintStatus status,
    const char *reason_code
);

void atm_sra_constraint_free (
    AtmSraConstraintResult *constraint
);

gboolean atm_sra_constraint_add_support (
    AtmSraConstraintResult *constraint,
    const char *qualified_artifact_id,
    GError **error
);

gboolean atm_sra_result_add_constraint (
    AtmSraResult *result,
    AtmSraConstraintResult *constraint,
    GError **error
);

AtmSraConflict *atm_sra_conflict_new (
    const char *conflict_id,
    const char *reason_code
);

void atm_sra_conflict_free (
    AtmSraConflict *conflict
);

gboolean atm_sra_conflict_add_support (
    AtmSraConflict *conflict,
    const char *qualified_artifact_id,
    GError **error
);

gboolean atm_sra_result_add_conflict (
    AtmSraResult *result,
    AtmSraConflict *conflict,
    GError **error
);

gboolean atm_sra_result_add_limitation (
    AtmSraResult *result,
    const char *limitation,
    GError **error
);

gboolean atm_sra_qualification_add_canonical_obligation (
    AtmSraResult *result,
    const char *obligation,
    GError **error
);

gboolean atm_sra_qualification_add_repository_snapshot (
    AtmSraResult *result,
    const char *repository_snapshot,
    GError **error
);

gboolean atm_sra_qualification_add_evidence_artifact (
    AtmSraResult *result,
    const AtmScientificArtifact *artifact,
    GError **error
);

gboolean atm_sra_qualification_add_control_artifact (
    AtmSraResult *result,
    const AtmScientificArtifact *artifact,
    GError **error
);

gboolean atm_sra_qualification_add_semantic_profile (
    AtmSraResult *result,
    const char *semantic_profile,
    GError **error
);

gboolean atm_sra_qualification_add_operation (
    AtmSraResult *result,
    const char *operation,
    GError **error
);

gboolean atm_sra_qualification_set_numeric_profile (
    AtmSraResult *result,
    const char *numeric_profile,
    GError **error
);

gboolean atm_sra_result_finalize (
    AtmSraResult *result,
    GError **error
);

gboolean atm_sra_result_validate (
    const AtmSraResult *result,
    GError **error
);

G_END_DECLS