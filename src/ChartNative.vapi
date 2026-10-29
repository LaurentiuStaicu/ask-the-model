[CCode (cheader_filename = "chart_view.h,chart_history_reconstruct.h,chart_catalog.h,chart_spec_v4.h")]
namespace AskTheModel.ChartNative {
    [Compact]
    [CCode (cname = "AtmChartSpec", free_function = "atm_chart_spec_free", has_type_id = false)]
    public class Spec {
        /* chart_spec.h: all returned pointers are borrowed until destruction. */
        [CCode (cname = "atm_chart_spec_series_scientific_id")]
        public unowned string? series_scientific_id (uint index);
        [CCode (cname = "atm_chart_spec_series_qualified_id")]
        public unowned string? series_qualified_id (uint index);
        [CCode (cname = "atm_chart_spec_id")]
        public unowned string? id ();
        [CCode (cname = "atm_chart_spec_schema")]
        public unowned string? schema ();
        [CCode (cname = "atm_chart_spec_series_profile")]
        public unowned string? series_profile ();
        [CCode (cname = "atm_chart_spec_series_snapshot_sha")]
        public unowned string? series_snapshot_sha (uint index);
        [CCode (cname = "atm_chart_spec_series_source_path")]
        public unowned string? series_source_path (uint index);
        [CCode (cname = "atm_chart_spec_series_repository_id")]
        public unowned string? series_repository_id (uint index);
    }

    [Compact]
    [CCode (cname = "AtmChartSpecV4", free_function = "atm_chart_spec_v4_free", has_type_id = false)]
    public class SpecV4 {
        /* chart_spec_v4.h: borrowed identity/metadata and source spec. */
        [CCode (cname = "atm_chart_spec_v4_schema")]
        public unowned string? schema ();
        [CCode (cname = "atm_chart_spec_v4_id")]
        public unowned string? id ();
        [CCode (cname = "atm_chart_spec_v4_series_id")]
        public unowned string? series_id ();
        [CCode (cname = "atm_chart_spec_v4_source_spec")]
        public unowned Spec? source_spec ();
        [CCode (cname = "atm_chart_spec_v4_admission_profile_id")]
        public unowned string? admission_profile_id ();
        [CCode (cname = "atm_chart_spec_v4_reconstruction_profile_id")]
        public unowned string? reconstruction_profile_id ();
        [CCode (cname = "atm_chart_spec_v4_history_schema_version")]
        public unowned string? history_schema_version ();
    }

    [CCode (cname = "atm_chart_catalog_adapter_id_for_series")]
    public static unowned string? catalog_adapter_id_for_series (
        string series_id
    );

    [CCode (cname = "atm_chart_catalog_repository_matches_series")]
    public static bool catalog_repository_matches_series (
        string series_id,
        string repository_id
    );

    [CCode (cname = "atm_chart_catalog_match_request_series_id")]
    public static string? match_catalog_request_series_id (
        string request,
        out bool ambiguous
    );

    [CCode (cname = "atm_chart_wrap_qualified_spec_v4")]
    public static bool wrap_qualified_spec_v4 (
        string series_id,
        Spec qualified_source_spec,
        out SpecV4 spec
    ) throws GLib.Error;

    [CCode (cname = "atm_chart_history_presentation_input_from_v4")]
    public static bool history_presentation_input_from_v4 (
        SpecV4 spec,
        out ChartPresentationNative.PresentationInput presentation
    ) throws GLib.Error;

    [CCode (cname = "atm_chart_history_reconstruct_v4")]
    public static bool reconstruct_history_v4 (
        string series_id,
        Spec qualified_source_spec,
        ref ChartPresentationNative.PresentationInput presentation,
        out SpecV4 spec
    ) throws GLib.Error;

    [CCode (cname = "atm_chart_live_reconstruct_v4")]
    public static bool reconstruct_live_v4 (
        string series_id,
        string snapshot_path,
        string repository_id,
        string? repository_version,
        string snapshot_sha,
        out SpecV4 spec
    ) throws GLib.Error;

    [CCode (cname = "AtmChartView", lower_case_cprefix = "atm_chart_view_", type_id = "atm_chart_view_get_type ()")]
    public class View : Gtk.Box {
        public uint row_count ();
        public unowned string? data ();
        public unowned string? visible_page ();
        public void show_chart ();
        public void show_data ();
        public bool select_row (uint row);
        public uint selected_row ();
        /* chart_view.h: caller owns returned provenance. */
        public string? provenance ();
    }
    [CCode (cname = "atm_chart_view_new")]
    public static bool create (Spec? spec, out View view) throws GLib.Error;
    [CCode (cname = "atm_chart_view_new_v4")]
    public static bool create_v4 (SpecV4? spec, out View view) throws GLib.Error;
    [CCode (cname = "atm_chart_live_reconstruct_gistemp")]
    public static bool reconstruct_live_gistemp (
        string snapshot_path,
        string repository_id,
        string repository_version,
        string snapshot_sha,
        out Spec spec
    ) throws GLib.Error;
    [CCode (cname = "atm_chart_live_reconstruct_energy_institute")]
    public static bool reconstruct_live_energy_institute (
        string snapshot_path,
        string repository_id,
        string snapshot_sha,
        string chart_kind,
        out Spec spec
    ) throws GLib.Error;
    [CCode (cname = "atm_chart_live_reconstruct_bnr_bls_c01")]
    public static bool reconstruct_live_bnr_bls_c01 (
        string snapshot_path,
        string repository_id,
        string snapshot_sha,
        string chart_kind,
        out Spec spec
    ) throws GLib.Error;
    [CCode (cname = "atm_chart_history_reconstruct_gistemp")]
    public static bool reconstruct_history_gistemp (
        string snapshot_path,
        string repository_id,
        string repository_version,
        string snapshot_sha,
        string chart_schema,
        string chart_spec_id,
        string chart_kind,
        string reconstruction_profile,
        string series_profile,
        string admission_profile,
        string scientific_id,
        string qualified_id,
        string source_path,
        out Spec spec
    ) throws GLib.Error;
    [CCode (cname = "atm_chart_history_reconstruct_energy_institute")]
    public static bool reconstruct_history_energy_institute (
        string snapshot_path,
        string repository_id,
        string repository_version,
        string snapshot_sha,
        string chart_schema,
        string chart_spec_id,
        string chart_kind,
        string reconstruction_profile,
        string series_profile,
        string admission_profile,
        string scientific_id,
        string qualified_id,
        string source_path,
        out Spec spec
    ) throws GLib.Error;
    [CCode (cname = "atm_chart_history_reconstruct_bnr_bls_c01")]
    public static bool reconstruct_history_bnr_bls_c01 (
        string snapshot_path,
        string repository_id,
        string repository_version,
        string snapshot_sha,
        string chart_schema,
        string chart_spec_id,
        string chart_kind,
        string reconstruction_profile,
        string series_profile,
        string admission_profile,
        string scientific_id,
        string qualified_id,
        string source_path,
        out Spec spec
    ) throws GLib.Error;
}
