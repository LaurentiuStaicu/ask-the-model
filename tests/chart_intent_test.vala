using GLib;

int main () {
    var requested = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show the global temperature trend."
    );
    assert (requested.gistemp_requested);
    assert (!requested.energy_institute_requested);
    assert (requested.query == "Show the global temperature trend.");

    var upper = AskTheModel.ChartIntent.parse (
        "/CHART GISTEMP Show the global temperature trend."
    );
    assert (upper.gistemp_requested);
    assert (!upper.energy_institute_requested);
    assert (upper.query == "Show the global temperature trend.");

    var ordinary = AskTheModel.ChartIntent.parse (
        "show me the global temperature trend"
    );
    assert (!ordinary.gistemp_requested);
    assert (!ordinary.energy_institute_requested);
    assert (ordinary.query == "show me the global temperature trend");

    var natural_romanian = AskTheModel.ChartIntent.parse (
        "Arată-mi un grafic GISTEMP cu evoluția temperaturii globale în timp."
    );
    assert (natural_romanian.gistemp_requested);
    assert (!natural_romanian.energy_institute_requested);
    assert (natural_romanian.query ==
        "Arată-mi un grafic GISTEMP cu evoluția temperaturii globale în timp.");

    var natural_english = AskTheModel.ChartIntent.parse (
        "Show me a GISTEMP chart of global temperature over time."
    );
    assert (natural_english.gistemp_requested);
    assert (!natural_english.energy_institute_requested);

    var capability_question = AskTheModel.ChartIntent.parse (
        "Can you generate charts?"
    );
    assert (!capability_question.gistemp_requested);
    assert (!capability_question.energy_institute_requested);

    var generic_chart = AskTheModel.ChartIntent.parse (
        "Show me a chart of population over time."
    );
    assert (!generic_chart.gistemp_requested);
    assert (!generic_chart.energy_institute_requested);

    var near_miss = AskTheModel.ChartIntent.parse (
        "/chart gistemp-ish Show the trend"
    );
    assert (!near_miss.gistemp_requested);
    assert (!near_miss.energy_institute_requested);

    var empty = AskTheModel.ChartIntent.parse (
        "/chart gistemp"
    );
    assert (empty.gistemp_requested);
    assert (empty.query == "");

    var ei_command = AskTheModel.ChartIntent.parse (
        "/chart energy-institute Show the global TES trend."
    );
    assert (!ei_command.gistemp_requested);
    assert (ei_command.energy_institute_requested);
    assert (ei_command.query == "Show the global TES trend.");

    string ei_guidance = ei_command.response_guidance ();
    assert (ei_guidance.contains ("Chart/Data tabs"));
    assert (ei_guidance.contains ("at most one brief sentence"));
    assert (ei_guidance.contains ("pseudo-table"));
    assert (ei_guidance.contains ("more years are unavailable"));
    assert (ei_guidance.contains ("Only provide year-by-year values or a table"));

    string gistemp_guidance = requested.response_guidance ();
    assert (gistemp_guidance.contains ("Chart/Data tabs"));
    assert (gistemp_guidance.contains ("at most one brief sentence"));
    assert (gistemp_guidance.contains ("pseudo-table"));
    assert (gistemp_guidance.contains ("more years are unavailable"));
    assert (gistemp_guidance.contains ("Only provide year-by-year values or a table"));

    assert (ordinary.response_guidance () == "");

    var ei_upper = AskTheModel.ChartIntent.parse (
        "/CHART ENERGY-INSTITUTE Show the global TES trend."
    );
    assert (ei_upper.energy_institute_requested);
    assert (ei_upper.query == "Show the global TES trend.");

    var ei_romanian = AskTheModel.ChartIntent.parse (
        "Arată-mi un grafic Energy Institute despre furnizarea totală de energie."
    );
    assert (!ei_romanian.gistemp_requested);
    assert (ei_romanian.energy_institute_requested);

    var ei_english = AskTheModel.ChartIntent.parse (
        "Show me an Energy Institute chart of global total energy supply using Physical Energy Content."
    );
    assert (!ei_english.gistemp_requested);
    assert (ei_english.energy_institute_requested);

    var ambiguous = AskTheModel.ChartIntent.parse (
        "Show a chart comparing GISTEMP global temperature with Energy Institute total energy supply."
    );
    assert (!ambiguous.gistemp_requested);
    assert (!ambiguous.energy_institute_requested);

    var ei_near_miss = AskTheModel.ChartIntent.parse (
        "/chart energy-institute-ish Show the trend"
    );
    assert (!ei_near_miss.gistemp_requested);
    assert (!ei_near_miss.energy_institute_requested);

    return 0;
}
