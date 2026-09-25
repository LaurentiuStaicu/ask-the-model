namespace AskTheModel.RepositoryNative {
    [CCode (
        cname = "AtmCapacityOperationKind",
        cprefix = "ATM_CAPACITY_OPERATION_",
        cheader_filename = "capacity_operation_plan.h"
    )]
    public enum CapacityOperationKind {
        FRESH_INSTALL,
        DIFFERENT_SHA_UPDATE,
        SAME_SHA_REPAIR
    }

    [CCode (
        cname = "atm_cff_extract_version",
        cheader_filename = "cff_version.h"
    )]
    public static extern bool cff_extract_version (
        [CCode (array_length = false)] uint8[] data,
        size_t length,
        out string version
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_ingest_archive",
        cheader_filename = "repository_ingest.h"
    )]
    public static extern bool ingest_archive (
        string data_root,
        string archive_path,
        string repository_id,
        string repository_acronym,
        string repository_display_name,
        string sha,
        out string version,
        out string snapshot_path,
        out uint64 entries,
        out uint64 total_bytes
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_ingest_archive_durable",
        cheader_filename = "repository_ingest.h"
    )]
    public static extern bool ingest_archive_durable (
        string data_root,
        string archive_path,
        string repository_id,
        string repository_acronym,
        string repository_display_name,
        string sha,
        out string pre_barrier_seal,
        out string version,
        out string snapshot_path,
        out uint64 entries,
        out uint64 total_bytes
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_quarantine_snapshot",
        cheader_filename = "repository_storage.h"
    )]
    public static extern bool quarantine_snapshot (
        string data_root,
        string repository_id,
        string sha,
        out string quarantine_path
    ) throws GLib.Error;

    [CCode (
        cname = "atm_snapshot_seal_compute",
        cheader_filename = "snapshot_seal.h"
    )]
    public static extern bool compute_snapshot_seal (
        string snapshot_root,
        out string sha256,
        out uint64 file_count,
        out uint64 total_bytes
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_ui_ensure_index",
        cheader_filename = "repository_ui_bridge.h"
    )]
    public static extern bool ensure_index (
        string state_root,
        bool coordinated,
        string cache_root,
        string snapshot_root,
        string repository_id,
        string snapshot_sha,
        out string index_path,
        out string repository_version
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_capacity_ui_download_preflight",
        cheader_filename = "repository_capacity_ui_bridge.h"
    )]
    public static extern bool capacity_download_preflight (
        string cache_path,
        string repository_id,
        string repository_sha,
        out bool admitted,
        out bool byte_prediction_qualified,
        out string detail
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_capacity_ui_mutation_preflight",
        cheader_filename = "repository_capacity_ui_bridge.h"
    )]
    public static extern bool capacity_mutation_preflight (
        string data_path,
        string cache_path,
        string state_path,
        string archive_path,
        string repository_id,
        string repository_sha,
        CapacityOperationKind operation_kind,
        out bool admitted,
        out bool byte_prediction_qualified,
        out bool must_admit_before_quarantine,
        out string detail
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_capacity_ui_state_commit_preflight",
        cheader_filename = "repository_capacity_ui_bridge.h"
    )]
    public static extern bool capacity_state_commit_preflight (
        string state_path,
        out bool admitted,
        out string detail
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_mutation_lease_try_acquire",
        cheader_filename = "repository_mutation_lease.h"
    )]
    public static extern bool try_acquire_mutation_lease (
        string state_root,
        out int lease_fd,
        out bool contended
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_mutation_lease_release",
        cheader_filename = "repository_mutation_lease.h"
    )]
    public static extern void release_mutation_lease (
        int lease_fd
    );


    [CCode (
        cname = "atm_repository_generation_lease_try_acquire_shared",
        cheader_filename = "repository_generation_lease.h"
    )]
    public static extern bool try_acquire_generation_lease_shared (
        string state_root,
        int64 generation_id,
        out int lease_fd,
        out bool contended
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_generation_lease_try_acquire_exclusive",
        cheader_filename = "repository_generation_lease.h"
    )]
    public static extern bool try_acquire_generation_lease_exclusive (
        string state_root,
        int64 generation_id,
        out int lease_fd,
        out bool contended
    ) throws GLib.Error;

    [CCode (
        cname = "atm_repository_generation_lease_release",
        cheader_filename = "repository_generation_lease.h"
    )]
    public static extern void release_generation_lease (
        int lease_fd
    );
}
