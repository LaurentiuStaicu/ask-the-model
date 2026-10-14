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
    assert (ei_guidance.contains ("native Chart/Data widget"));
    
    assert (ei_guidance.contains ("pseudo-table"));
    assert (ei_guidance.contains ("more years are unavailable"));
    assert (ei_guidance.contains ("Only provide year-by-year values or a table"));
    assert (ei_command.is_chart_only_request ());
    string ei_response = ei_command.chart_only_response ();
    assert (ei_response.contains ("Energy Institute global total energy supply chart"));
    assert (ei_response.contains ("native Chart/Data view"));
    assert (!ei_response.contains ("pseudo-table"));
    assert (!ei_response.contains ("couldn't render"));

    var ei_interpretation = AskTheModel.ChartIntent.parse (
        "/chart energy-institute Explain the trend in the data."
    );
    assert (!ei_interpretation.is_chart_only_request ());
    assert (ei_interpretation.chart_only_response () == "");

    var ei_table = AskTheModel.ChartIntent.parse (
        "/chart energy-institute Show the annual values in a table."
    );
    assert (!ei_table.is_chart_only_request ());

    var gistemp_value_question = AskTheModel.ChartIntent.parse (
        "/chart gistemp What was the temperature anomaly in 1880?"
    );
    assert (!gistemp_value_question.is_chart_only_request ());
    assert (gistemp_value_question.chart_only_response () == "");

    string gistemp_guidance = requested.response_guidance ();
    assert (gistemp_guidance.contains ("native Chart/Data widget"));
    
    assert (gistemp_guidance.contains ("pseudo-table"));
    assert (gistemp_guidance.contains ("more years are unavailable"));
    assert (gistemp_guidance.contains ("Only provide year-by-year values or a table"));
    assert (requested.is_chart_only_request ());
    string gistemp_response = requested.chart_only_response ();
    assert (gistemp_response.contains ("GISTEMP global temperature anomaly chart"));
    assert (gistemp_response.contains ("native Chart/Data view"));
    assert (!gistemp_response.contains ("pseudo-table"));
    assert (!gistemp_response.contains ("couldn't render"));

    var gistemp_interpretation = AskTheModel.ChartIntent.parse (
        "/chart gistemp Explain what the temperature trend means."
    );
    assert (!gistemp_interpretation.is_chart_only_request ());
    assert (gistemp_interpretation.chart_only_response () == "");

    assert (natural_english.chart_only_response ().contains ("GISTEMP global temperature anomaly chart"));
    assert (natural_romanian.chart_only_response ().contains ("Iată graficul GISTEMP"));
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
