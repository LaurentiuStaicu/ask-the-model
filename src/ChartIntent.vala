namespace AskTheModel {
    public enum ChartKind {
        NONE,
        GISTEMP
    }

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

        public static bool is_chart_follow_up (string prompt) {
            string normalized = prompt.strip ().down ();
            return normalized == "generate the chart" ||
                normalized == "show the chart" ||
                normalized == "display the chart" ||
                normalized == "create the chart" ||
                normalized == "draw the chart" ||
                normalized == "plot the chart" ||
                normalized == "generează graficul" ||
                normalized == "genereaza graficul" ||
                normalized == "arată graficul" ||
                normalized == "arata graficul" ||
                normalized == "afișează graficul" ||
                normalized == "afiseaza graficul" ||
                normalized == "creează graficul" ||
                normalized == "creeaza graficul" ||
                normalized == "desenează graficul" ||
                normalized == "deseneaza graficul" ||
                normalized == "vizualizează graficul" ||
                normalized == "vizualizeaza graficul";
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
