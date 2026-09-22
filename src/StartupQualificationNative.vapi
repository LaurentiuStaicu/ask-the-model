[CCode (cheader_filename = "startup_qualification.h,repository_reconcile.h")]
namespace AskTheModel.StartupQualificationNative {
    [CCode (
        cname = "AtmExecutionMode",
        cprefix = "ATM_EXECUTION_MODE_",
        has_type_id = false
    )]
    public enum ExecutionMode {
        DEVELOPMENT,
        FLATPAK
    }

    [CCode (
        cname = "AtmRepositoryReconcileStatus",
        cprefix = "ATM_REPOSITORY_RECONCILE_",
        has_type_id = false
    )]
    public enum RepositoryReconcileStatus {
        SNAPSHOT_MISSING,
        SNAPSHOT_INVALID,
        INDEX_ERROR,
        READY,
        READY_REPAIRED_INDEX
    }

    [CCode (
        cname = "AtmDeploymentQualification",
        free_function = "atm_deployment_qualification_free",
        has_type_id = false
    )]
    [Compact]
    public class DeploymentQualification {
        public ExecutionMode execution_mode;
        public bool platform_qualified;

        public unowned string? application_id;
        public unowned string? application_ref;
        public unowned string? application_commit;

        public unowned string? runtime_ref;
        public unowned string? runtime_commit;

        public unowned string? architecture;
        public unowned string? branch;
        public unowned string? flatpak_version;

        [CCode (
            array_length = false,
            array_null_terminated = true
        )]
        public unowned string[]? application_extensions;

        [CCode (
            array_length = false,
            array_null_terminated = true
        )]
        public unowned string[]? runtime_extensions;

        public unowned string? platform_fingerprint;
    }

    [CCode (
        cname = "AtmRepositoryReconcileResult",
        free_function = "atm_repository_reconcile_result_free",
        has_type_id = false
    )]
    [Compact]
    public class RepositoryReconcileResult {
        public RepositoryReconcileStatus status;
        public unowned string? reason_code;
        public unowned string? detail;
        public unowned string? repository_version;
        public unowned string? index_path;
    }

    [CCode (cname = "atm_startup_qualify_deployment")]
    public static bool qualify_deployment (
        string flatpak_info_path,
        string expected_application_id,
        string expected_runtime_id,
        string expected_runtime_branch,
        uint policy_version,
        out DeploymentQualification qualification
    ) throws GLib.Error;

    [CCode (cname = "atm_startup_qualify_storage_root_values")]
    public static bool qualify_storage_root_values (
        string storage_root,
        out bool qualified,
        out bool created,
        out uint32 mode,
        out uint64 owner_uid
    ) throws GLib.Error;

    [CCode (cname = "atm_repository_reconcile_local")]
    public static bool reconcile_local (
        string cache_root,
        string snapshot_root,
        string repository_id,
        string repository_acronym,
        string repository_display_name,
        string snapshot_sha,
        string persisted_version,
        out RepositoryReconcileResult result
    ) throws GLib.Error;
}
