namespace AskTheModel {
    /*
     * Explicit chart intent contract. The model never supplies chart intent,
     * values, axes, units, or domains. A live GISTEMP chart is requested only
     * by the exact command prefix "/chart gistemp"; the remaining text is the
     * ordinary grounded user question.
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

        public static ChartIntent parse (string prompt) {
            string trimmed = prompt.strip ();
            string prefix = "/chart gistemp";

            if (!trimmed.down ().has_prefix (prefix)) {
                return new ChartIntent (false, prompt);
            }

            if (trimmed.length == prefix.length) {
                return new ChartIntent (true, "");
            }

            char separator = trimmed[prefix.length];
            if (separator != ' ' && separator != '\t') {
                return new ChartIntent (false, prompt);
            }

            return new ChartIntent (
                true,
                trimmed.substring (prefix.length).strip ()
            );
        }
    }
}
