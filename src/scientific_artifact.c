#include "scientific_artifact.h"

#include <math.h>
#include <string.h>

#define ATM_CONTROL_PROFILE_ID "atm-scientific-control/v1"
#define ATM_CONTROL_PROFILE_VERSION "1"

typedef struct {
    AtmScientificControlId control_id;
    const char *repository_id;
    const char *source_path;
    const char *json_pointer;
    AtmScientificControlValueType value_type;
    const char *semantic_type;
} AtmArtifactControlSpec;

static const AtmArtifactControlSpec CONTROL_SPECS[] = {
    {
        ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE,
        "rmd",
        "model/dynamics/core_contract.json",
        "/behavioural_closure/active",
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN,
        "rmd.behavioural_closure_active.control"
    }
};

GQuark
atm_scientific_artifact_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-artifact-error-quark"
    );
}

static gboolean
nonempty (const char *value)
{
    return value != NULL && value[0] != '\0';
}

static gboolean
valid_utf8_nonempty (const char *value)
{
    return nonempty (value) &&
        g_utf8_validate (value, -1, NULL);
}

static gboolean
valid_utf8_optional (const char *value)
{
    return value == NULL ||
        g_utf8_validate (value, -1, NULL);
}

static gboolean
lower_hex_sha_is_valid (const char *sha)
{
    if (sha == NULL || strlen (sha) != 40) {
        return FALSE;
    }

    for (guint i = 0; i < 40; i++) {
        if (!g_ascii_isxdigit (sha[i]) ||
            (sha[i] >= 'A' && sha[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
artifact_id_is_valid (const char *artifact_id)
{
    static const char prefix[] = "sha256:";
    const char *hex;

    if (artifact_id == NULL ||
        !g_str_has_prefix (artifact_id, prefix) ||
        strlen (artifact_id) !=
            strlen (prefix) + 64) {
        return FALSE;
    }

    hex = artifact_id + strlen (prefix);

    for (guint i = 0; i < 64; i++) {
        if (!g_ascii_isxdigit (hex[i]) ||
            (hex[i] >= 'A' && hex[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static const char *
origin_name (AtmScientificArtifactOrigin origin)
{
    switch (origin) {
        case ATM_SCIENTIFIC_ARTIFACT_ORIGIN_TYPED_EVIDENCE:
            return "typed_evidence";
        case ATM_SCIENTIFIC_ARTIFACT_ORIGIN_CONTROL_EVIDENCE:
            return "control_evidence";
        default:
            return NULL;
    }
}

static const char *
payload_type_name (
    AtmScientificArtifactPayloadType payload_type
)
{
    switch (payload_type) {
        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_TEXT:
            return "text";
        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN:
            return "boolean";
        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_STRING:
            return "string";
        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_NUMBER:
            return "number";
        default:
            return NULL;
    }
}

static const AtmArtifactControlSpec *
find_control_spec (AtmScientificControlId control_id)
{
    for (gsize i = 0;
         i < G_N_ELEMENTS (CONTROL_SPECS);
         i++) {
        if (CONTROL_SPECS[i].control_id == control_id) {
            return &CONTROL_SPECS[i];
        }
    }

    return NULL;
}

static const AtmArtifactControlSpec *
find_control_spec_by_semantic_type (
    const char *semantic_type
)
{
    for (gsize i = 0;
         i < G_N_ELEMENTS (CONTROL_SPECS);
         i++) {
        if (g_strcmp0 (
                CONTROL_SPECS[i].semantic_type,
                semantic_type
            ) == 0) {
            return &CONTROL_SPECS[i];
        }
    }

    return NULL;
}

static const char *
expected_repository_profile_id (
    const char *repository_id
)
{
    if (g_strcmp0 (repository_id, "ewd") == 0) {
        return "atm-profile/ewd/v1";
    }

    if (g_strcmp0 (repository_id, "cbd") == 0) {
        return "atm-profile/cbd/v1";
    }

    if (g_strcmp0 (repository_id, "rmd") == 0) {
        return "atm-profile/rmd/v1";
    }

    return NULL;
}

static gboolean
semantic_type_matches_repository (
    const char *repository_id,
    const char *semantic_type
)
{
    if (!nonempty (repository_id) ||
        !nonempty (semantic_type)) {
        return FALSE;
    }

    char *prefix = g_strdup_printf (
        "%s.",
        repository_id
    );
    gboolean matches = g_str_has_prefix (
        semantic_type,
        prefix
    );

    g_free (prefix);
    return matches;
}

void
atm_scientific_artifact_free (
    AtmScientificArtifact *artifact
)
{
    if (artifact == NULL) {
        return;
    }

    g_free (artifact->artifact_profile_id);
    g_free (artifact->artifact_profile_version);
    g_free (artifact->artifact_id);
    g_free (artifact->source_profile_id);
    g_free (artifact->source_profile_version);
    g_free (artifact->repository_id);
    g_free (artifact->repository_version);
    g_free (artifact->snapshot_sha);
    g_free (artifact->logical_source_id);
    g_free (artifact->source_path);
    g_free (artifact->source_locator_kind);
    g_free (artifact->source_locator);
    g_free (artifact->evidence_kind);
    g_free (artifact->semantic_type);
    g_free (artifact->entity_type);
    g_free (artifact->relation_type);
    g_free (artifact->native_id);
    g_free (artifact->from_logical_source_id);
    g_free (artifact->to_logical_source_id);
    g_free (artifact->dataset_logical_source_id);
    g_free (artifact->row_key);
    g_free (artifact->text_value);
    g_free (artifact->string_value);
    g_free (artifact);
}

static AtmScientificArtifact *
new_artifact (AtmScientificArtifactOrigin origin)
{
    AtmScientificArtifact *artifact = g_new0 (
        AtmScientificArtifact,
        1
    );

    artifact->schema_version =
        ATM_SCIENTIFIC_ARTIFACT_SCHEMA_VERSION;
    artifact->artifact_profile_id = g_strdup (
        ATM_SCIENTIFIC_ARTIFACT_PROFILE_ID
    );
    artifact->artifact_profile_version = g_strdup (
        ATM_SCIENTIFIC_ARTIFACT_PROFILE_VERSION
    );
    artifact->origin = origin;

    return artifact;
}

static gboolean
validate_optional_metadata (
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    const char *values[] = {
        artifact->repository_version,
        artifact->logical_source_id,
        artifact->entity_type,
        artifact->relation_type,
        artifact->native_id,
        artifact->from_logical_source_id,
        artifact->to_logical_source_id,
        artifact->dataset_logical_source_id,
        artifact->row_key
    };

    for (gsize i = 0; i < G_N_ELEMENTS (values); i++) {
        if (!valid_utf8_optional (values[i])) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
                "Scientific artifact contains invalid UTF-8 metadata."
            );
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
validate_fields (
    const AtmScientificArtifact *artifact,
    gboolean require_artifact_id,
    GError **error
)
{
    if (artifact == NULL ||
        artifact->schema_version !=
            ATM_SCIENTIFIC_ARTIFACT_SCHEMA_VERSION ||
        g_strcmp0 (
            artifact->artifact_profile_id,
            ATM_SCIENTIFIC_ARTIFACT_PROFILE_ID
        ) != 0 ||
        g_strcmp0 (
            artifact->artifact_profile_version,
            ATM_SCIENTIFIC_ARTIFACT_PROFILE_VERSION
        ) != 0 ||
        origin_name (artifact->origin) == NULL ||
        payload_type_name (artifact->payload_type) == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
            "Scientific artifact profile identity or enum state is invalid."
        );
        return FALSE;
    }

    if (require_artifact_id &&
        !artifact_id_is_valid (artifact->artifact_id)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_INTEGRITY,
            "Scientific artifact ID is absent or malformed."
        );
        return FALSE;
    }

    if (!valid_utf8_nonempty (
            artifact->source_profile_id
        ) ||
        !valid_utf8_nonempty (
            artifact->source_profile_version
        ) ||
        !valid_utf8_nonempty (
            artifact->repository_id
        ) ||
        !lower_hex_sha_is_valid (
            artifact->snapshot_sha
        ) ||
        !valid_utf8_nonempty (
            artifact->source_path
        ) ||
        !valid_utf8_nonempty (
            artifact->source_locator_kind
        ) ||
        !valid_utf8_nonempty (
            artifact->source_locator
        ) ||
        !valid_utf8_nonempty (
            artifact->evidence_kind
        ) ||
        !valid_utf8_nonempty (
            artifact->semantic_type
        ) ||
        !validate_optional_metadata (
            artifact,
            error
        )) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
                "Scientific artifact provenance is incomplete or invalid."
            );
        }
        return FALSE;
    }

    if (artifact->origin ==
        ATM_SCIENTIFIC_ARTIFACT_ORIGIN_TYPED_EVIDENCE) {
        const char *expected_profile =
            expected_repository_profile_id (
                artifact->repository_id
            );

        if (expected_profile == NULL ||
            g_strcmp0 (
                artifact->source_profile_id,
                expected_profile
            ) != 0 ||
            g_strcmp0 (
                artifact->source_profile_version,
                "1"
            ) != 0 ||
            !semantic_type_matches_repository (
                artifact->repository_id,
                artifact->semantic_type
            )) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_SOURCE_STATUS,
                "Typed-evidence artifact does not match an AtM production repository profile."
            );
            return FALSE;
        }

        if (!valid_utf8_nonempty (
                artifact->repository_version
            ) ||
            !valid_utf8_nonempty (
                artifact->logical_source_id
            ) ||
            g_strcmp0 (
                artifact->source_locator_kind,
                "retrieval_locator"
            ) != 0 ||
            artifact->payload_type !=
                ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_TEXT ||
            !valid_utf8_nonempty (
                artifact->text_value
            ) ||
            artifact->boolean_value ||
            artifact->string_value != NULL ||
            artifact->number_value != 0.0) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                "Typed-evidence artifact fields are inconsistent."
            );
            return FALSE;
        }
    } else if (
        artifact->origin ==
        ATM_SCIENTIFIC_ARTIFACT_ORIGIN_CONTROL_EVIDENCE
    ) {
        const AtmArtifactControlSpec *spec =
            find_control_spec_by_semantic_type (
                artifact->semantic_type
            );

        if (spec == NULL ||
            g_strcmp0 (
                artifact->source_profile_id,
                ATM_CONTROL_PROFILE_ID
            ) != 0 ||
            g_strcmp0 (
                artifact->source_profile_version,
                ATM_CONTROL_PROFILE_VERSION
            ) != 0 ||
            g_strcmp0 (
                artifact->repository_id,
                spec->repository_id
            ) != 0 ||
            g_strcmp0 (
                artifact->source_path,
                spec->source_path
            ) != 0 ||
            g_strcmp0 (
                artifact->source_locator,
                spec->json_pointer
            ) != 0 ||
            artifact->repository_version != NULL ||
            artifact->logical_source_id != NULL ||
            g_strcmp0 (
                artifact->source_locator_kind,
                "json_pointer"
            ) != 0 ||
            g_strcmp0 (
                artifact->evidence_kind,
                "control"
            ) != 0 ||
            artifact->entity_type != NULL ||
            artifact->relation_type != NULL ||
            artifact->native_id != NULL ||
            artifact->from_logical_source_id != NULL ||
            artifact->to_logical_source_id != NULL ||
            artifact->dataset_logical_source_id != NULL ||
            artifact->row_key != NULL ||
            artifact->text_value != NULL) {
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
                "Control-evidence artifact fields are inconsistent with the AtM control specification."
            );
            return FALSE;
        }

        switch (artifact->payload_type) {
            case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN:
                if (spec->value_type !=
                        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN ||
                    artifact->string_value != NULL ||
                    artifact->number_value != 0.0) {
                    g_set_error_literal (
                        error,
                        ATM_SCIENTIFIC_ARTIFACT_ERROR,
                        ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                        "Boolean control artifact contains non-canonical payload state."
                    );
                    return FALSE;
                }
                break;

            case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_STRING:
                if (spec->value_type !=
                        ATM_SCIENTIFIC_CONTROL_VALUE_STRING ||
                    artifact->boolean_value ||
                    artifact->number_value != 0.0 ||
                    artifact->string_value == NULL ||
                    !g_utf8_validate (
                        artifact->string_value,
                        -1,
                        NULL
                    )) {
                    g_set_error_literal (
                        error,
                        ATM_SCIENTIFIC_ARTIFACT_ERROR,
                        ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                        "String control artifact payload is invalid."
                    );
                    return FALSE;
                }
                break;

            case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_NUMBER:
                if (spec->value_type !=
                        ATM_SCIENTIFIC_CONTROL_VALUE_NUMBER ||
                    artifact->boolean_value ||
                    artifact->string_value != NULL ||
                    !isfinite (artifact->number_value)) {
                    g_set_error_literal (
                        error,
                        ATM_SCIENTIFIC_ARTIFACT_ERROR,
                        ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                        "Numeric control artifact payload is invalid."
                    );
                    return FALSE;
                }
                break;

            default:
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                    "Control artifact uses an unsupported payload type."
                );
                return FALSE;
        }
    }

    return TRUE;
}

