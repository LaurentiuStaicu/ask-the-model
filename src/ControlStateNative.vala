namespace AskTheModel.ControlStateNative {
    [CCode (
        cname = "atm_control_state_load_repository_values",
        cheader_filename = "control_state.h"
    )]
    public static extern bool load_repository_values (
        string path,
        string repository_id,
        out bool present,
        out string? snapshot_sha,
        out string? repository_version,
        out string? snapshot_seal_sha256
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_set_current_values",
        cheader_filename = "control_state.h"
    )]
    public static extern bool set_current_values (
        string path,
        string repository_id,
        string snapshot_sha,
        string repository_version,
        string? snapshot_seal_sha256
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_set_snapshot_seal_values",
        cheader_filename = "control_state.h"
    )]
    public static extern bool set_snapshot_seal_values (
        string path,
        string repository_id,
        string expected_snapshot_sha,
        string snapshot_seal_sha256
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_publish_cutover",
        cheader_filename = "control_state.h"
    )]
    public static extern bool publish_cutover (
        string control_path,
        string legacy_json_path,
        out int disposition
    ) throws GLib.Error;
}
