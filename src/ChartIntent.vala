namespace AskTheModel {
    /*
     * Chart intent is resolved locally. The model never supplies chart intent,
     * values, axes, units, or domains. Each admitted chart source has an
     * explicit command; natural language must identify exactly one source.
     */
    public class ChartIntent : Object {
        public bool gistemp_requested { get; private set; }
        public bool energy_institute_requested { get; private set; }
        public string query { get; private set; }

        /*
         * Chart data and provenance are rendered by the qualified native
         * Chart/Data widget. The conversational answer must not substitute
         * partial retrieval excerpts for that complete admitted series.
         */
        public string response_guidance () {
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
         * Chart-only replies are deterministic. The model must not produce a
         * partial table when the native chart is absent, nor claim success if
         * the chart widget could not be constructed.
         */
        public string chart_only_response () {
            if (!is_chart_only_request ()) {
                return "";
            }

            bool romanian = contains_any (query.down (), {
                "grafic", "arată", "arata", "generează", "genereaza",
                "creează", "creeaza", "afișează", "afiseaza",
                "temperatura globală", "temperatura globala",
                "furnizarea", "valorile", "în timp", "in timp"
            });

            if (energy_institute_requested) {
                return romanian
                    ? "Iată graficul Energy Institute al furnizării totale globale de energie. Consultă fila Data pentru valorile anuale și proveniența surselor."
                    : "Here is the Energy Institute global total energy supply chart. See the Data tab for the underlying annual values and source provenance.";
            }
            if (gistemp_requested) {
                return romanian
                    ? "Iată graficul GISTEMP al anomaliilor temperaturii globale. Consultă fila Data pentru valorile și proveniența surselor."
                    : "Here is the GISTEMP global temperature anomaly chart. See the Data tab for the underlying values and source provenance.";
            }
            return "";
        }

        private ChartIntent (
            bool gistemp_requested,
            bool energy_institute_requested,
            string query
        ) {
            Object ();
            this.gistemp_requested = gistemp_requested;
            this.energy_institute_requested = energy_institute_requested;
            this.query = query;
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

            if (command_matches (trimmed, gistemp_prefix)) {
                return new ChartIntent (
                    true,
                    false,
                    command_query (trimmed, gistemp_prefix)
                );
            }
            if (command_matches (trimmed, ei_prefix)) {
                return new ChartIntent (
                    false,
                    true,
                    command_query (trimmed, ei_prefix)
                );
            }

            /* A malformed slash command must not fall through to natural
             * language detection merely because it contains a source name. */
            if (trimmed.down ().has_prefix (gistemp_prefix) ||
                trimmed.down ().has_prefix (ei_prefix)) {
                return new ChartIntent (false, false, prompt);
            }

            bool asks_for_chart = contains_any (
                normalized,
                {
                    "grafic",
                    "chart",
                    "plot",
                    "diagram"
                }
            );
            bool requests_visualization = contains_any (
                normalized,
                {
                    "arată",
                    "arata",
                    "generează",
                    "genereaza",
                    "creează",
                    "creeaza",
                    "desenează",
                    "deseneaza",
                    "afișează",
                    "afiseaza",
                    "vizualizează",
                    "vizualizeaza",
                    "show",
                    "generate",
                    "create",
                    "draw",
                    "make",
                    "plot"
                }
            );
            bool names_gistemp_series = contains_any (
                normalized,
                {
                    "gistemp",
                    "temperatura globala",
                    "temperatura globală",
                    "temperaturii globale",
                    "global temperature"
                }
            );
            bool names_energy_institute_series = contains_any (
                normalized,
                {
                    "energy institute",
                    "total energy supply",
                    "global total energy supply",
                    "physical energy content",
                    "furnizarea totală de energie",
                    "furnizarea totala de energie",
                    "oferta totală de energie",
                    "oferta totala de energie"
                }
            );

            if (asks_for_chart && requests_visualization) {
                if (names_gistemp_series && !names_energy_institute_series) {
                    return new ChartIntent (true, false, prompt);
                }
                if (names_energy_institute_series && !names_gistemp_series) {
                    return new ChartIntent (false, true, prompt);
                }
            }

            return new ChartIntent (false, false, prompt);
        }
    }
}