static void
checksum_u64 (
    GChecksum *checksum,
    guint64 value
)
{
    guint64 encoded = GUINT64_TO_BE (value);

    g_checksum_update (
        checksum,
        (const guchar *) &encoded,
        sizeof encoded
    );
}

static void
checksum_field (
    GChecksum *checksum,
    const char *name,
    const guint8 *data,
    gsize length,
    gboolean present
)
{
    guint8 presence = present ? 1 : 0;
    gsize name_length = strlen (name);

    checksum_u64 (
        checksum,
        (guint64) name_length
    );
    g_checksum_update (
        checksum,
        (const guchar *) name,
        name_length
    );
    g_checksum_update (
        checksum,
        &presence,
        1
    );

    if (!present) {
        return;
    }

    checksum_u64 (
        checksum,
        (guint64) length
    );

    if (length > 0) {
        g_checksum_update (
            checksum,
            data,
            length
        );
    }
}

static void
checksum_string (
    GChecksum *checksum,
    const char *name,
    const char *value
)
{
    checksum_field (
        checksum,
        name,
        (const guint8 *) value,
        value != NULL ? strlen (value) : 0,
        value != NULL
    );
}

static gboolean
append_payload_digest (
    GChecksum *checksum,
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    switch (artifact->payload_type) {
        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_TEXT:
            checksum_string (
                checksum,
                "payload_value",
                artifact->text_value
            );
            return TRUE;

        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN:
            checksum_string (
                checksum,
                "payload_value",
                artifact->boolean_value
                    ? "true"
                    : "false"
            );
            return TRUE;

        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_STRING:
            checksum_string (
                checksum,
                "payload_value",
                artifact->string_value
            );
            return TRUE;

        case ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_NUMBER: {
            char buffer[G_ASCII_DTOSTR_BUF_SIZE];

            if (!isfinite (artifact->number_value)) {
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                    "Cannot canonicalize a non-finite numeric payload."
                );
                return FALSE;
            }

            g_ascii_dtostr (
                buffer,
                sizeof buffer,
                artifact->number_value
            );
            checksum_string (
                checksum,
                "payload_value",
                buffer
            );
            return TRUE;
        }

        default:
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                "Cannot canonicalize an unsupported payload type."
            );
            return FALSE;
    }
}

