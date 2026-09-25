namespace AskTheModel.ControlStateNative {
    [CCode (
        cname = "atm_control_state_active_generation_id",
        cheader_filename = "control_state.h"
    )]
    public static extern bool active_generation_id (
        string path,
        out int64 generation_id
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_active_generation_id_readonly",
        cheader_filename = "control_state.h"
    )]
    public static extern bool active_generation_id_readonly (
        string path,
        out int64 generation_id
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_load_repository_values_at_generation",
        cheader_filename = "control_state.h"
    )]
    public static extern bool load_repository_values_at_generation (
        string path,
        int64 generation_id,
        string repository_id,
        out bool present,
        out string? snapshot_sha,
        out string? repository_version,
        out string? snapshot_seal_sha256
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_load_repository_values_at_generation_readonly",
        cheader_filename = "control_state.h"
    )]
    public static extern bool load_repository_values_at_generation_readonly (
        string path,
        int64 generation_id,
        string repository_id,
        out bool present,
        out string? snapshot_sha,
        out string? repository_version,
        out string? snapshot_seal_sha256
    ) throws GLib.Error;

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
        cname = "atm_control_state_set_current_values_guarded",
        cheader_filename = "control_state.h"
    )]
    public static extern bool set_current_values_guarded (
        string path,
        int64 expected_generation_id,
        string repository_id,
        string snapshot_sha,
        string repository_version,
        string? snapshot_seal_sha256,
        out int64 generation_id
    ) throws GLib.Error;

    [CCode (
        cname = "atm_control_state_set_snapshot_seal_values_guarded",
        cheader_filename = "control_state.h"
    )]
    public static extern bool set_snapshot_seal_values_guarded (
        string path,
        int64 expected_generation_id,
        string repository_id,
        string expected_snapshot_sha,
        string snapshot_seal_sha256,
        out int64 generation_id
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
