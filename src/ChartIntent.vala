namespace AskTheModel {
    /*
     * Chart intent is resolved locally. The model never supplies chart intent,
     * values, axes, units, or domains. GISTEMP requests may use the explicit
     * command or an unambiguous natural-language request for a GISTEMP/global
     * temperature chart.
     */
    public class ChartIntent : Object {
        public bool gistemp_requested { get; private set; }
        public string query { get; private set; }

        private ChartIntent (
            bool gistemp_requested,
            string query
        ) {
            Object ();
            this.gistemp_requested = gistemp_requested;
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

        public static ChartIntent parse (string prompt) {
            string trimmed = prompt.strip ();
            string normalized = prompt.down ();
            string prefix = "/chart gistemp";

            if (trimmed.down ().has_prefix (prefix)) {
                if (trimmed.length == prefix.length) {
                    return new ChartIntent (true, "");
                }

                char separator = trimmed[prefix.length];
                if (separator == ' ' || separator == '\t') {
                    return new ChartIntent (
                        true,
                        trimmed.substring (prefix.length).strip ()
                    );
                }
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

            if (asks_for_chart &&
                names_gistemp_series &&
                requests_visualization) {
                return new ChartIntent (true, prompt);
            }

            return new ChartIntent (false, prompt);
        }
    }
}
