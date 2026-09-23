#include "scientific_artifact.h"

#include <string.h>

static const guint8 STORAGE_DOMAIN[] =
    "ATM-STORAGE-DIGEST-v1\0";

GQuark
atm_scientific_artifact_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-scientific-artifact-error-quark"
    );
}

static gboolean
nonempty_utf8 (const char *value)
{
    return value != NULL &&
        value[0] != '\0' &&
        g_utf8_validate (value, -1, NULL);
}

static gboolean
optional_utf8 (const char *value)
{
    return value == NULL ||
        g_utf8_validate (value, -1, NULL);
}

static gboolean
lower_hex_is_valid (
    const char *value,
    gsize length
)
{
    if (value == NULL ||
        strlen (value) != length) {
        return FALSE;
    }

    for (gsize i = 0; i < length; i++) {
        if (!g_ascii_isxdigit (value[i]) ||
            (value[i] >= 'A' && value[i] <= 'F')) {
            return FALSE;
        }
    }

    return TRUE;
}

static void
checksum_u32 (
    GChecksum *checksum,
    guint32 value
)
{
    guint32 encoded = GUINT32_TO_BE (value);

    g_checksum_update (
        checksum,
        (const guchar *) &encoded,
        sizeof encoded
    );
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
checksum_string (
    GChecksum *checksum,
    const char *value
)
{
    if (value == NULL) {
        checksum_u64 (checksum, G_MAXUINT64);
        return;
    }

    gsize length = strlen (value);

    checksum_u64 (
        checksum,
        (guint64) length
    );
    g_checksum_update (
        checksum,
        (const guchar *) value,
        length
    );
}

static char *
compute_storage_digest (
    const AtmScientificArtifact *artifact
)
{
    GChecksum *checksum = g_checksum_new (
        G_CHECKSUM_SHA256
    );

    if (checksum == NULL) {
        return NULL;
    }

    g_checksum_update (
        checksum,
        STORAGE_DOMAIN,
        sizeof STORAGE_DOMAIN - 1
    );

    checksum_string (
        checksum,
        artifact->schema_id
    );
    checksum_u32 (
        checksum,
        (guint32) artifact->kind
    );
    checksum_string (
        checksum,
        artifact->artifact_class
    );
    checksum_string (
        checksum,
        artifact->repository_id
    );
    checksum_string (
        checksum,
        artifact->repository_version
    );
    checksum_string (
        checksum,
        artifact->snapshot_sha
    );
    checksum_string (
        checksum,
        artifact->source_path
    );
    checksum_string (
        checksum,
        artifact->locator
    );
    checksum_string (
        checksum,
        artifact->logical_source_id
    );
    checksum_string (
        checksum,
        artifact->profile_id
    );
    checksum_string (
        checksum,
        artifact->profile_version
    );
    checksum_u32 (
        checksum,
        (guint32) artifact->payload_type
    );
    checksum_string (
        checksum,
        artifact->payload
    );
    checksum_string (
        checksum,
        artifact->scientific_content_id
    );
    checksum_string (
        checksum,
        artifact->qualified_artifact_id
    );

    char *digest = g_strdup (
        g_checksum_get_string (checksum)
    );

    g_checksum_free (checksum);
    return digest;
}

void
atm_scientific_artifact_free (
    AtmScientificArtifact *artifact
)
{
    if (artifact == NULL) {
        return;
    }

    g_free (artifact->schema_id);
    g_free (artifact->artifact_class);
    g_free (artifact->repository_id);
    g_free (artifact->repository_version);
    g_free (artifact->snapshot_sha);
    g_free (artifact->source_path);
    g_free (artifact->locator);
    g_free (artifact->logical_source_id);
    g_free (artifact->profile_id);
    g_free (artifact->profile_version);
    g_free (artifact->payload);
    g_free (artifact->scientific_content_id);
    g_free (artifact->qualified_artifact_id);
    g_free (artifact->storage_digest);
    g_free (artifact);
}

static gboolean
base_provenance_is_valid (
    const char *repository_id,
    const char *snapshot_sha,
    const char *source_path,
    const char *locator
)
{
    return
        nonempty_utf8 (repository_id) &&
        lower_hex_is_valid (snapshot_sha, 40) &&
        nonempty_utf8 (source_path) &&
        nonempty_utf8 (locator);
}

static AtmScientificArtifact *
new_artifact (
    AtmScientificArtifactKind kind,
    const char *artifact_class,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *source_path,
    const char *locator,
    const char *logical_source_id,
    const char *profile_id,
    const char *profile_version,
    AtmScientificArtifactPayloadType payload_type,
    const char *payload
)
{
    AtmScientificArtifact *artifact = g_new0 (
        AtmScientificArtifact,
        1
    );

    artifact->schema_id = g_strdup (
        ATM_SCIENTIFIC_ARTIFACT_SCHEMA_ID
    );
    artifact->kind = kind;
    artifact->artifact_class = g_strdup (
        artifact_class
    );
    artifact->repository_id = g_strdup (
        repository_id
    );
    artifact->repository_version = g_strdup (
        repository_version
    );
    artifact->snapshot_sha = g_strdup (
        snapshot_sha
    );
    artifact->source_path = g_strdup (
        source_path
    );
    artifact->locator = g_strdup (
        locator
    );
    artifact->logical_source_id = g_strdup (
        logical_source_id
    );
    artifact->profile_id = g_strdup (
        profile_id
    );
    artifact->profile_version = g_strdup (
        profile_version
    );
    artifact->payload_type = payload_type;
    artifact->payload = g_strdup (payload);

    return artifact;
}

static gboolean
assign_evidence_identities (
    AtmScientificArtifact *artifact,
    GError **error
)
{
    if (!atm_scientific_content_id_json (
            artifact->artifact_class,
            artifact->payload,
            &artifact->scientific_content_id,
            error
        )) {
        return FALSE;
    }

    if (!atm_scientific_qualified_artifact_id (
            artifact->scientific_content_id,
            artifact->repository_id,
            artifact->repository_version,
            artifact->snapshot_sha,
            artifact->source_path,
            artifact->locator,
            artifact->logical_source_id,
            artifact->profile_id,
            artifact->profile_version,
            &artifact->qualified_artifact_id,
            error
        )) {
        return FALSE;
    }

    artifact->storage_digest =
        compute_storage_digest (artifact);

    return artifact->storage_digest != NULL;
}

static gboolean
assign_control_identities (
    AtmScientificArtifact *artifact,
    const char *scalar_type,
    GError **error
)
{
    if (!atm_scientific_content_id_scalar (
            artifact->artifact_class,
            scalar_type,
            artifact->payload,
            &artifact->scientific_content_id,
            error
        )) {
        return FALSE;
    }

    if (!atm_scientific_qualified_artifact_id (
            artifact->scientific_content_id,
            artifact->repository_id,
            NULL,
            artifact->snapshot_sha,
            artifact->source_path,
            artifact->locator,
            NULL,
            NULL,
            NULL,
            &artifact->qualified_artifact_id,
            error
        )) {
        return FALSE;
    }

    artifact->storage_digest =
        compute_storage_digest (artifact);

    return artifact->storage_digest != NULL;
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
            "Scientific artifact evidence factory received invalid arguments."
        );
        return FALSE;
    }

    if (evidence->status !=
        ATM_SCIENTIFIC_EVIDENCE_TYPED_VALIDATED) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_STATUS,
            "Only TYPED_VALIDATED evidence may become a canonical scientific artifact."
        );
        return FALSE;
    }

    if (!base_provenance_is_valid (
            evidence->repository_id,
            evidence->snapshot_sha,
            evidence->source_path,
            evidence->locator
        ) ||
        !nonempty_utf8 (
            evidence->repository_version
        ) ||
        !nonempty_utf8 (
            evidence->logical_source_id
        ) ||
        !nonempty_utf8 (evidence->profile_id) ||
        !nonempty_utf8 (
            evidence->profile_version
        ) ||
        !nonempty_utf8 (
            evidence->semantic_type
        )) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
            "Typed scientific evidence has incomplete canonical provenance."
        );
        return FALSE;
    }

    if (!nonempty_utf8 (evidence->raw_payload)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_PAYLOAD,
            "Typed scientific evidence has no valid UTF-8 payload."
        );
        return FALSE;
    }

    AtmScientificArtifact *artifact = new_artifact (
        ATM_SCIENTIFIC_ARTIFACT_KIND_EVIDENCE,
        evidence->semantic_type,
        evidence->repository_id,
        evidence->repository_version,
        evidence->snapshot_sha,
        evidence->source_path,
        evidence->locator,
        evidence->logical_source_id,
        evidence->profile_id,
        evidence->profile_version,
        ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_JSON,
        evidence->raw_payload
    );

    GError *identity_error = NULL;

    if (!assign_evidence_identities (
            artifact,
            &identity_error
        )) {
        atm_scientific_artifact_free (artifact);
        g_set_error (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY,
            "Could not create canonical scientific evidence identity: %s.",
            identity_error != NULL
                ? identity_error->message
                : "unknown canonicalization error"
        );
        g_clear_error (&identity_error);
        return FALSE;
    }

    *out_artifact = artifact;
    return TRUE;
}

