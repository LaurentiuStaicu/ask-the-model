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

    var natural_romanian = AskTheModel.ChartIntent.parse (
        "Arată-mi un grafic GISTEMP cu evoluția temperaturii globale în timp."
    );
    assert (natural_romanian.gistemp_requested);
    assert (natural_romanian.query ==
        "Arată-mi un grafic GISTEMP cu evoluția temperaturii globale în timp.");

    var natural_english = AskTheModel.ChartIntent.parse (
        "Show me a GISTEMP chart of global temperature over time."
    );
    assert (natural_english.gistemp_requested);

    var capability_question = AskTheModel.ChartIntent.parse (
        "Can you generate charts?"
    );
    assert (!capability_question.gistemp_requested);

    var generic_chart = AskTheModel.ChartIntent.parse (
        "Show me a chart of population over time."
    );
    assert (!generic_chart.gistemp_requested);

    var near_miss = AskTheModel.ChartIntent.parse (
        "/chart gistemp-ish Show the trend"
    );
    assert (!near_miss.gistemp_requested);

    var empty = AskTheModel.ChartIntent.parse (
        "/chart gistemp"
    );
    assert (empty.gistemp_requested);
    assert (empty.query == "");

    assert (AskTheModel.ChartIntent.is_chart_follow_up (
        "Generate the chart"
    ));
    assert (AskTheModel.ChartIntent.is_chart_follow_up (
        "Arată graficul"
    ));
    assert (!AskTheModel.ChartIntent.is_chart_follow_up (
        "Generate a chart of population"
    ));

    return 0;
}
