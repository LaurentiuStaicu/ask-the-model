[CCode (cheader_filename = "chart_view.h,chart_history_reconstruct.h")]
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
    }

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
    [CCode (cname = "atm_chart_live_reconstruct_gistemp")]
    public static bool reconstruct_live_gistemp (
        string snapshot_path,
        string repository_id,
        string repository_version,
        string snapshot_sha,
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
}