static gboolean
control_contract (
    const AtmScientificControlValue *control,
    const char **out_artifact_class,
    AtmScientificArtifactPayloadType *out_payload_type,
    const char **out_scalar_type,
    char **out_payload,
    GError **error
)
{
    switch (control->control_id) {
        case ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE:
            if (g_strcmp0 (
                    control->repository_id,
                    "rmd"
                ) != 0 ||
                g_strcmp0 (
                    control->source_path,
                    "model/dynamics/core_contract.json"
                ) != 0 ||
                g_strcmp0 (
                    control->json_pointer,
                    "/behavioural_closure/active"
                ) != 0 ||
                control->value_type !=
                    ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN) {
                g_set_error_literal (
                    error,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR,
                    ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
                    "Scientific control value does not match its AtM-owned control contract."
                );
                return FALSE;
            }

            *out_artifact_class =
                "rmd.behavioural_closure_active_control";
            *out_payload_type =
                ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN;
            *out_scalar_type = "boolean";
            *out_payload = g_strdup (
                control->boolean_value
                    ? "true"
                    : "false"
            );
            return TRUE;

        default:
            g_set_error_literal (
                error,
                ATM_SCIENTIFIC_ARTIFACT_ERROR,
                ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
                "Scientific control identifier is not supported by the canonical artifact profile."
            );
            return FALSE;
    }
}

