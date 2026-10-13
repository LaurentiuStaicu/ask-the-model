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
