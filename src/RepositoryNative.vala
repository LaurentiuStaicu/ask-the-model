namespace AskTheModel.RepositoryNative {
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
        cname = "atm_repository_ui_ensure_index",
        cheader_filename = "repository_ui_bridge.h"
    )]
    public static extern bool ensure_index (
        string cache_root,
        string snapshot_root,
        string repository_id,
        string snapshot_sha,
        out string index_path,
        out string repository_version
    ) throws GLib.Error;
}
