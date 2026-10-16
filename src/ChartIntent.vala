namespace AskTheModel {
    /*
     * Chart intent is resolved locally. The model never supplies chart intent,
     * values, axes, units, or domains. Each admitted chart source has an
     * explicit command; natural language must identify exactly one source.
     */
    public class ChartIntent : Object {
        public bool gistemp_requested { get; private set; }
        public bool energy_institute_requested { get; private set; }
        public bool bnr_bls_c01_requested { get; private set; }
        public string? series_id { get; private set; }
        public string intent { get; private set; }
        public string response_mode { get; private set; }
        public string query { get; private set; }

        /*
         * Chart data and provenance are rendered by the qualified native
         * Chart/Data widget. The conversational answer must not substitute
         * partial retrieval excerpts for that complete admitted series.
         */
        public string response_guidance () {
            if (bnr_bls_c01_requested) {
                return
                    "\nThe application attempts to construct the qualified BNR BLS C01 quarterly " +
                    "credit-standards net-percentage chart from its pinned RMD snapshot. The " +
                    "Chart/Data widget presents the complete qualified quarter sequence, exact " +
                    "values and provenance. The 2025-Q2 observation is explicitly MISSING, not " +
                    "zero; do not impute it, invent values, or join a line across that gap. " +
                    "For chart-only requests, do not substitute partial retrieval excerpts for " +
                    "the native chart or present them as a pseudo-table. Only give individual " +
                    "values or a table when explicitly requested.";
            }
            if (energy_institute_requested) {
                return
                    "\nThe application attempts to construct the qualified Energy Institute TES " +
                    "chart separately in its native Chart/Data widget. The widget, not the " +
                    "prose answer, presents the full admitted series and source provenance. " +
                    "For a chart-only request, do not reproduce chart data in prose: do not " +
                    "list individual years or values, and do not format partial observations " +
                    "as a table or pseudo-table. Do not claim that the chart cannot be rendered " +
                    "or that more years are unavailable based only on a short retrieved excerpt. " +
                    "Only provide year-by-year values or a table when the user explicitly asks " +
                    "for them. For additional interpretation requests, do not contradict the " +
                    "qualified chart's time range, interpretation, or units.";
            }
            if (gistemp_requested) {
                return
                    "\nThe application attempts to construct the qualified GISTEMP chart " +
                    "separately in its native Chart/Data widget. The widget, not the prose " +
                    "answer, presents the full admitted series and source provenance. For a " +
                    "chart-only request, do not reproduce chart data in prose: do not list " +
                    "individual years or temperatures, and do not format partial observations " +
                    "as a table or pseudo-table. Do not claim that the chart cannot be rendered " +
                    "or that more years are unavailable based only on a short retrieved excerpt. " +
                    "Only provide year-by-year values or a table when the user explicitly asks " +
                    "for them. For additional interpretation requests, do not contradict the " +
                    "qualified chart's scope or time range.";
            }
            return "";
        }

        /*
         * A chart command can be accompanied by a substantive question. Only
         * replace the model's prose when the request asks for a visualization
         * without also asking for interpretation or a textual data extract.
         */
        public bool is_chart_only_request () {
            string normalized = query.down ().strip ();
            if (normalized.length == 0) {
                return false;
            }

            return !contains_any (
                normalized,
                {
                    "explain", "interpret", "analyze", "analyse", "compare",
                    "summarize", "summarise", "describe", "discuss", "why",
                    "what does", "what do", "what is", "what are", "what was",
                    "what were", "what's", "what can we learn", "tell me",
                    "does the", "do the", "is there", "are there", "was there",
                    "were there", "how does", "how do", "when was", "when were",
                    "where was", "which", "meaning", "implication", "insight",
                    "evaluate", "assess",
                    "calculate", "table", "values", "value", "list", "give me",
                    "which year", "when did", "how much", "how has", "how did",
                    "what happened", "data say", "analysis", "explanation",
                    "explică", "explica", "interpretează", "interpreteaza",
                    "analizează", "analizeaza", "compară", "compara",
                    "rezumă", "rezuma", "descrie", "discută", "discuta",
                    "de ce", "ce este", "ce sunt", "care este", "care sunt",
                    "când", "cand", "cum", "ce înseamnă", "ce inseamna",
                    "ce ne spune", "ce putem învăța", "ce putem invata",
                    "arată valorile",
                    "arata valorile", "tabel", "valorile", "valoare",
                    "listează", "listeaza", "calculează", "calculeaza",
                    "care an", "în ce an", "in ce an", "cât", "cat",
                    "analiză", "analiza", "interpretare", "ce observăm",
                    "ce observam", "ce s-a întâmplat", "ce s-a intamplat"
                }
            );
        }

        /*
         * Chart-only replies are deterministic so short retrieval excerpts
         * cannot replace the native chart with a partial table. Chart
         * construction is qualified separately after the durable turn commit;
         * the UI reports a chart status if the widget cannot be constructed.
         */
        public string chart_only_response () {
            if (!is_chart_only_request ()) {
                return "";
            }

            bool romanian = contains_any (query.down (), {
                "grafic", "arată", "arata", "generează", "genereaza",
                "creează", "creeaza", "afișează", "afiseaza",
                "temperatura globală", "temperatura globala",
                "furnizarea", "valorile", "în timp", "in timp",
                "standarde de credit", "standardele de credit", "procent net"
            });

            if (bnr_bls_c01_requested) {
                return romanian
                    ? "Cerere de grafic: BNR BLS C01 trimestrial pentru procentul net al standardelor de credit. Consultă vizualizarea Data calificată pentru valorile exacte și detaliile sursei când este disponibilă; 2025-Q2 lipsește, nu este zero. Dacă graficul nu poate fi construit, AtM raportează separat problema și nu substituie valori."
                    : "Requested chart: BNR BLS C01 quarterly credit-standards net-percentage chart. See the qualified Data view for exact values and source details when available; 2025-Q2 is missing, not zero. If chart construction fails, AtM reports that separately and does not substitute values.";
            }
            if (energy_institute_requested) {
                return romanian
                    ? "Cerere de grafic: Energy Institute pentru furnizarea totală globală de energie. Valorile anuale exacte și detaliile sursei apar în vizualizarea Chart/Data calificată când este disponibilă; dacă aceasta nu poate fi construită, AtM raportează separat problema și nu substituie valori."
                    : "Requested chart: Energy Institute global total energy supply chart. Exact annual values and source details appear in the qualified Chart/Data view when available; if construction fails, AtM reports that separately rather than substituting values.";
            }
            if (gistemp_requested) {
                return romanian
                    ? "Cerere de grafic: GISTEMP pentru anomaliile temperaturii globale. Valorile exacte și detaliile sursei apar în vizualizarea Chart/Data calificată când este disponibilă; dacă aceasta nu poate fi construită, AtM raportează separat problema și nu substituie valori."
                    : "Requested chart: GISTEMP global temperature anomaly chart. Exact values and source details appear in the qualified Chart/Data view when available; if construction fails, AtM reports that separately rather than substituting values.";
            }
            return "";
        }

        private ChartIntent (
            bool gistemp_requested,
            bool energy_institute_requested,
            bool bnr_bls_c01_requested,
            string query,
            string? parsed_intent = null
        ) {
            Object ();
            this.gistemp_requested = gistemp_requested;
            this.energy_institute_requested = energy_institute_requested;
            this.bnr_bls_c01_requested = bnr_bls_c01_requested;
            this.query = query;
            this.series_id = gistemp_requested
                ? "climate.gistemp.global-temperature-anomaly"
                : (energy_institute_requested
                    ? "energy.energy-institute.global-total-energy-supply"
                    : (bnr_bls_c01_requested
                        ? "banking.bnr.bls-c01.credit-standards-net-percentage"
                        : null));
            this.intent = parsed_intent ??
                (this.series_id == null ? "not_chart" : "chart");
            this.response_mode = this.series_id == null
                ? "none"
                : (is_chart_only_request () ? "chart_only" : "chart_and_text");
        }

        private static bool contains_any (
            string text,
            string[] terms
        ) {
            foreach (string term in terms) {
                if (text.index_of (term) >= 0) {
                    return true;
                }
            }

            return false;
        }

        private static bool contains_bounded_any (
            string text,
            string[] terms
        ) {
            foreach (string term in terms) {
                if (ChartCatalog.text_has_phrase (text, term)) {
                    return true;
                }
            }
            return false;
        }

        /*
         * These guards run before alias selection. A recognized catalog alias
         * is only an entity match; negated, hypothetical, quoted-language and
         * capability-only prompts are not chart authorization.
         */
        private static bool is_explicitly_negated_chart_request (string normalized) {
            return contains_any (normalized, {
                /* Allow the series name to occur between negation and chart noun. */
                "do not make", "do not generate", "do not create",
                "do not draw", "do not plot", "do not show",
                "do not display", "do not visualize", "do not visualise",
                "do not want", "do not need", "never generate",
                "never create", "never show", "avoid generating",
                "avoid creating", "without generating", "without creating",
                "don't make", "don't generate", "don't create",
                "don't draw", "don't plot", "don't show", "don't display",
                "don't visualize", "don't visualise", "don't want",
                "don't need",
                "dont make", "dont generate", "dont create", "dont show",
                "dont display", "dont want", "dont need",
                "not generate", "not create",
                "not asking for a chart", "not asking for chart",
                "not a chart request", "no chart", "without a chart",
                "without chart",
                "nu genera", "nu crea", "nu creea", "nu desena",
                "nu arăta", "nu arata", "nu afișa", "nu afisa",
                "nu vizualiza", "nu afișezi", "nu afisezi",
                "nu vreau un grafic", "nu vreau grafic",
                "nu vreau să generezi", "nu vreau sa generezi",
                "nu vreau să creezi", "nu vreau sa creezi",
                "nu-mi genera", "nu imi genera",
                "nu face un grafic", "nu face grafic",
                "nu este o cerere de grafic", "nu cer un grafic",
                "fara grafic", "fără grafic"
            });
        }

        private static bool is_hypothetical_chart_request (string normalized) {
            return contains_any (normalized, {
                "what if", "hypothetically", "hypothetical", "suppose we",
                "suppose i", "imagine if", "if i were to", "if we were to",
                "if i wanted to", "if one wanted to",
                "dacă aș genera", "daca as genera", "dacă aș cere",
                "daca as cere", "presupunând că", "presupunand ca",
                "ipotetic", "imaginează-ți dacă", "imagineaza-ti daca"
            });
        }

        private static bool asks_about_quoted_instruction (string normalized) {
            return contains_any (normalized, {
                "the phrase", "this phrase", "that phrase", "the sentence",
                "this sentence", "the wording", "quoted instruction",
                "is the phrase", "does the sentence", "what does the phrase",
                "expresia", "fraza", "propozitia", "propoziția",
                "formularea", "citatul", "ce înseamnă expresia",
                "ce inseamna expresia"
            });
        }

        private static bool is_capability_only_chart_question (string normalized) {
            return contains_any (normalized, {
                "can you generate charts", "can you make charts",
                "can you create charts", "do you support charts",
                "do you support charting", "are you able to generate charts",
                "are you able to create charts", "do you know how to make charts",
                "can this app generate charts", "does this app support charts",
                "poți genera grafice", "poti genera grafice",
                "poți crea grafice", "poti crea grafice",
                "poți să generezi grafice", "poti sa generezi grafice",
                "știi să faci grafice", "stii sa faci grafice",
                "aplicația poate genera grafice", "aplicatia poate genera grafice"
            });
        }

        private static bool requests_comparison (string normalized) {
            return contains_any (
                normalized,
                {
                    "compare", "comparing", "comparison", "versus", " vs ", "vs.",
                    "against", "relative to", "in relation to",
                    "compară", "compara", "comparând", "comparand",
                    "în comparație", "in comparatie", "în raport cu",
                    "in raport cu", "față de", "fata de"
                }
            );
        }

        private static bool requests_additional_series (string normalized) {
            /*
             * The first route is single-series only. Use the catalog's
             * normalized phrase boundary helper for unsupported indicators so
             * short words never match inside unrelated text.
             */
            string[] unsupported_series_terms = {
                "gdp", "gross domestic product", "pib",
                "unemployment", "șomaj", "șomajului", "somaj", "somajului",
                "inflation", "inflație", "inflației", "inflatie", "inflatiei",
                "population", "populație", "populatie",
                "populației", "populatiei",
                "carbon dioxide", "co2", "emissions", "emisii",
                "renewable energy", "fossil fuel", "fossil fuels",
                "energie regenerabilă", "energie regenerabila",
                "combustibili fosili", "non-performing loan",
                "creditele neperformante", "creditelor neperformante"
            };
            foreach (string term in unsupported_series_terms) {
                if (ChartCatalog.text_has_phrase (normalized, term)) {
                    return true;
                }
            }
            return false;
        }

        private static bool requests_derived_series (string normalized) {
            return contains_any (
                normalized,
                {
                    "standard deviation", "moving average", "moving-average",
                    "forecast", "forecasting", "forecasted",
                    "projection", "projected values",
                    "correlation", "correlated",
                    "abaterea standard", "abatere standard",
                    "medie mobilă", "medie mobila",
                    "media mobilă", "media mobila",
                    "prognoză", "prognoza",
                    "proiecție", "proiectie",
                    "corelație", "corelatie"
                }
            );
        }

        private static bool command_query_is_compatible (
            string query,
            string selected_series_id
        ) {
            string normalized = query.down ();
            if (query.strip ().length == 0) {
                return true;
            }
            if (is_explicitly_negated_chart_request (normalized) ||
                is_hypothetical_chart_request (normalized) ||
                asks_about_quoted_instruction (normalized) ||
                is_capability_only_chart_question (normalized) ||
                requests_comparison (normalized) ||
                requests_additional_series (normalized) ||
                requests_derived_series (normalized)) {
                return false;
            }

            bool ambiguous = false;
            string? query_series_id =
                ChartCatalog.match_request_series_id (query, out ambiguous);
            if (ambiguous) {
                return false;
            }
            return query_series_id == null ||
                query_series_id == selected_series_id;
        }

        private static bool command_matches (
            string trimmed,
            string prefix
        ) {
            if (!trimmed.down ().has_prefix (prefix)) {
                return false;
            }
            if (trimmed.length == prefix.length) {
                return true;
            }
            char separator = trimmed[prefix.length];
            return separator == ' ' || separator == '\t';
        }

        private static string command_query (string trimmed, string prefix) {
            if (trimmed.length == prefix.length) {
                return "";
            }
            return trimmed.substring (prefix.length).strip ();
        }

        public static ChartIntent parse (string prompt) {
            string trimmed = prompt.strip ();
            string normalized = prompt.down ();
            const string gistemp_prefix = "/chart gistemp";
            const string ei_prefix = "/chart energy-institute";
            const string bnr_prefix = "/chart bnr-bls-c01";

            if (command_matches (trimmed, gistemp_prefix)) {
                string query = command_query (trimmed, gistemp_prefix);
                if (!command_query_is_compatible (
                        query,
                        "climate.gistemp.global-temperature-anomaly")) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }
                return new ChartIntent (true, false, false, query);
            }
            if (command_matches (trimmed, ei_prefix)) {
                string query = command_query (trimmed, ei_prefix);
                if (!command_query_is_compatible (
                        query,
                        "energy.energy-institute.global-total-energy-supply")) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }
                return new ChartIntent (false, true, false, query);
            }
            if (command_matches (trimmed, bnr_prefix)) {
                string query = command_query (trimmed, bnr_prefix);
                if (!command_query_is_compatible (
                        query,
                        "banking.bnr.bls-c01.credit-standards-net-percentage")) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }
                return new ChartIntent (false, false, true, query);
            }

            /* A malformed slash command must not fall through to natural
             * language detection merely because it contains a source name. */
            if (trimmed.down ().has_prefix (gistemp_prefix) ||
                trimmed.down ().has_prefix (ei_prefix) ||
                trimmed.down ().has_prefix (bnr_prefix)) {
                return new ChartIntent (false, false, false, prompt);
            }

            if (is_explicitly_negated_chart_request (normalized) ||
                is_hypothetical_chart_request (normalized) ||
                asks_about_quoted_instruction (normalized) ||
                is_capability_only_chart_question (normalized)) {
                return new ChartIntent (
                    false, false, false, prompt, "not_chart"
                );
            }

            bool asks_for_chart = contains_bounded_any (
                normalized,
                {
                    "grafic", "chart", "graph", "plot", "diagram"
                }
            );
            bool requests_visualization = contains_bounded_any (
                normalized,
                {
                    "arată", "arata", "generează", "genereaza",
                    "creează", "creeaza", "desenează", "deseneaza",
                    "afișează", "afiseaza", "vizualizează", "vizualizeaza",
                    "show", "generate", "create", "draw", "make", "plot",
                    "visualize", "visualise"
                }
            );

            if (asks_for_chart && requests_visualization) {
                bool ambiguous = false;
                string? selected_series_id =
                    ChartCatalog.match_request_series_id (prompt, out ambiguous);

                if (ambiguous) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }

                if (selected_series_id == null) {
                    return new ChartIntent (
                        false, false, false, prompt, "unsupported"
                    );
                }

                /*
                 * The first generic slice supports one native line series only.
                 * Reject comparison/multiseries/derived requests before routing,
                 * even when exactly one registered alias appears in the text.
                 */
                if (requests_comparison (normalized) ||
                    requests_additional_series (normalized) ||
                    requests_derived_series (normalized)) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }

                if (selected_series_id ==
                    "climate.gistemp.global-temperature-anomaly") {
                    return new ChartIntent (true, false, false, prompt);
                }
                if (selected_series_id ==
                    "energy.energy-institute.global-total-energy-supply") {
                    return new ChartIntent (false, true, false, prompt);
                }
                if (selected_series_id ==
                    "banking.bnr.bls-c01.credit-standards-net-percentage") {
                    return new ChartIntent (false, false, true, prompt);
                }
            }

            return new ChartIntent (false, false, false, prompt);
        }
    }
}