gboolean
atm_scientific_artifact_from_control (
    const AtmScientificControlValue *control,
    AtmScientificArtifact **out_artifact,
    GError **error
)
{
    const char *artifact_class = NULL;
    const char *scalar_type = NULL;
    AtmScientificArtifactPayloadType payload_type;
    char *payload = NULL;

    if (control == NULL ||
        out_artifact == NULL ||
        *out_artifact != NULL) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_ARGUMENT,
            "Scientific artifact control factory received invalid arguments."
        );
        return FALSE;
    }

    if (!base_provenance_is_valid (
            control->repository_id,
            control->snapshot_sha,
            control->source_path,
            control->json_pointer
        )) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
            "Scientific control evidence has incomplete canonical provenance."
        );
        return FALSE;
    }

    if (!control_contract (
            control,
            &artifact_class,
            &payload_type,
            &scalar_type,
            &payload,
            error
        )) {
        return FALSE;
    }

    AtmScientificArtifact *artifact = new_artifact (
        ATM_SCIENTIFIC_ARTIFACT_KIND_CONTROL,
        artifact_class,
        control->repository_id,
        NULL,
        control->snapshot_sha,
        control->source_path,
        control->json_pointer,
        NULL,
        NULL,
        NULL,
        payload_type,
        payload
    );
    g_free (payload);

    GError *identity_error = NULL;

    if (!assign_control_identities (
            artifact,
            scalar_type,
            &identity_error
        )) {
        atm_scientific_artifact_free (artifact);
        g_set_error (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY,
            "Could not create canonical scientific control identity: %s.",
            identity_error != NULL
                ? identity_error->message
                : "unknown canonicalization error"
        );
        g_clear_error (&identity_error);
        return FALSE;
    }

    *out_artifact = artifact;
    return TRUE;
}

static gboolean
shape_is_valid (
    const AtmScientificArtifact *artifact
)
{
    if (artifact == NULL ||
        g_strcmp0 (
            artifact->schema_id,
            ATM_SCIENTIFIC_ARTIFACT_SCHEMA_ID
        ) != 0 ||
        !nonempty_utf8 (artifact->artifact_class) ||
        !base_provenance_is_valid (
            artifact->repository_id,
            artifact->snapshot_sha,
            artifact->source_path,
            artifact->locator
        ) ||
        !nonempty_utf8 (artifact->payload) ||
        !optional_utf8 (
            artifact->repository_version
        ) ||
        !optional_utf8 (
            artifact->logical_source_id
        ) ||
        !optional_utf8 (artifact->profile_id) ||
        !optional_utf8 (
            artifact->profile_version
        ) ||
        !lower_hex_is_valid (
            artifact->scientific_content_id,
            64
        ) ||
        !lower_hex_is_valid (
            artifact->qualified_artifact_id,
            64
        ) ||
        !lower_hex_is_valid (
            artifact->storage_digest,
            64
        )) {
        return FALSE;
    }

    if (artifact->kind ==
        ATM_SCIENTIFIC_ARTIFACT_KIND_EVIDENCE) {
        return
            artifact->payload_type ==
                ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_JSON &&
            nonempty_utf8 (
                artifact->repository_version
            ) &&
            nonempty_utf8 (
                artifact->logical_source_id
            ) &&
            nonempty_utf8 (
                artifact->profile_id
            ) &&
            nonempty_utf8 (
                artifact->profile_version
            );
    }

    if (artifact->kind ==
        ATM_SCIENTIFIC_ARTIFACT_KIND_CONTROL) {
        return
            artifact->repository_version == NULL &&
            artifact->logical_source_id == NULL &&
            artifact->profile_id == NULL &&
            artifact->profile_version == NULL;
    }

    return FALSE;
}

