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
                    "\\nThe native Chart/Data widget displays the complete admitted " +
                    "Energy Institute TES series. For a chart request, give a brief " +
                    "one- or two-sentence introduction and direct the user to the Chart/Data " +
                    "tabs for the full series and provenance. Do not reproduce a data table " +
                    "or individual year values unless the user explicitly asks for them. " +
                    "Never treat a partial repository evidence excerpt as the chart's complete " +
                    "series, and never claim other years are unavailable merely because they " +
                    "do not appear in an excerpt. Do not contradict the admitted chart's " +
                    "time range, source interpretation, or units.";
            }
            if (gistemp_requested) {
                return
                    "\\nThe native Chart/Data widget displays the qualified GISTEMP series. " +
                    "For a chart request, give a brief one- or two-sentence introduction and " +
                    "direct the user to the Chart/Data tabs for the full series and provenance. " +
                    "Do not reproduce a data table or individual year values unless the user " +
                    "explicitly asks for them. Do not infer that years are missing from partial " +
                    "repository evidence excerpts or contradict the admitted chart's time range.";
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
