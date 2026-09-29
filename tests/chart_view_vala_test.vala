using AskTheModel.ChartNative;
[CCode (cname = "atm_chart_test_spec", cheader_filename = "chart_fixture.h")]
extern Spec fixture (uint kind);
int main (string[] args) {
    Gtk.init ();
    Spec? spec = fixture (1);
    try {
        View view;
        assert (create (spec, out view));
        spec = null;
        assert (view.row_count () == 146);
        view.show_data ();
        assert (view.visible_page () == "data");
        assert (view.select_row (145));
        assert (view.selected_row () == 145);
        assert (view.provenance ().contains ("Year: 2025"));
        assert (view.data ().contains ("degree_Celsius_anomaly"));
        view.show_chart ();
        assert (view.visible_page () == "chart");
    } catch (Error error) {
        GLib.error ("Chart bridge failed: %s", error.message);
    }
    bool rejected = false;
    try {
        View invalid;
        create (null, out invalid);
    } catch (Error error) { rejected = true; }
    assert (rejected);
    return 0;
}