gboolean
atm_scientific_artifact_recompute_id (
    const AtmScientificArtifact *artifact,
    char **out_artifact_id,
    GError **error
)
{
    static const char domain[] =
        "ATM-SCIENTIFIC-ARTIFACT-DIGEST-v1";
    char schema_buffer[16];
    GChecksum *checksum = NULL;
    char *digest = NULL;

    if (artifact == NULL ||
        out_artifact_id == NULL ||
        *out_artifact_id != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
            "Scientific artifact digest arguments are invalid."
        );
        return FALSE;
    }

    if (!validate_fields (
            artifact,
            FALSE,
            error
        )) {
        return FALSE;
    }

    checksum = g_checksum_new (
        G_CHECKSUM_SHA256
    );

    if (checksum == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_DIGEST,
            "SHA-256 support is unavailable."
        );
        return FALSE;
    }

    g_checksum_update (
        checksum,
        (const guchar *) domain,
        sizeof domain - 1
    );

    g_snprintf (
        schema_buffer,
        sizeof schema_buffer,
        "%u",
        artifact->schema_version
    );

    checksum_string (
        checksum,
        "schema_version",
        schema_buffer
    );
    checksum_string (
        checksum,
        "artifact_profile_id",
        artifact->artifact_profile_id
    );
    checksum_string (
        checksum,
        "artifact_profile_version",
        artifact->artifact_profile_version
    );
    checksum_string (
        checksum,
        "origin",
        origin_name (artifact->origin)
    );
    checksum_string (
        checksum,
        "source_profile_id",
        artifact->source_profile_id
    );
    checksum_string (
        checksum,
        "source_profile_version",
        artifact->source_profile_version
    );
    checksum_string (
        checksum,
        "repository_id",
        artifact->repository_id
    );
    checksum_string (
        checksum,
        "repository_version",
        artifact->repository_version
    );
    checksum_string (
        checksum,
        "snapshot_sha",
        artifact->snapshot_sha
    );
    checksum_string (
        checksum,
        "logical_source_id",
        artifact->logical_source_id
    );
    checksum_string (
        checksum,
        "source_path",
        artifact->source_path
    );
    checksum_string (
        checksum,
        "source_locator_kind",
        artifact->source_locator_kind
    );
    checksum_string (
        checksum,
        "source_locator",
        artifact->source_locator
    );
    checksum_string (
        checksum,
        "evidence_kind",
        artifact->evidence_kind
    );
    checksum_string (
        checksum,
        "semantic_type",
        artifact->semantic_type
    );
    checksum_string (
        checksum,
        "entity_type",
        artifact->entity_type
    );
    checksum_string (
        checksum,
        "relation_type",
        artifact->relation_type
    );
    checksum_string (
        checksum,
        "native_id",
        artifact->native_id
    );
    checksum_string (
        checksum,
        "from_logical_source_id",
        artifact->from_logical_source_id
    );
    checksum_string (
        checksum,
        "to_logical_source_id",
        artifact->to_logical_source_id
    );
    checksum_string (
        checksum,
        "dataset_logical_source_id",
        artifact->dataset_logical_source_id
    );
    checksum_string (
        checksum,
        "row_key",
        artifact->row_key
    );
    checksum_string (
        checksum,
        "payload_type",
        payload_type_name (
            artifact->payload_type
        )
    );

    if (!append_payload_digest (
            checksum,
            artifact,
            error
        )) {
        g_checksum_free (checksum);
        return FALSE;
    }

    digest = g_strdup (
        g_checksum_get_string (checksum)
    );
    g_checksum_free (checksum);

    if (digest == NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_DIGEST,
            "Could not finalize scientific artifact SHA-256."
        );
        return FALSE;
    }

    *out_artifact_id = g_strdup_printf (
        "sha256:%s",
        digest
    );
    g_free (digest);

    return *out_artifact_id != NULL;
}