static gboolean
validate_control_shape (
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    if (g_strcmp0 (
            artifact->artifact_class,
            "rmd.behavioural_closure_active_control"
        ) != 0 ||
        artifact->payload_type !=
            ATM_SCIENTIFIC_ARTIFACT_PAYLOAD_BOOLEAN ||
        !(g_strcmp0 (
              artifact->payload,
              "true"
          ) == 0 ||
          g_strcmp0 (
              artifact->payload,
              "false"
          ) == 0)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
            "Canonical scientific control artifact does not match an AtM-owned control class."
        );
        return FALSE;
    }

    AtmScientificControlValue control = { 0 };

    control.control_id =
        ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE;
    control.repository_id = artifact->repository_id;
    control.snapshot_sha = artifact->snapshot_sha;
    control.source_path = artifact->source_path;
    control.json_pointer = artifact->locator;
    control.value_type =
        ATM_SCIENTIFIC_CONTROL_VALUE_BOOLEAN;
    control.boolean_value =
        g_strcmp0 (
            artifact->payload,
            "true"
        ) == 0;

    const char *artifact_class = NULL;
    const char *scalar_type = NULL;
    AtmScientificArtifactPayloadType payload_type;
    char *payload = NULL;

    if (!control_contract (
            &control,
            &artifact_class,
            &payload_type,
            &scalar_type,
            &payload,
            error
        )) {
        return FALSE;
    }

    gboolean matches =
        g_strcmp0 (
            artifact_class,
            artifact->artifact_class
        ) == 0 &&
        payload_type == artifact->payload_type &&
        g_strcmp0 (
            payload,
            artifact->payload
        ) == 0;

    g_free (payload);

    if (!matches) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_CONTROL,
            "Canonical scientific control artifact failed control-contract validation."
        );
        return FALSE;
    }

    return TRUE;
}

gboolean
atm_scientific_artifact_validate (
    const AtmScientificArtifact *artifact,
    GError **error
)
{
    if (!shape_is_valid (artifact)) {
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_PROVENANCE,
            "Canonical scientific artifact has an invalid shape or provenance."
        );
        return FALSE;
    }

    if (artifact->kind ==
            ATM_SCIENTIFIC_ARTIFACT_KIND_CONTROL &&
        !validate_control_shape (
            artifact,
            error
        )) {
        return FALSE;
    }

    char *expected_content_id = NULL;
    char *expected_qualified_id = NULL;
    char *expected_storage_digest = NULL;
    GError *local_error = NULL;

    if (artifact->kind ==
        ATM_SCIENTIFIC_ARTIFACT_KIND_EVIDENCE) {
        if (!atm_scientific_content_id_json (
                artifact->artifact_class,
                artifact->payload,
                &expected_content_id,
                &local_error
            )) {
            goto identity_failure;
        }
    } else {
        if (!atm_scientific_content_id_scalar (
                artifact->artifact_class,
                "boolean",
                artifact->payload,
                &expected_content_id,
                &local_error
            )) {
            goto identity_failure;
        }
    }

    if (!atm_scientific_qualified_artifact_id (
            expected_content_id,
            artifact->repository_id,
            artifact->repository_version,
            artifact->snapshot_sha,
            artifact->source_path,
            artifact->locator,
            artifact->logical_source_id,
            artifact->profile_id,
            artifact->profile_version,
            &expected_qualified_id,
            &local_error
        )) {
        goto identity_failure;
    }

    expected_storage_digest =
        compute_storage_digest (artifact);

    if (expected_storage_digest == NULL) {
        goto identity_failure;
    }

    if (g_strcmp0 (
            expected_content_id,
            artifact->scientific_content_id
        ) != 0 ||
        g_strcmp0 (
            expected_qualified_id,
            artifact->qualified_artifact_id
        ) != 0 ||
        g_strcmp0 (
            expected_storage_digest,
            artifact->storage_digest
        ) != 0) {
        g_free (expected_storage_digest);
        g_free (expected_qualified_id);
        g_free (expected_content_id);
        g_set_error_literal (
            error,
            ATM_SCIENTIFIC_ARTIFACT_ERROR,
            ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY,
            "Canonical scientific artifact identities do not match its current contents."
        );
        return FALSE;
    }

    g_free (expected_storage_digest);
    g_free (expected_qualified_id);
    g_free (expected_content_id);
    return TRUE;

identity_failure:
    g_free (expected_storage_digest);
    g_free (expected_qualified_id);
    g_free (expected_content_id);

    g_set_error (
        error,
        ATM_SCIENTIFIC_ARTIFACT_ERROR,
        ATM_SCIENTIFIC_ARTIFACT_ERROR_IDENTITY,
        "Could not recompute canonical scientific artifact identity: %s.",
        local_error != NULL
            ? local_error->message
            : "unknown canonicalization error"
    );
    g_clear_error (&local_error);
    return FALSE;
}
