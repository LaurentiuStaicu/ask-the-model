[CCode (cheader_filename = "startup_qualification.h,repository_reconcile.h")]
namespace AskTheModel.StartupQualificationNative {
    [CCode (cname = "atm_startup_qualify_deployment_values")]
    public static bool qualify_deployment_values (
        string flatpak_info_path,
        string expected_application_id,
        string expected_runtime_id,
        string expected_runtime_branch,
        uint policy_version,
        out int execution_mode,
        out bool platform_qualified,
        out string? application_id,
        out string? application_ref,
        out string? application_commit,
        out string? runtime_ref,
        out string? runtime_commit,
        out string? architecture,
        out string? branch,
        out string? flatpak_version,
        out string application_extensions,
        out string runtime_extensions,
        out string? platform_fingerprint
    ) throws GLib.Error;

    [CCode (cname = "atm_startup_qualify_storage_root_values")]
    public static bool qualify_storage_root_values (
        string storage_root,
        out bool qualified,
        out bool created,
        out uint32 mode,
        out uint64 owner_uid
    ) throws GLib.Error;

    [CCode (cname = "atm_repository_probe_snapshot_root")]
    public static bool probe_snapshot_root (
        string snapshot_root,
        out int status
    ) throws GLib.Error;

    [CCode (cname = "atm_repository_reconcile_local_values")]
    public static bool reconcile_local_values (
        string cache_root,
        string snapshot_root,
        string repository_id,
        string repository_acronym,
        string repository_display_name,
        string snapshot_sha,
        string persisted_version,
        out int status,
        out string reason_code,
        out string detail,
        out string? repository_version,
        out string? index_path
    ) throws GLib.Error;
}