static gboolean
finalize_artifact (
    AtmScientificArtifact *artifact,
    AtmScientificArtifact **out_artifact,
    GError **error
)
{
    if (!atm_scientific_artifact_recompute_id (
            artifact,
            &artifact->artifact_id,
            error
        ) ||
        !atm_scientific_artifact_validate (
            artifact,
            error
        )) {
        atm_scientific_artifact_free (artifact);
        return FALSE;
    }

    *out_artifact = artifact;
    return TRUE;
}

gboolean
atm_scientific_artifact_from_evidence (
    const AtmScientificEvidenceAtom *evidence,
    AtmScientificArtifact **out_artifact,
    GError **error
)
{
    if (evidence == NULL ||
        out_artifact == NULL ||
        *out_artifact != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
            "Scientific evidence artifact arguments are invalid."
        );
        return FALSE;
    }

    if (evidence->status !=
            ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED ||
        g_strcmp0 (
            evidence->reason_code,
            "typed_validated"
        ) != 0) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_SOURCE_STATUS,
            "Only TYPED_VALIDATED evidence may become a canonical scientific artifact."
        );
        return FALSE;
    }

    if (!valid_utf8_nonempty (evidence->profile_id) ||
        !valid_utf8_nonempty (
            evidence->profile_version
        ) ||
        !valid_utf8_nonempty (
            evidence->repository_id
        ) ||
        !valid_utf8_nonempty (
            evidence->repository_version
        ) ||
        !lower_hex_sha_is_valid (
            evidence->snapshot_sha
        ) ||
        !valid_utf8_nonempty (
            evidence->logical_source_id
        ) ||
        !valid_utf8_nonempty (
            evidence->source_path
        ) ||
        !valid_utf8_nonempty (
            evidence->locator
        ) ||
        !valid_utf8_nonempty (
            evidence->evidence_kind
        ) ||
        !valid_utf8_nonempty (
            evidence->semantic_type
        ) ||
        !valid_utf8_nonempty (
            evidence->raw_payload
        )) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
            "Typed evidence lacks canonical artifact provenance or payload."
        );
        return FALSE;
    }

    AtmScientificArtifact *artifact = new_artifact (
        ATM_SCIENTIFIC_ARTIFACT_ORIGIN_TYPED_EVIDENCE
    );

    artifact->source_profile_id =
        g_strdup (evidence->profile_id);
    artifact->source_profile_version =
        g_strdup (evidence->profile_version);
    artifact->repository_id =
        g_strdup (evidence->repository_id);
    artifact->repository_version =
        g_strdup (evidence->repository_version);
    artifact->snapshot_sha =
        g_strdup (evidence->snapshot_sha);
    artifact->logical_source_id =
        g_strdup (evidence->logical_source_id);
    artifact->source_path =
        g_strdup (evidence->source_path);
    artifact->source_locator_kind =
        g_strdup ("retrieval_locator");
    artifact->source_locator =
        g_strdup (evidence->locator);
    artifact->evidence_kind =
        g_strdup (evidence->evidence_kind);
    artifact->semantic_type =
        g_strdup (evidence->semantic_type);
    artifact->entity_type =
        g_strdup (evidence->entity_type);
    artifact->relation_type =
        g_strdup (evidence->relation_type);
    artifact->native_id =
        g_strdup (evidence->native_id);
    artifact->from_logical_source_id =
        g_strdup (
            evidence->from_logical_source_id
        );
    artifact->to_logical_source_id =
        g_strdup (
            evidence->to_logical_source_id
        );
    artifact->dataset_logical_source_id =
        g_strdup (
            evidence->dataset_logical_source_id
        );
    artifact->row_key =
        g_strdup (evidence->row_key);
    artifact->payload_type =
        ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_TEXT;
    artifact->text_value =
        g_strdup (evidence->raw_payload);

    return finalize_artifact (
        artifact,
        out_artifact,
        error
    );
}

