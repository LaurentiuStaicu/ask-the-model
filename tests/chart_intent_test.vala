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
    assert (ei_response.contains ("Data tab"));
    assert (ei_response.contains ("source details"));
    assert (!ei_response.contains ("source provenance"));
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
    assert (gistemp_response.contains ("Data tab"));
    assert (gistemp_response.contains ("source details"));
    assert (!gistemp_response.contains ("source provenance"));
    assert (!gistemp_response.contains ("pseudo-table"));
    assert (!gistemp_response.contains ("couldn't render"));

    var gistemp_interpretation = AskTheModel.ChartIntent.parse (
        "/chart gistemp Explain what the temperature trend means."
    );
    assert (!gistemp_interpretation.is_chart_only_request ());
    assert (gistemp_interpretation.chart_only_response () == "");

    assert (natural_english.chart_only_response ().contains ("GISTEMP global temperature anomaly chart"));
    assert (natural_english.chart_only_response ().contains ("source details"));
    assert (!natural_english.chart_only_response ().contains ("source provenance"));
    assert (natural_romanian.chart_only_response ().contains ("Iată graficul GISTEMP"));
    assert (natural_romanian.chart_only_response ().contains ("detalii despre sursă"));
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
    assert (ei_romanian.chart_only_response ().contains ("detaliile sursei"));

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

    var bnr_command = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Show the quarterly credit standards net percentage trend."
    );
    assert (bnr_command.bnr_bls_c01_requested);
    assert (!bnr_command.gistemp_requested);
    assert (!bnr_command.energy_institute_requested);
    assert (bnr_command.query ==
        "Show the quarterly credit standards net percentage trend.");
    assert (bnr_command.is_chart_only_request ());
    string bnr_guidance = bnr_command.response_guidance ();
    assert (bnr_guidance.contains ("pinned RMD snapshot"));
    assert (bnr_guidance.contains ("2025-Q2"));
    assert (bnr_guidance.contains ("MISSING, not zero"));
    string bnr_response = bnr_command.chart_only_response ();
    assert (bnr_response.contains ("BNR BLS C01 quarterly credit-standards"));
    assert (bnr_response.contains ("missing, not zero"));
    assert (bnr_response.contains ("source details"));
    assert (bnr_response.contains ("Data"));

    var bnr_upper = AskTheModel.ChartIntent.parse (
        "/CHART BNR-BLS-C01 Show the series."
    );
    assert (bnr_upper.bnr_bls_c01_requested);
    assert (bnr_upper.query == "Show the series.");

    var bnr_empty = AskTheModel.ChartIntent.parse ("/chart bnr-bls-c01");
    assert (bnr_empty.bnr_bls_c01_requested);
    assert (bnr_empty.query == "");

    var bnr_romanian = AskTheModel.ChartIntent.parse (
        "Generează un grafic BNR BLS C01 al procentului net privind standardele de credit."
    );
    assert (bnr_romanian.bnr_bls_c01_requested);
    assert (bnr_romanian.chart_only_response ().contains ("Iată graficul trimestrial BNR BLS C01"));
    assert (bnr_romanian.chart_only_response ().contains ("valoarea pentru 2025-Q2 lipsește, nu este zero"));
    assert (bnr_romanian.chart_only_response ().contains ("detaliile sursei"));

    var bnr_english = AskTheModel.ChartIntent.parse (
        "Generate a BNR BLS C01 chart of credit standards net percentage."
    );
    assert (bnr_english.bnr_bls_c01_requested);
    assert (bnr_english.chart_only_response ().contains ("Here is the BNR BLS C01"));

    var generic_net_percentage = AskTheModel.ChartIntent.parse (
        "Generate a chart of the net percentage of non-performing loans."
    );
    assert (!generic_net_percentage.bnr_bls_c01_requested);
    assert (!generic_net_percentage.gistemp_requested);
    assert (!generic_net_percentage.energy_institute_requested);

    var generic_romanian_net_percentage = AskTheModel.ChartIntent.parse (
        "Generează un grafic al procentului net pentru creditele neperformante."
    );
    assert (!generic_romanian_net_percentage.bnr_bls_c01_requested);

    var credit_standard_deviation = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standard deviation."
    );
    assert (!credit_standard_deviation.bnr_bls_c01_requested);

    var credit_standards_subject = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards over time."
    );
    assert (credit_standards_subject.bnr_bls_c01_requested);

    var credit_standards_romanian_genitive = AskTheModel.ChartIntent.parse (
        "Generează un grafic al standardelor de credit în timp."
    );
    assert (credit_standards_romanian_genitive.bnr_bls_c01_requested);

    var bnr_near_miss = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01-ish Show the series."
    );
    assert (!bnr_near_miss.bnr_bls_c01_requested);

    var bnr_interpretation = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Explain what the quarterly trend means."
    );
    assert (bnr_interpretation.bnr_bls_c01_requested);
    assert (!bnr_interpretation.is_chart_only_request ());
    assert (bnr_interpretation.chart_only_response () == "");

    var bnr_ambiguous = AskTheModel.ChartIntent.parse (
        "Generate a chart comparing BNR BLS C01 credit standards with Energy Institute total energy supply."
    );
    assert (!bnr_ambiguous.bnr_bls_c01_requested);
    assert (!bnr_ambiguous.energy_institute_requested);

    var bnr_command_compares_gistemp = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Compare this with GISTEMP global temperature."
    );
    assert (!bnr_command_compares_gistemp.bnr_bls_c01_requested);
    assert (!bnr_command_compares_gistemp.gistemp_requested);
    assert (!bnr_command_compares_gistemp.energy_institute_requested);

    var bnr_command_compares_energy = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Compare this with Energy Institute total energy supply."
    );
    assert (!bnr_command_compares_energy.bnr_bls_c01_requested);
    assert (!bnr_command_compares_energy.gistemp_requested);
    assert (!bnr_command_compares_energy.energy_institute_requested);

    var bnr_command_compares_gdp = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Compare with GDP."
    );
    assert (!bnr_command_compares_gdp.bnr_bls_c01_requested);

    var bnr_natural_comparison = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards against GDP."
    );
    assert (!bnr_natural_comparison.bnr_bls_c01_requested);

    var bnr_romanian_comparison = AskTheModel.ChartIntent.parse (
        "Generează un grafic comparativ al standardelor de credit față de PIB."
    );
    assert (!bnr_romanian_comparison.bnr_bls_c01_requested);

    var bnr_natural_multi_series = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards and GDP."
    );
    assert (!bnr_natural_multi_series.bnr_bls_c01_requested);

    var bnr_explicit_multi_series = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Show this with GDP."
    );
    assert (!bnr_explicit_multi_series.bnr_bls_c01_requested);

    var bnr_romanian_multi_series = AskTheModel.ChartIntent.parse (
        "Generează un grafic al standardelor de credit și inflației."
    );
    assert (!bnr_romanian_multi_series.bnr_bls_c01_requested);

    var bnr_comma_gdp = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards, GDP."
    );
    assert (!bnr_comma_gdp.bnr_bls_c01_requested);

    var bnr_romanian_comma_pib = AskTheModel.ChartIntent.parse (
        "Generează un grafic al standardelor de credit, PIB."
    );
    assert (!bnr_romanian_comma_pib.bnr_bls_c01_requested);

    var bnr_semicolon_gdp = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards; GDP."
    );
    assert (!bnr_semicolon_gdp.bnr_bls_c01_requested);

    var bnr_alongside_gdp = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards alongside GDP."
    );
    assert (!bnr_alongside_gdp.bnr_bls_c01_requested);

    var bnr_romanian_semicolon_pib = AskTheModel.ChartIntent.parse (
        "Generează un grafic al standardelor de credit; PIB."
    );
    assert (!bnr_romanian_semicolon_pib.bnr_bls_c01_requested);

    var bnr_natural_derived = AskTheModel.ChartIntent.parse (
        "Generate a chart of the standard deviation of credit standards."
    );
    assert (!bnr_natural_derived.bnr_bls_c01_requested);

    var bnr_explicit_derived = AskTheModel.ChartIntent.parse (
        "/chart bnr-bls-c01 Use a moving average of the series."
    );
    assert (!bnr_explicit_derived.bnr_bls_c01_requested);

    var bnr_romanian_derived = AskTheModel.ChartIntent.parse (
        "Generează un grafic cu medie mobilă pentru standardele de credit."
    );
    assert (!bnr_romanian_derived.bnr_bls_c01_requested);

    var bnr_romanian_definite_moving_average = AskTheModel.ChartIntent.parse (
        "Generează un grafic cu media mobilă a standardelor de credit."
    );
    assert (!bnr_romanian_definite_moving_average.bnr_bls_c01_requested);

    return 0;
}
