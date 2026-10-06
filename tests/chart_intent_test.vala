using GLib;

int main () {
    var requested = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show the global temperature trend."
    );
    assert (requested.gistemp_requested);
    assert (requested.query == "Show the global temperature trend.");

    var upper = AskTheModel.ChartIntent.parse (
        "/CHART GISTEMP Show the global temperature trend."
    );
    assert (upper.gistemp_requested);
    assert (upper.query == "Show the global temperature trend.");

    var ordinary = AskTheModel.ChartIntent.parse (
        "show me the global temperature trend"
    );
    assert (!ordinary.gistemp_requested);
    assert (ordinary.query == "show me the global temperature trend");

    var near_miss = AskTheModel.ChartIntent.parse (
        "/chart gistemp-ish Show the trend"
    );
    assert (!near_miss.gistemp_requested);

    var empty = AskTheModel.ChartIntent.parse (
        "/chart gistemp"
    );
    assert (empty.gistemp_requested);
    assert (empty.query == "");

    return 0;
}