gboolean
atm_scientific_artifact_from_control (
    const AtmScientificControlValue *control,
    AtmScientificArtifact **out_artifact,
    GError **error
)
{
    if (control == NULL ||
        out_artifact == NULL ||
        *out_artifact != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
            "Scientific control artifact arguments are invalid."
        );
        return FALSE;
    }

    const AtmArtifactControlSpec *spec =
        find_control_spec (control->control_id);

    if (spec == NULL ||
        g_strcmp0 (
            control->repository_id,
            spec->repository_id
        ) != 0 ||
        g_strcmp0 (
            control->source_path,
            spec->source_path
        ) != 0 ||
        g_strcmp0 (
            control->json_pointer,
            spec->json_pointer
        ) != 0 ||
        control->value_type != spec->value_type) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
            "Scientific control value does not match an AtM-owned canonical control specification."
        );
        return FALSE;
    }

    if (!lower_hex_sha_is_valid (
            control->snapshot_sha
        )) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
            "Scientific control value has a non-canonical snapshot SHA."
        );
        return FALSE;
    }

    AtmScientificArtifact *artifact = new_artifact (
        ATM_SCIENTIFIC_ARTIFACT_ORIGIN_CONTROL_EVIDENCE
    );

    artifact->source_profile_id = g_strdup (
        ATM_CONTROL_PROFILE_ID
    );
    artifact->source_profile_version = g_strdup (
        ATM_CONTROL_PROFILE_VERSION
    );
    artifact->repository_id = g_strdup (
        control->repository_id
    );
    artifact->snapshot_sha = g_strdup (
        control->snapshot_sha
    );
    artifact->source_path = g_strdup (
        control->source_path
    );
    artifact->source_locator_kind = g_strdup (
        "json_pointer"
    );
    artifact->source_locator = g_strdup (
        control->json_pointer
    );
    artifact->evidence_kind = g_strdup (
        "control"
    );
    artifact->semantic_type = g_strdup (
        spec->semantic_type
    );

    switch (control->value_type) {
        case ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN:
            artifact->payload_type =
                ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN;
            artifact->boolean_value =
                control->boolean_value;
            break;

        case ATM_SCIENTIFIC_CONTROL_VALUE_STRING:
            if (control->string_value == NULL ||
                !g_utf8_validate (
                    control->string_value,
                    -1,
                    NULL
                )) {
                atm_scientific_artifact_free (
                    artifact
                );
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                    "Scientific control string payload is invalid."
                );
                return FALSE;
            }

            artifact->payload_type =
                ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_STRING;
            artifact->string_value = g_strdup (
                control->string_value
            );
            break;

        case ATM_SCIENTIFIC_CONTROL_VALUE_NUMBER:
            if (!isfinite (control->number_value)) {
                atm_scientific_artifact_free (
                    artifact
                );
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                    "Scientific control numeric payload is not finite."
                );
                return FALSE;
            }

            artifact->payload_type =
                ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_NUMBER;
            artifact->number_value =
                control->number_value;
            break;

        default:
            atm_scientific_artifact_free (
                artifact
            );
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
                "Scientific control payload type is unsupported."
            );
            return FALSE;
    }

    return finalize_artifact (
        artifact,
        out_artifact,
        error
    );
}

gboolean
atm_scientific_artifact_validate (
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    char *recomputed = NULL;

    if (!validate_fields (
            artifact,
            TRUE,
            error
        )) {
        return FALSE;
    }

    if (!atm_scientific_artifact_recompute_id (
            artifact,
            &recomputed,
            error
        )) {
        return FALSE;
    }

    gboolean matches =
        g_strcmp0 (
            artifact->artifact_id,
            recomputed
        ) == 0;

    g_free (recomputed);

    if (!matches) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_INTEGRITY,
            "Scientific artifact ID no longer matches its canonical fields."
        );
        return FALSE;
    }

    return TRUE;
}
