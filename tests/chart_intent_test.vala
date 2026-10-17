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
    assert (generic_chart.series_id == null);
    assert (generic_chart.intent == "unsupported");
    assert (generic_chart.response_mode == "none");

    var near_miss = AskTheModel.ChartIntent.parse (
        "/chart gistemp-ish Show the trend"
    );
    assert (!near_miss.gistemp_requested);
    assert (!near_miss.energy_institute_requested);
    assert (near_miss.series_id == null);
    assert (near_miss.intent == "unsupported");

    var unknown_slash_selector = AskTheModel.ChartIntent.parse (
        "/chart population"
    );
    assert (unknown_slash_selector.series_id == null);
    assert (unknown_slash_selector.intent == "unsupported");

    var unknown_slash_selector_with_verb = AskTheModel.ChartIntent.parse (
        "/chart population generate a time trend"
    );
    assert (unknown_slash_selector_with_verb.series_id == null);
    assert (unknown_slash_selector_with_verb.intent == "unsupported");

    var bare_chart_command = AskTheModel.ChartIntent.parse ("/chart");
    assert (bare_chart_command.series_id == null);
    assert (bare_chart_command.intent == "unsupported");

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
    assert (ei_guidance.contains ("Physical Energy Content"));
    assert (ei_guidance.contains ("gross supply"));
    assert (ei_guidance.contains ("does not independently reproduce publisher"));
    assert (ei_guidance.contains ("only provide those details when explicitly requested"));
    assert (ei_command.is_chart_only_request ());
    string ei_response = ei_command.chart_only_response ();
    assert (ei_response.contains ("Global total energy supply (Energy Institute TES)"));
    assert (ei_response.contains ("qualified Chart/Data view"));
    assert (ei_response.contains ("source provenance"));
    assert (ei_response.contains ("If chart construction fails"));
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
    assert (gistemp_guidance.contains ("Global annual land-ocean temperature anomaly"));
    assert (gistemp_guidance.contains ("NASA's processing"));
    assert (gistemp_guidance.contains ("only provide those details when explicitly requested"));
    assert (requested.is_chart_only_request ());
    string gistemp_response = requested.chart_only_response ();
    assert (gistemp_response.contains ("Global surface temperature anomaly (GISTEMP)"));
    assert (gistemp_response.contains ("qualified Chart/Data view"));
    assert (gistemp_response.contains ("source provenance"));
    assert (gistemp_response.contains ("If chart construction fails"));
    assert (!gistemp_response.contains ("pseudo-table"));
    assert (!gistemp_response.contains ("couldn't render"));

    var gistemp_interpretation = AskTheModel.ChartIntent.parse (
        "/chart gistemp Explain what the temperature trend means."
    );
    assert (!gistemp_interpretation.is_chart_only_request ());
    assert (gistemp_interpretation.chart_only_response () == "");

    assert (natural_english.chart_only_response ().contains ("Global surface temperature anomaly (GISTEMP)"));
    assert (natural_english.chart_only_response ().contains ("source provenance"));
    assert (natural_romanian.chart_only_response ().contains ("Cerere de grafic: Anomalia globală a temperaturii de la suprafață (GISTEMP)"));
    assert (natural_romanian.chart_only_response ().contains ("proveniența"));
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
    assert (ei_romanian.chart_only_response ().contains ("proveniența"));

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
    assert (bnr_guidance.contains ("romanian-monetary-dynamics"));
    assert (bnr_guidance.contains ("2025-Q2"));
    assert (bnr_guidance.contains ("MISSING"));
    assert (bnr_guidance.contains ("not zero"));
    string bnr_response = bnr_command.chart_only_response ();
    assert (bnr_response.contains ("BNR Bank Lending Survey: credit standards net percentage"));
    assert (bnr_response.contains ("2025-Q2 observation is missing from the source, not zero"));
    assert (bnr_response.contains ("source provenance"));
    assert (bnr_response.contains ("Chart/Data"));
    assert (bnr_response.contains ("If chart construction fails"));

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
    assert (bnr_romanian.chart_only_response ().contains ("Cerere de grafic: Sondajul BNR privind creditarea: procentul net al standardelor de credit"));
    assert (bnr_romanian.chart_only_response ().contains ("2025-Q2 lipsește din sursă; este lipsă, nu zero"));
    assert (bnr_romanian.chart_only_response ().contains ("proveniența"));

    var bnr_english = AskTheModel.ChartIntent.parse (
        "Generate a BNR BLS C01 chart of credit standards net percentage."
    );
    assert (bnr_english.bnr_bls_c01_requested);
    assert (bnr_english.chart_only_response ().contains ("Requested chart: BNR Bank Lending Survey"));

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


    /* Generic series routing requires a catalog alias and a positive request. */
    assert (natural_english.series_id ==
        "climate.gistemp.global-temperature-anomaly");
    assert (natural_english.intent == "chart");
    assert (natural_english.response_mode == "chart_only");
    assert (ei_english.series_id ==
        "energy.energy-institute.global-total-energy-supply");
    assert (bnr_english.series_id ==
        "banking.bnr.bls-c01.credit-standards-net-percentage");

    var negated_english = AskTheModel.ChartIntent.parse (
        "Do not make a GISTEMP chart."
    );
    assert (negated_english.series_id == null);
    assert (negated_english.intent == "not_chart");
    assert (negated_english.response_mode == "none");

    var negated_show_english = AskTheModel.ChartIntent.parse (
        "Don't show me a GISTEMP chart."
    );
    assert (negated_show_english.series_id == null);
    assert (negated_show_english.intent == "not_chart");

    var negated_show_romanian = AskTheModel.ChartIntent.parse (
        "Nu arăta un grafic GISTEMP."
    );
    assert (negated_show_romanian.series_id == null);
    assert (negated_show_romanian.intent == "not_chart");

    var negated_want_romanian = AskTheModel.ChartIntent.parse (
        "Nu vreau să generezi un grafic GISTEMP."
    );
    assert (negated_want_romanian.series_id == null);
    assert (negated_want_romanian.intent == "not_chart");

    var negated_explicit_request = AskTheModel.ChartIntent.parse (
        "I am not asking for a chart of GISTEMP."
    );
    assert (negated_explicit_request.series_id == null);

    var negated_romanian = AskTheModel.ChartIntent.parse (
        "Nu genera un grafic GISTEMP."
    );
    assert (negated_romanian.series_id == null);
    assert (negated_romanian.intent == "not_chart");

    var hypothetical_english = AskTheModel.ChartIntent.parse (
        "What if I were to generate a GISTEMP chart?"
    );
    assert (hypothetical_english.series_id == null);

    var hypothetical_romanian = AskTheModel.ChartIntent.parse (
        "Ipotetic, ce s-ar întâmpla dacă aș genera un grafic GISTEMP?"
    );
    assert (hypothetical_romanian.series_id == null);

    var quoted_instruction = AskTheModel.ChartIntent.parse (
        "Does the phrase \"Generate a GISTEMP chart\" express a clear request?"
    );
    assert (quoted_instruction.series_id == null);

    var capability_specific = AskTheModel.ChartIntent.parse (
        "Can you generate charts for GISTEMP?"
    );
    assert (capability_specific.series_id == null);

    var capability_singular = AskTheModel.ChartIntent.parse (
        "Does this app support creating GISTEMP charts?"
    );
    assert (capability_singular.series_id == null);
    assert (capability_singular.intent == "not_chart");

    var capability_romanian_singular = AskTheModel.ChartIntent.parse (
        "Aplicația poate genera grafice GISTEMP?"
    );
    assert (capability_romanian_singular.series_id == null);
    assert (capability_romanian_singular.intent == "not_chart");

    var polite_request_english = AskTheModel.ChartIntent.parse (
        "Can you create a GISTEMP chart?"
    );
    assert (polite_request_english.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var polite_request_romanian = AskTheModel.ChartIntent.parse (
        "Poți genera un grafic GISTEMP?"
    );
    assert (polite_request_romanian.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var existing_chart_interpretation = AskTheModel.ChartIntent.parse (
        "What does the GISTEMP chart show?"
    );
    assert (existing_chart_interpretation.series_id == null);
    assert (existing_chart_interpretation.intent == "not_chart");

    var existing_chart_interpretation_romanian =
        AskTheModel.ChartIntent.parse ("Ce ne arată graficul GISTEMP?");
    assert (existing_chart_interpretation_romanian.series_id == null);
    assert (existing_chart_interpretation_romanian.intent == "not_chart");

    var chart_request_followed_by_interpretation =
        AskTheModel.ChartIntent.parse (
            "Generate a GISTEMP chart. What does the GISTEMP chart show?"
        );
    assert (chart_request_followed_by_interpretation.series_id ==
        "climate.gistemp.global-temperature-anomaly");
    assert (chart_request_followed_by_interpretation.intent == "chart");
    assert (!chart_request_followed_by_interpretation.is_chart_only_request ());

    var subject_clause_followed_by_interpretation =
        AskTheModel.ChartIntent.parse (
            "Generate a chart of global temperature anomalies. What does the GISTEMP chart show?"
        );
    assert (subject_clause_followed_by_interpretation.series_id ==
        "climate.gistemp.global-temperature-anomaly");
    assert (subject_clause_followed_by_interpretation.intent == "chart");
    assert (!subject_clause_followed_by_interpretation.is_chart_only_request ());

    var short_alias_near_miss = AskTheModel.ChartIntent.parse (
        "Make a chart about the latest Tesla production figures."
    );
    assert (short_alias_near_miss.series_id == null);

    var exact_credit_alias = AskTheModel.ChartIntent.parse (
        "Generate a chart of credit standards over time."
    );
    assert (exact_credit_alias.series_id ==
        "banking.bnr.bls-c01.credit-standards-net-percentage");

    var ambiguous_catalog_aliases = AskTheModel.ChartIntent.parse (
        "Generate a chart comparing GISTEMP and Energy Institute total energy supply."
    );
    assert (ambiguous_catalog_aliases.series_id == null);
    assert (ambiguous_catalog_aliases.intent == "ambiguous");

    /* A catalog alias in a source/method clause does not ground the chart subject. */
    var unrelated_subject_using_gistemp = AskTheModel.ChartIntent.parse (
        "Generate a chart of diabetes prevalence using GISTEMP."
    );
    assert (unrelated_subject_using_gistemp.series_id == null);
    assert (unrelated_subject_using_gistemp.intent == "ambiguous");

    var unrelated_subject_using_ei = AskTheModel.ChartIntent.parse (
        "Generate a chart of diabetes prevalence using Energy Institute."
    );
    assert (unrelated_subject_using_ei.series_id == null);
    assert (unrelated_subject_using_ei.intent == "ambiguous");

    var unrelated_subject_with_gistemp = AskTheModel.ChartIntent.parse (
        "Generate a chart of global temperature anomalies over time with diabetes prevalence."
    );
    assert (unrelated_subject_with_gistemp.series_id == null);
    assert (unrelated_subject_with_gistemp.intent == "ambiguous");

    var directly_grounded_gistemp = AskTheModel.ChartIntent.parse (
        "Generate a chart of GISTEMP global temperature anomaly."
    );
    assert (directly_grounded_gistemp.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var directly_grounded_ei = AskTheModel.ChartIntent.parse (
        "Show a chart of total energy supply (TES)."
    );
    assert (directly_grounded_ei.series_id ==
        "energy.energy-institute.global-total-energy-supply");

    var unsupported_pie_chart = AskTheModel.ChartIntent.parse (
        "Generate a pie chart of global temperature anomalies."
    );
    assert (unsupported_pie_chart.series_id == null);
    assert (unsupported_pie_chart.intent == "ambiguous");

    var unsupported_bar_chart = AskTheModel.ChartIntent.parse (
        "Generate a bar chart of Energy Institute total energy supply."
    );
    assert (unsupported_bar_chart.series_id == null);
    assert (unsupported_bar_chart.intent == "ambiguous");

    var unsupported_explicit_chart_kind = AskTheModel.ChartIntent.parse (
        "/chart gistemp Make a pie chart."
    );
    assert (unsupported_explicit_chart_kind.series_id == null);
    assert (unsupported_explicit_chart_kind.intent == "ambiguous");

    var explicit_population_conflict = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show population over time."
    );
    assert (explicit_population_conflict.series_id == null);
    assert (explicit_population_conflict.intent == "ambiguous");

    var explicit_unrelated_alias_conflict = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show a chart of diabetes prevalence using GISTEMP."
    );
    assert (explicit_unrelated_alias_conflict.series_id == null);
    assert (explicit_unrelated_alias_conflict.intent == "ambiguous");

    var explicit_unknown_subject_conflict = AskTheModel.ChartIntent.parse (
        "/chart gistemp Plot malaria prevalence over time."
    );
    assert (explicit_unknown_subject_conflict.series_id == null);
    assert (explicit_unknown_subject_conflict.intent == "ambiguous");

    var explicit_unknown_subject_conflict_romanian = AskTheModel.ChartIntent.parse (
        "/chart gistemp Desenează un grafic al prevalenței malariei în timp."
    );
    assert (explicit_unknown_subject_conflict_romanian.series_id == null);
    assert (explicit_unknown_subject_conflict_romanian.intent == "ambiguous");

    var explicit_unknown_trend_subject = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show the trend for malaria over time."
    );
    assert (explicit_unknown_trend_subject.series_id == null);
    assert (explicit_unknown_trend_subject.intent == "ambiguous");

    var explicit_unknown_subject_before_trend = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show malaria prevalence trend over time."
    );
    assert (explicit_unknown_subject_before_trend.series_id == null);
    assert (explicit_unknown_subject_before_trend.intent == "ambiguous");

    var explicit_unknown_trend_subject_romanian = AskTheModel.ChartIntent.parse (
        "/chart gistemp Arată tendința pentru malarie în timp."
    );
    assert (explicit_unknown_trend_subject_romanian.series_id == null);
    assert (explicit_unknown_trend_subject_romanian.intent == "ambiguous");

    var explicit_time_range_modifier = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show the trend for 1880-2025."
    );
    assert (explicit_time_range_modifier.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var explicit_time_range_modifier_romanian = AskTheModel.ChartIntent.parse (
        "/chart gistemp Arată tendința pentru 1880-2025."
    );
    assert (explicit_time_range_modifier_romanian.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var explicit_time_range_without_article = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show trend for 1880-2025."
    );
    assert (explicit_time_range_without_article.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var explicit_time_range_romanian_unaccented = AskTheModel.ChartIntent.parse (
        "/chart gistemp Arata tendinta pentru 1880-2025."
    );
    assert (explicit_time_range_romanian_unaccented.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var explicit_outside_18xx_range = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show trend for 1850-2020."
    );
    assert (explicit_outside_18xx_range.series_id == null);
    assert (explicit_outside_18xx_range.intent == "unsupported_period");

    var full_and_partial_ranges = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show trend for 1880-2025 and 2000-2020."
    );
    assert (full_and_partial_ranges.series_id == null);
    assert (full_and_partial_ranges.intent == "unsupported_period");

    var gistemp_reference_baseline = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show the trend relative to the 1951-1980 baseline."
    );
    assert (gistemp_reference_baseline.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var gistemp_reference_mean = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show the trend relative to the 1951-1980 mean."
    );
    assert (gistemp_reference_mean.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var gistemp_full_range_from_to = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show trend from 1880 to 2025."
    );
    assert (gistemp_full_range_from_to.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var gistemp_full_range_between = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show trend between 1880 and 2025."
    );
    assert (gistemp_full_range_between.series_id ==
        "climate.gistemp.global-temperature-anomaly");

    var ei_full_range_from_through = AskTheModel.ChartIntent.parse (
        "/chart energy-institute Show trend from 1965 through 2025."
    );
    assert (ei_full_range_from_through.series_id ==
        "energy.energy-institute.global-total-energy-supply");

    var gistemp_partial_from_to = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show trend from 1880 to 2020."
    );
    assert (gistemp_partial_from_to.series_id == null);
    assert (gistemp_partial_from_to.intent == "unsupported_period");

    var ei_full_coverage_range = AskTheModel.ChartIntent.parse (
        "/chart energy-institute Show trend for 1965-2025."
    );
    assert (ei_full_coverage_range.series_id ==
        "energy.energy-institute.global-total-energy-supply");

    var mismatched_full_coverage_range = AskTheModel.ChartIntent.parse (
        "/chart energy-institute Show trend for 1880-2025."
    );
    assert (mismatched_full_coverage_range.series_id == null);
    assert (mismatched_full_coverage_range.intent == "unsupported_period");

    var explicit_grounded_subject = AskTheModel.ChartIntent.parse (
        "/chart gistemp Show a chart of global temperature anomalies over time."
    );
    assert (explicit_grounded_subject.series_id ==
        "climate.gistemp.global-temperature-anomaly");


    var explicit_cross_series_conflict = AskTheModel.ChartIntent.parse (
        "/chart gistemp Compare global temperature with Energy Institute total energy supply."
    );
    assert (explicit_cross_series_conflict.series_id == null);
    assert (explicit_cross_series_conflict.intent == "ambiguous");

    var explicit_negation_conflict = AskTheModel.ChartIntent.parse (
        "/chart gistemp Do not make a chart."
    );
    assert (explicit_negation_conflict.series_id == null);
    assert (explicit_negation_conflict.intent == "ambiguous");

    var unsupported_alias = AskTheModel.ChartIntent.parse (
        "Generate a chart of population over time."
    );
    assert (unsupported_alias.intent == "unsupported");
    assert (unsupported_alias.series_id == null);

    /* A registered alias never overrides a different requested concept. */
    var population_with_gistemp_alias = AskTheModel.ChartIntent.parse (
        "Generate a GISTEMP chart of population over time."
    );
    assert (population_with_gistemp_alias.series_id == null);
    assert (population_with_gistemp_alias.intent == "ambiguous");

    var population_with_ei_alias = AskTheModel.ChartIntent.parse (
        "Generate an Energy Institute chart of population over time."
    );
    assert (population_with_ei_alias.series_id == null);
    assert (population_with_ei_alias.intent == "ambiguous");

    var population_with_bnr_alias = AskTheModel.ChartIntent.parse (
        "Generate a chart of BNR credit standards for population over time."
    );
    assert (population_with_bnr_alias.series_id == null);
    assert (population_with_bnr_alias.intent == "ambiguous");

    /* The current line adapters always render the full catalogued coverage. */
    var gistemp_year_range = AskTheModel.ChartIntent.parse (
        "Generate a GISTEMP chart from 2000 to 2020."
    );
    assert (gistemp_year_range.series_id == null);
    assert (gistemp_year_range.intent == "unsupported_period");

    var gistemp_recent_period = AskTheModel.ChartIntent.parse (
        "Generate a GISTEMP chart for the last 10 years."
    );
    assert (gistemp_recent_period.series_id == null);
    assert (gistemp_recent_period.intent == "unsupported_period");

    var ei_romanian_period = AskTheModel.ChartIntent.parse (
        "Generează un grafic Energy Institute între 2000 și 2020."
    );
    assert (ei_romanian_period.series_id == null);
    assert (ei_romanian_period.intent == "unsupported_period");

    var explicit_year_range = AskTheModel.ChartIntent.parse (
        "/chart gistemp from 2000 to 2020"
    );
    assert (explicit_year_range.series_id == null);
    assert (explicit_year_range.intent == "unsupported_period");

    var explicit_romanian_year_range = AskTheModel.ChartIntent.parse (
        "/chart energy-institute între 2000 și 2020"
    );
    assert (explicit_romanian_year_range.series_id == null);
    assert (explicit_romanian_year_range.intent == "unsupported_period");

    /* A matching alias must not override a conflicting geographic/subject filter. */
    var gistemp_regional_filter = AskTheModel.ChartIntent.parse (
        "Generate a chart of GISTEMP global temperature anomalies for Romania."
    );
    assert (gistemp_regional_filter.series_id == null);
    assert (gistemp_regional_filter.intent == "ambiguous");

    var energy_regional_filter = AskTheModel.ChartIntent.parse (
        "Generate a chart of Energy Institute total energy supply for Europe."
    );
    assert (energy_regional_filter.series_id == null);
    assert (energy_regional_filter.intent == "ambiguous");

    var bnr_subject_filter = AskTheModel.ChartIntent.parse (
        "Generate a chart of BNR credit standards for households."
    );
    assert (bnr_subject_filter.series_id == null);
    assert (bnr_subject_filter.intent == "ambiguous");

    return 0;
}
