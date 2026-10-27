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
            if (series_id == null) {
                return "";
            }
            return ChartCatalog.response_guidance (series_id) ?? "";
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
                    "does this app support", "does atm support",
                    "the phrase", "this phrase", "that phrase", "the wording",
                    "quoted instruction", "example",
                    "aplicația poate genera grafice",
                    "aplicatia poate genera grafice",
                    "aplicația poate crea grafice",
                    "aplicatia poate crea grafice",
                    "ce tipuri de grafice", "fraza", "expresia", "citatul",
                    "formularea", "exemplu",
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
            if (!is_chart_only_request () || series_id == null) {
                return "";
            }

            /*
             * User-facing text is composed from the same immutable catalog
             * metadata as the native Chart/Data attribution. Do not maintain
             * a separate source-specific prose branch here.
             */
            bool romanian = contains_any (query.down (), {
                "grafic", "arată", "arata", "generează", "genereaza",
                "creează", "creeaza", "afișează", "afiseaza",
                "vizualizează", "vizualizeaza", "în timp", "in timp",
                "valorile", "detaliile sursei", "lipsește", "lipseste"
            });
            return ChartCatalog.chart_only_response (
                series_id, romanian
            ) ?? "";
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

        /*
         * Catalog-selected construction is the generic route. The booleans are
         * retained as legacy compatibility accessors for existing callers and
         * tests; the stable catalog ID is the routing authority.
         */
        private ChartIntent.for_catalog_series (
            string series_id,
            string query
        ) {
            Object ();
            this.series_id = series_id;
            this.gistemp_requested =
                series_id == "climate.gistemp.global-temperature-anomaly";
            this.energy_institute_requested =
                series_id == "energy.energy-institute.global-total-energy-supply";
            this.bnr_bls_c01_requested =
                series_id == "banking.bnr.bls-c01.credit-standards-net-percentage";
            this.query = query;
            this.intent = "chart";
            this.response_mode = is_chart_only_request ()
                ? "chart_only"
                : "chart_and_text";
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

        private static bool starts_with_any (
            string text,
            string[] terms
        ) {
            string trimmed = text.strip ();
            foreach (string term in terms) {
                if (trimmed.has_prefix (term)) {
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
            return starts_with_any (normalized, {
                "can you generate charts", "can you make charts",
                "can you create charts", "do you support charts",
                "do you support charting", "are you able to generate charts",
                "are you able to create charts", "do you know how to make charts",
                "does this app support charts", "does this app support creating charts",
                "does this app support generating charts", "does atm support charts",
                "does atm support charting", "does atm support creating gistemp charts",
                "can this app generate charts", "does this app generate charts",
                "what chart types do you support", "which chart types do you support",
                "what chart types can you create", "what charts can you create",
                "which charts can you create", "what kinds of charts can you generate",
                "how can i generate a chart", "how do i make a chart",
                "how to generate a chart", "how to create a chart",
                "poți genera grafice", "poti genera grafice",
                "poți crea grafice", "poti crea grafice",
                "poți să generezi grafice", "poti sa generezi grafice",
                "știi să faci grafice", "stii sa faci grafice",
                "aplicația poate genera grafice", "aplicatia poate genera grafice",
                "aplicația poate crea grafice", "aplicatia poate crea grafice",
                "ce tipuri de grafice poți genera", "ce tipuri de grafice poti genera"
            });
        }

        private static bool is_existing_chart_interpretation_question (
            string normalized
        ) {
            return starts_with_any (normalized, {
                "what does the chart show", "what does this chart show",
                "what does that chart show", "what does the graph show",
                "what does this graph show", "what is shown in the chart",
                "what is shown by the chart", "what does the chart tell us",
                "what can we learn from the chart", "tell me what the chart shows",
                "what does the gistemp chart show",
                "what does the energy institute chart show",
                "what does the bnr bls c01 chart show",
                "what does the chart mean", "what does this chart mean",
                "ce arată graficul", "ce arata graficul",
                "ce ne arată graficul", "ce ne arata graficul",
                "ce arată graficul gistemp", "ce arata graficul gistemp",
                "ce înseamnă graficul", "ce inseamna graficul",
                "ce ne spune graficul", "ce ne spune graficul gistemp",
                "cum interpretez graficul", "cum interpretăm graficul",
                "cum interpretam graficul"
            });
        }

        private static bool requests_comparison (string normalized) {
            return contains_any (
                normalized,
                {
                    "compare", "comparing", "comparison", "versus", " vs ", "vs.",
                    "against",
                    "compară", "compara", "comparând", "comparand",
                    "în comparație", "in comparatie"
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


        private static bool baseline_range_used_as_observation_filter (
            string request_text
        ) {
            /*
             * 1951–1980 is valid GISTEMP reference metadata only when it is
             * not also selected as the observation range. A prompt can mention
             * the baseline while independently asking to filter observations
             * to that same span; baseline wording must not erase that filter.
             */
            string[] patterns = {
                "\\b(from|since|after|before|during|between|only|starting in|beginning in|for the period|in the period)\\s+(the\\s+)?1951\\s*[-–—]\\s*1980\\b",
                "\\b(from|since|after|before|during|between|only|starting in|beginning in|for the period|in the period)\\s+(the\\s+)?1951\\s+(to|through|until|and)\\s+1980\\b",
                "\\b(doar în intervalul|doar in intervalul|numai în intervalul|numai in intervalul|în intervalul|in intervalul|în perioada|in perioada|pe perioada|din|între|intre|doar în|doar in|numai în|numai in)\\s+(anii\\s+)?1951\\s*[-–—]\\s*1980\\b",
                "\\b(doar în intervalul|doar in intervalul|numai în intervalul|numai in intervalul|în intervalul|in intervalul|în perioada|in perioada|pe perioada|din|între|intre|doar în|doar in|numai în|numai in)\\s+(anii\\s+)?1951\\s+(până în|pana in|și|si|până la|pana la|to|through|until|and)\\s+1980\\b"
            };

            foreach (string pattern in patterns) {
                if (Regex.match_simple (
                        pattern,
                        request_text,
                        (RegexCompileFlags) 0,
                        (RegexMatchFlags) 0)) {
                    return true;
                }
            }
            return false;
        }

        private static bool requests_explicit_period_filter (
            string normalized,
            string selected_series_id
        ) {
            /*
             * All three current adapters render the complete catalogued
             * coverage; custom period filtering is not implemented. Explicitly
             * exempt only the exact catalog coverage range for the selected
             * series and a qualified GISTEMP reference baseline. Do not let any
             * other 18xx/19xx/20xx dashed range pass by accident.
             */
            string request_text = normalized.down ();
            string range_check = request_text;

            if (selected_series_id ==
                "climate.gistemp.global-temperature-anomaly") {
                bool has_reference_baseline = contains_bounded_any (
                    request_text, {
                        "baseline", "reference period", "reference mean",
                        "reference baseline", "relative to", "compared with",
                        "mean", "perioada de referinta",
                        "perioada de referință", "perioada de referinta",
                        "raportat la", "raportată la", "raportata la",
                        "media de referință", "media de referinta"
                    });
                if (has_reference_baseline &&
                    !baseline_range_used_as_observation_filter (request_text)) {
                    range_check = range_check
                        .replace ("1951-1980", "")
                        .replace ("1951–1980", "")
                        .replace ("1951—1980", "")
                        .replace ("1951 - 1980", "")
                        .replace ("1951 – 1980", "")
                        .replace ("1951 — 1980", "");
                }

                range_check = range_check
                    .replace ("1880-2025", "")
                    .replace ("1880–2025", "")
                    .replace ("1880—2025", "")
                    .replace ("1880 - 2025", "")
                    .replace ("1880 – 2025", "")
                    .replace ("1880 — 2025", "")
                    .replace ("from 1880 to 2025", "")
                    .replace ("from 1880 through 2025", "")
                    .replace ("from 1880 until 2025", "")
                    .replace ("1880 to 2025", "")
                    .replace ("1880 through 2025", "")
                    .replace ("1880 until 2025", "")
                    .replace ("between 1880 and 2025", "")
                    .replace ("între 1880 și 2025", "")
                    .replace ("între 1880 si 2025", "")
                    .replace ("intre 1880 și 2025", "")
                    .replace ("intre 1880 si 2025", "")
                    .replace ("din 1880 până în 2025", "")
                    .replace ("din 1880 pana in 2025", "");
            } else if (selected_series_id ==
                       "energy.energy-institute.global-total-energy-supply") {
                range_check = range_check
                    .replace ("1965-2025", "")
                    .replace ("1965–2025", "")
                    .replace ("1965—2025", "")
                    .replace ("1965 - 2025", "")
                    .replace ("1965 – 2025", "")
                    .replace ("1965 — 2025", "")
                    .replace ("from 1965 to 2025", "")
                    .replace ("from 1965 through 2025", "")
                    .replace ("from 1965 until 2025", "")
                    .replace ("1965 to 2025", "")
                    .replace ("1965 through 2025", "")
                    .replace ("1965 until 2025", "")
                    .replace ("between 1965 and 2025", "");
            } else if (selected_series_id ==
                       "banking.bnr.bls-c01.credit-standards-net-percentage") {
                /*
                 * The BNR adapter exposes this complete quarterly interval.
                 * Filtering a subset is still unsupported. Remove only exact
                 * spellings of the complete interval, so a second/partial
                 * period in the same request remains visible to the guards.
                 */
                range_check = range_check
                    .replace ("2022-q4-2025-q3", "")
                    .replace ("2022-q4–2025-q3", "")
                    .replace ("2022-q4—2025-q3", "")
                    .replace ("2022-q4 - 2025-q3", "")
                    .replace ("2022-q4 – 2025-q3", "")
                    .replace ("2022-q4 — 2025-q3", "")
                    .replace ("from 2022-q4 to 2025-q3", "")
                    .replace ("from 2022-q4 through 2025-q3", "")
                    .replace ("from 2022-q4 until 2025-q3", "")
                    .replace ("2022-q4 to 2025-q3", "")
                    .replace ("2022-q4 through 2025-q3", "")
                    .replace ("2022-q4 until 2025-q3", "")
                    .replace ("between 2022-q4 and 2025-q3", "")
                    .replace ("între 2022-q4 și 2025-q3", "")
                    .replace ("între 2022-q4 si 2025-q3", "")
                    .replace ("intre 2022-q4 și 2025-q3", "")
                    .replace ("intre 2022-q4 si 2025-q3", "")
                    .replace ("din 2022-q4 până în 2025-q3", "")
                    .replace ("din 2022-q4 pana in 2025-q3", "");
            }

            bool has_dashed_year_range = Regex.match_simple (
                "\\b(18|19|20)[0-9]{2}\\s*[-–—]\\s*(18|19|20)[0-9]{2}\\b",
                range_check,
                (RegexCompileFlags) 0,
                (RegexMatchFlags) 0
            );
            if (has_dashed_year_range) {
                return true;
            }

            string[] patterns = {
                "\\b(from|since|after|before)\\s+(18|19|20)[0-9]{2}\\b",
                "\\b(until|through|up to|upto|as of|as at)\\s+(18|19|20)[0-9]{2}\\b",
                "\\bbetween\\s+(18|19|20)[0-9]{2}\\s+(and|to|through|until)\\s+(18|19|20)[0-9]{2}\\b",
                "\\b(18|19|20)[0-9]{2}\\s+(to|through|until)\\s+(18|19|20)[0-9]{2}\\b",
                "\\b(last|past|the last|the past)\\s+[0-9]+\\s+years?\\b",
                "\\b(last|past|the last|the past)\\s+(one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|twenty|thirty|forty|fifty|hundred)\\s+years?\\b",
                "\\b(last|past|the last|the past)\\s+(few|several)\\s+years?\\b",
                "\\b(last|past|the last|the past)\\s+decades?\\b",
                "\\brecent\\s+(years?|decades?)\\b",
                "\\bfrom\\s+(18|19|20)[0-9]{2}\\s+to\\s+(18|19|20)[0-9]{2}\\b",
                "(^|\\s)între\\s+(18|19|20)[0-9]{2}\\s+(și|si)\\s+(18|19|20)[0-9]{2}($|\\s|[.,!?])",
                "\\bintre\\s+(18|19|20)[0-9]{2}\\s+si\\s+(18|19|20)[0-9]{2}\\b",
                "\\bdin\\s+(18|19|20)[0-9]{2}\\s+până în\\s+(18|19|20)[0-9]{2}\\b",
                "\\bdin\\s+(18|19|20)[0-9]{2}\\s+pana in\\s+(18|19|20)[0-9]{2}\\b",
                "\\b(până în|pana in|până la|pana la|până acum|pana acum)\\s+(18|19|20)[0-9]{2}\\b",
                "\\bultimii\\s+[0-9]+\\s+ani\\b",
                "\\bultimele\\s+[0-9]+\\s+ani\\b",
                "\\bultimii\\s+(unu|doi|trei|patru|cinci|șase|sase|șapte|sapte|opt|nouă|noua|zece|douăzeci|douazeci)\\s+ani\\b",
                "\\bultimele\\s+(unu|doi|trei|patru|cinci|șase|sase|șapte|sapte|opt|nouă|noua|zece|douăzeci|douazeci)\\s+ani\\b",
                "\\bultimul\\s+deceniu\\b",
                "\\bultimele\\s+decenii\\b",
                "\\bultimii\\s+ani\\b",
                "\\banii\\s+recenți\\b",
                "\\banii\\s+recenti\\b"
            };
            foreach (string pattern in patterns) {
                if (Regex.match_simple (
                        pattern,
                        range_check,
                        (RegexCompileFlags) 0,
                        (RegexMatchFlags) 0)) {
                    return true;
                }
            }
            return false;
        }

        private static bool requests_unsupported_chart_kind (string normalized) {
            return contains_bounded_any (normalized, {
                "pie chart", "pie charts", "pie diagram",
                "bar chart", "bar charts", "bar graph", "bar graphs",
                "scatter plot", "scatter plots", "scatter chart",
                "histogram", "histograms", "box plot", "box plots",
                "area chart", "area graph", "heatmap", "heat map",
                "donut chart", "doughnut chart", "treemap",
                "radar chart", "spider chart", "candlestick chart",
                "waterfall chart", "stacked bar", "stacked area",
                "circular chart", "circular diagram",
                "grafic circular", "grafic cu bare", "grafic cu coloane",
                "grafic de dispersie", "grafic dispersie",
                "diagramă de dispersie", "diagrama de dispersie",
                "diagramă circulară", "diagrama circulara",
                "diagramă tip bară", "diagrama tip bara",
                "histogramă", "histograma", "hartă tematică",
                "harta tematica", "grafic în arie", "grafic in arie"
            });
        }

        /*
         * Extract the chart's grammatical subject, not just any eligible alias
         * mentioned elsewhere in the sentence. Explicit "of/showing/for" phrases
         * have priority over the looser "GISTEMP chart" form so that
         * "chart of diabetes prevalence using GISTEMP" cannot route to GISTEMP.
         */
        private static string? extract_chart_subject (string normalized) {
            string[] specific_markers = {
                "chart of ", "graph of ", "plot of ", "diagram of ",
                "chart for ", "graph for ", "plot for ",
                "chart about ", "graph about ", "chart on ", "graph on ",
                "chart showing ", "graph showing ", "plot showing ",
                "chart depicting ", "graph depicting ",
                "grafic al ", "graficul al ", "grafic pentru ",
                "grafic despre ", "grafic privind ",
                "diagramă a ", "diagrama a ",
                "diagramă pentru ", "diagrama pentru "
            };

            int best_index = -1;
            string best_marker = "";
            foreach (string marker in specific_markers) {
                int index = normalized.index_of (marker);
                if (index >= 0 && (best_index < 0 || index < best_index)) {
                    best_index = index;
                    best_marker = marker;
                }
            }

            if (best_index >= 0) {
                return clean_chart_subject (
                    normalized.substring (best_index + best_marker.length)
                );
            }

            string[] generic_markers = {
                "chart ", "graph ", "plot ", "diagram ", "grafic ",
                "diagrama ", "diagramă "
            };
            foreach (string marker in generic_markers) {
                int index = normalized.index_of (marker);
                if (index >= 0 && (best_index < 0 || index < best_index)) {
                    best_index = index;
                    best_marker = marker;
                }
            }
            if (best_index < 0) {
                return null;
            }

            return clean_chart_subject (
                normalized.substring (best_index + best_marker.length)
            );
        }

        private static bool is_allowed_chart_subject_tail (string suffix) {
            string normalized_suffix = suffix
                .replace (".", "")
                .replace (",", "")
                .replace ("?", "")
                .replace ("!", "")
                .strip ();
            string[] allowed_tails = {
                "over time", "over the years", "across time", "by year",
                "annually", "in the period", "relative to 1951-1980",
                "what does the gistemp chart show",
                "what does the energy institute chart show",
                "what does the bnr bls c01 chart show",
                "what does the chart show", "what does this chart show",
                "explain what the chart shows", "tell me what the chart shows",
                "ce ne arată graficul gistemp", "ce ne arata graficul gistemp",
                "ce arată graficul gistemp", "ce arata graficul gistemp",
                "explică ce arată graficul", "explica ce arata graficul",
                "from gistemp", "using gistemp", "with gistemp",
                "from energy institute", "using energy institute",
                "with energy institute", "using physical energy content",
                "with physical energy content", "from bnr bls c01",
                "using bnr bls c01",
                "în timp", "in timp", "anual", "anuală", "anuala",
                "from nasa giss", "using nasa giss",
                "cu evoluția temperaturii globale în timp",
                "cu evolutia temperaturii globale in timp",
                "despre furnizarea totală de energie",
                "despre furnizarea totala de energie",
                "al procentului net privind standardele de credit",
                "al procentului net al standardelor de credit"
            };
            foreach (string allowed in allowed_tails) {
                if (normalized_suffix == allowed) {
                    return true;
                }
            }
            return false;
        }

        private static string clean_chart_subject (string raw_subject) {
            string subject = raw_subject.strip ();

            string[] tail_markers = {
                ". ", " using ", " from ", " with ", " based on ", " based upon ",
                " relative to ", " compared with ", " comparing ",
                " over time", " over the years", " across time",
                " by year", " annually", " for the period ",
                " as a ", " as an ", " as ", " via ",
                " in the period ", " for years ", " by year ",
                " folosind ", " utilizând ", " utilizand ", " pe baza ",
                " în timp", " in timp", " în perioada ", " in perioada ",
                " pentru perioada ", " anual", " pe parcursul ",
                " al ", " ale ", " despre ", " cu "
            };
            int cutoff = -1;
            foreach (string marker in tail_markers) {
                int index = subject.index_of (marker);
                if (index >= 0 && (cutoff < 0 || index < cutoff)) {
                    cutoff = index;
                }
            }
            if (cutoff >= 0) {
                string suffix = subject.substring (cutoff).strip ();
                /*
                 * Only discard a tail when it is a reviewed modifier or source
                 * qualifier. Any extra concept after a tail (e.g. "over time
                 * with diabetes prevalence") remains in the subject and fails
                 * exact catalog-subject matching.
                 */
                if (is_allowed_chart_subject_tail (suffix)) {
                    subject = subject.substring (0, cutoff).strip ();
                }
            }

            subject = subject
                .replace (".", "")
                .replace (",", "")
                .replace ("?", "")
                .replace ("!", "")
                .replace (":", "")
                .strip ();

            /* Drop a parenthesized acronym only when it is the terminal suffix. */
            int parenthesis = subject.index_of (" (");
            if (parenthesis >= 0 && subject.has_suffix (")")) {
                subject = subject.substring (0, parenthesis).strip ();
            }

            subject = subject
                .replace (".", "")
                .replace (",", "")
                .replace ("?", "")
                .replace ("!", "")
                .replace (":", "")
                .strip ();

            string[] leading_articles = {
                "the ", "a ", "an ", "un ", "o ", "unui ", "unei "
            };
            foreach (string article in leading_articles) {
                if (subject.has_prefix (article)) {
                    subject = subject.substring (article.length).strip ();
                    break;
                }
            }
            return subject;
        }

        private static string? extract_series_name_before_chart_noun (
            string normalized
        ) {
            string[] noun_markers = {
                " chart", " graph", " plot", " diagram", " grafic",
                " diagrama", " diagramă"
            };
            int first_index = -1;
            foreach (string marker in noun_markers) {
                int index = normalized.index_of (marker);
                if (index >= 0 && (first_index < 0 || index < first_index)) {
                    first_index = index;
                }
            }
            if (first_index < 0) {
                return null;
            }

            string prefix = normalized.substring (0, first_index).strip ();
            string[] request_prefixes = {
                "please generate a ", "please create a ", "please make a ",
                "can you generate a ", "can you create a ", "can you make a ",
                "could you generate a ", "could you create a ", "could you make a ",
                "generate a ", "generate an ", "generate ",
                "create a ", "create an ", "create ",
                "make a ", "make an ", "make ",
                "show me a ", "show me an ", "show a ", "show an ", "show ",
                "draw a ", "draw an ", "draw ",
                "please plot ", "plot a ", "plot an ", "plot ",
                "generează un ", "genereaza un ", "generează o ",
                "genereaza o ", "creează un ", "creeaza un ",
                "creează o ", "creeaza o ", "arată-mi un ",
                "arata-mi un ", "arată un ", "arata un ",
                "desenează un ", "deseneaza un ", "afișează un ",
                "afiseaza un "
            };
            foreach (string prefix_phrase in request_prefixes) {
                if (prefix.has_prefix (prefix_phrase)) {
                    prefix = prefix.substring (prefix_phrase.length).strip ();
                    break;
                }
            }
            return prefix;
        }

        private static bool has_explicit_chart_subject_clause (
            string normalized
        ) {
            return contains_bounded_any (normalized, {
                "chart of ", "graph of ", "plot of ", "diagram of ",
                "chart for ", "graph for ", "plot for ",
                "chart about ", "graph about ", "chart on ", "graph on ",
                "chart showing ", "graph showing ", "plot showing ",
                "chart depicting ", "graph depicting ",
                "grafic al ", "graficul al ", "grafic pentru ",
                "grafic despre ", "grafic privind ",
                "diagramă a ", "diagrama a ",
                "diagramă pentru ", "diagrama pentru "
            });
        }

        private static bool exact_subject_matches_series (
            string subject,
            string selected_series_id
        ) {
            if (subject.strip ().length == 0) {
                return false;
            }
            bool ambiguous = false;
            string? subject_series = ChartCatalog.match_exact_alias_series_id (
                subject, out ambiguous
            );
            return !ambiguous && subject_series == selected_series_id;
        }

        private static bool request_subject_matches_series (
            string normalized,
            string selected_series_id
        ) {
            /*
             * An explicit subject clause ("chart of diabetes prevalence using
             * GISTEMP") takes priority and must match the catalog exactly. Do
             * not fall back to the source mention in that case.
             */
            if (has_explicit_chart_subject_clause (normalized)) {
                string? subject = extract_chart_subject (normalized);
                return subject != null &&
                    exact_subject_matches_series (subject, selected_series_id);
            }

            /*
             * If the source name is directly before the chart noun, use that
             * first. This also handles a following interpretation sentence
             * without letting "what does the chart show?" overwrite the
             * original request's subject.
             */
            string? prefix = extract_series_name_before_chart_noun (normalized);
            if (prefix != null &&
                exact_subject_matches_series (prefix, selected_series_id)) {
                return true;
            }

            string? subject = extract_chart_subject (normalized);
            return subject != null &&
                exact_subject_matches_series (subject, selected_series_id);
        }

        /*
         * Explicit slash commands can carry a subject without repeating the
         * word "chart" (for example, "/chart gistemp show the trend for malaria").
         * Extract these scoped trend/evolution subjects too, so an unknown
         * topic does not evade the grounding gate just because it is absent
         * from the unsupported-variable term list.
         */
        private static string? extract_modifier_subject (string normalized) {
            string[] markers = {
                "trend for ", "trend of ", "evolution of ", "evolution for ",
                "time series for ", "time series of ",
                "tendința pentru ", "tendinta pentru ",
                "tendința a ", "tendinta a ",
                "evoluția pentru ", "evolutia pentru ",
                "evoluția a ", "evolutia a "
            };
            int best_index = -1;
            string best_marker = "";
            foreach (string marker in markers) {
                int index = normalized.index_of (marker);
                if (index >= 0 && (best_index < 0 || index < best_index)) {
                    best_index = index;
                    best_marker = marker;
                }
            }
            if (best_index < 0) {
                return null;
            }
            return clean_chart_subject (
                normalized.substring (best_index + best_marker.length)
            );
        }

        private static string? extract_subject_before_modifier (string normalized) {
            string[] markers = {
                " trend", " evolution", " time series", " tendința",
                " tendinta", " evoluția", " evolutia"
            };
            int best_index = -1;
            foreach (string marker in markers) {
                int index = normalized.index_of (marker);
                if (index >= 0 && (best_index < 0 || index < best_index)) {
                    best_index = index;
                }
            }
            if (best_index < 0) {
                return null;
            }

            string subject = normalized.substring (0, best_index).strip ();
            string[] leading_commands = {
                "show me ", "show ", "display ", "plot ", "generate a ",
                "generate ", "create a ", "create ", "make a ", "make ",
                "draw ", "visualize ", "visualise ",
                "explain what the ", "explain the ", "explain ",
                "tell me what the ", "what does the ",
                "arata-mi ", "arată-mi ", "arata ", "arată ", "genereaza ",
                "generează ", "creeaza ", "creează ", "afiseaza ",
                "afișează ", "vizualizeaza ", "vizualizează ",
                "explică ce arată ", "explica ce arata ",
                "explică ", "explica "
            };
            bool changed = true;
            while (changed) {
                changed = false;
                foreach (string prefix in leading_commands) {
                    if (subject.has_prefix (prefix)) {
                        subject = subject.substring (prefix.length).strip ();
                        changed = true;
                        break;
                    }
                }
                if (subject.has_prefix ("the ") ||
                    subject.has_prefix ("a ") ||
                    subject.has_prefix ("an ")) {
                    int separator = subject.index_of (" ");
                    subject = separator >= 0
                        ? subject.substring (separator + 1).strip ()
                        : "";
                    changed = true;
                }
            }

            string[] leading_scope_modifiers = {
                "quarterly ", "annual ", "yearly ", "historical ",
                "overall ", "long-term ", "trimestrial ",
                "trimestrială ", "trimestriala ", "anual "
            };
            changed = true;
            while (changed) {
                changed = false;
                foreach (string modifier in leading_scope_modifiers) {
                    if (subject.has_prefix (modifier)) {
                        subject = subject.substring (modifier.length).strip ();
                        changed = true;
                        break;
                    }
                }
            }

            /*
             * A modifier clause can follow a command plus a determiner, e.g.
             * "Show the trend for 1880-2025" or "Arată tendința pentru
             * 1880-2025". The prefix before "trend/tendința" is not a
             * chart subject in those cases. Let extract_modifier_subject()
             * validate the actual tail: numeric ranges are allowed, while an
             * unsupported topic such as "malaria" still fails closed there.
             */
            string[] empty_subject_residue = {
                "", "the", "a", "an", "un", "o", "unui", "unei",
                "show", "show me", "display", "plot", "generate", "create",
                "make", "draw", "visualize", "visualise",
                "arată", "arata", "arată-mi", "arata-mi",
                "generează", "genereaza", "creează", "creeaza",
                "desenează", "deseneaza", "afișează", "afiseaza",
                "explică", "explica"
            };
            foreach (string residue in empty_subject_residue) {
                if (subject == residue) {
                    return null;
                }
            }
            if (subject == "historical" ||
                subject == "annual" || subject == "overall" ||
                subject == "long-term" || subject == "yearly" ||
                subject == "quarterly") {
                return null;
            }
            return clean_chart_subject (subject);
        }

        private static bool is_time_period_only_subject (string subject) {
            string normalized = subject.down ().strip ()
                .replace ("–", "-")
                .replace ("—", "-");
            if (normalized.length == 0) {
                return false;
            }

            /*
             * The catalog's sole quarterly profile has this exact admitted
             * coverage. Treat its normalized range as period metadata, without
             * making arbitrary quarter ranges bypass the unsupported-period
             * guard.
             */
            if (normalized == "2022-q4-2025-q3") {
                return true;
            }

            bool has_digit = false;
            bool only_range_characters = true;
            for (int i = 0; i < normalized.length; i++) {
                char character = normalized[i];
                if (character >= '0' && character <= '9') {
                    has_digit = true;
                } else if (character != '-' && character != '/' &&
                           character != '.' && character != ',' &&
                           character != ':' && character != ' ' &&
                           character != '(' && character != ')') {
                    only_range_characters = false;
                    break;
                }
            }
            /*
             * Allow a bare numeric range such as 1880-2025 as a time
             * modifier, but do not allow an unknown topic to be excused just
             * because it also contains words like "year" or "period".
             */
            return has_digit && only_range_characters;
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
                requests_derived_series (normalized) ||
                requests_unsupported_chart_kind (normalized)) {
                return false;
            }

            bool ambiguous = false;
            string? query_series_id =
                ChartCatalog.match_request_series_id (query, out ambiguous);
            if (ambiguous) {
                return false;
            }

            if (has_explicit_chart_subject_clause (normalized) &&
                !request_subject_matches_series (
                    normalized, selected_series_id
                )) {
                return false;
            }

            /*
             * Explicit /chart commands select one series, but any additional
             * non-empty chart-subject phrase must still agree with it. Do not
             * rely only on a finite unsupported-variable blacklist: otherwise
             * an explicit GISTEMP command with "plot malaria prevalence" could
             * silently become a GISTEMP chart merely because malaria is not
             * in that list. A generic modifier such as "show the trend" has
             * no extracted subject and remains valid after a source command.
             */
            string? extracted_subject = extract_chart_subject (normalized);
            if (!has_explicit_chart_subject_clause (normalized) &&
                extracted_subject != null &&
                extracted_subject.strip ().length > 0 &&
                !request_subject_matches_series (
                    normalized, selected_series_id
                )) {
                return false;
            }

            string? subject_before_modifier =
                extract_subject_before_modifier (normalized);
            if (subject_before_modifier != null &&
                subject_before_modifier.strip ().length > 0 &&
                !exact_subject_matches_series (
                    subject_before_modifier, selected_series_id
                )) {
                return false;
            }

            string? modifier_subject = extract_modifier_subject (normalized);
            if (modifier_subject != null &&
                modifier_subject.strip ().length > 0 &&
                !is_time_period_only_subject (modifier_subject) &&
                !exact_subject_matches_series (
                    modifier_subject, selected_series_id
                )) {
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

        /*
         * Guard phrases apply to the clause that contains them, not to every
         * other request in a compound message. If the user negates or discusses
         * one chart but separately asks for one eligible chart, route only that
         * positive clause. Multiple/unsupported positive chart clauses remain
         * ambiguous and fail closed.
         */
        private static string? extract_single_positive_chart_clause (
            string prompt,
            out bool ambiguous
        ) {
            ambiguous = false;
            string segmented = prompt
                .replace ("\r\n", "\n")
                .replace ("\r", "\n")
                .replace (",", "\n")
                .replace (":", "\n")
                .replace (";", "\n")
                .replace (".", "\n")
                .replace ("?", "\n")
                .replace ("!", "\n")
                .replace (" but ", "\n")
                .replace (" instead ", "\n")
                .replace (" however ", "\n")
                .replace (" and then generate ", "\ngenerate ")
                .replace (" and then create ", "\ncreate ")
                .replace (" and then make ", "\nmake ")
                .replace (" and then plot ", "\nplot ")
                .replace (" and generate ", "\ngenerate ")
                .replace (" and create ", "\ncreate ")
                .replace (" and make ", "\nmake ")
                .replace (" and show ", "\nshow ")
                .replace (" and plot ", "\nplot ")
                .replace (" and draw ", "\ndraw ")
                .replace (" and visualize ", "\nvisualize ")
                .replace (" and visualise ", "\nvisualise ")
                .replace (" și generează ", "\ngenerează ")
                .replace (" si genereaza ", "\ngenereaza ")
                .replace (" și creează ", "\ncreează ")
                .replace (" si creeaza ", "\ncreeaza ")
                .replace (" și arată ", "\narata ")
                .replace (" si arata ", "\narata ")
                .replace (" iar generează ", "\ngenerează ")
                .replace (" iar genereaza ", "\ngenereaza ")
                .replace (" iar creează ", "\ncreează ")
                .replace (" iar creeaza ", "\ncreeaza ")
                .replace (" in schimb ", "\n")
                .replace (" ci ", "\n");
            string[] clauses = segmented.split ("\n");
            string? found_clause = null;
            string? found_series_id = null;

            foreach (string raw_clause in clauses) {
                string clause = raw_clause.strip ();
                string normalized_clause = clause.down ();
                if (normalized_clause.length == 0) {
                    continue;
                }

                bool negated_clause =
                    is_explicitly_negated_chart_request (normalized_clause);
                if (negated_clause ||
                    is_hypothetical_chart_request (normalized_clause) ||
                    asks_about_quoted_instruction (normalized_clause) ||
                    is_capability_only_chart_question (normalized_clause) ||
                    is_existing_chart_interpretation_question (normalized_clause)) {
                    /*
                     * A later generic negation such as "don't show it" can
                     * revoke the immediately preceding positive chart request.
                     * A negation that names a different catalog series does
                     * not revoke the one explicit positive clause selected.
                     */
                    if (found_series_id != null && negated_clause) {
                        bool mentioned_ambiguous = false;
                        string? negated_series_id =
                            ChartCatalog.match_request_series_id (
                                clause, out mentioned_ambiguous
                            );
                        if (mentioned_ambiguous ||
                            negated_series_id == null ||
                            negated_series_id == found_series_id) {
                            ambiguous = true;
                            return null;
                        }
                    }
                    continue;
                }

                bool asks_for_chart = contains_bounded_any (
                    normalized_clause,
                    {"grafic", "chart", "graph", "plot", "diagram"}
                );
                bool requests_visualization = contains_bounded_any (
                    normalized_clause,
                    {
                        "arată", "arata", "generează", "genereaza",
                        "creează", "creeaza", "desenează", "deseneaza",
                        "afișează", "afiseaza", "vizualizează", "vizualizeaza",
                        "show", "generate", "create", "draw", "make", "plot",
                        "visualize", "visualise",
                        "can you generate", "can you create", "can you make",
                        "could you generate", "could you create", "could you make",
                        "poți genera", "poti genera", "poți crea", "poti crea",
                        "poți să generezi", "poti sa generezi",
                        "poți să creezi", "poti sa creezi"
                    }
                );
                if (!asks_for_chart || !requests_visualization) {
                    continue;
                }

                bool series_ambiguous = false;
                string? series_id = ChartCatalog.match_request_series_id (
                    clause, out series_ambiguous
                );
                if (series_ambiguous || series_id == null ||
                    requests_explicit_period_filter (normalized_clause, series_id) ||
                    requests_comparison (normalized_clause) ||
                    requests_additional_series (normalized_clause) ||
                    requests_derived_series (normalized_clause) ||
                    requests_unsupported_chart_kind (normalized_clause) ||
                    !request_subject_matches_series (normalized_clause, series_id)) {
                    ambiguous = true;
                    return null;
                }

                if (found_series_id != null) {
                    ambiguous = true;
                    return null;
                }
                found_clause = clause;
                found_series_id = series_id;
            }

            return found_clause;
        }

        public static ChartIntent parse (string prompt) {
            string trimmed = prompt.strip ();
            string normalized = prompt.down ();
            const string gistemp_prefix = "/chart gistemp";
            const string ei_prefix = "/chart energy-institute";
            const string bnr_prefix = "/chart bnr-bls-c01";

            if (command_matches (trimmed, gistemp_prefix)) {
                string query = command_query (trimmed, gistemp_prefix);
                if (requests_explicit_period_filter (
                        query,
                        "climate.gistemp.global-temperature-anomaly")) {
                    return new ChartIntent (
                        false, false, false, prompt, "unsupported_period"
                    );
                }
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
                if (requests_explicit_period_filter (
                        query,
                        "energy.energy-institute.global-total-energy-supply")) {
                    return new ChartIntent (
                        false, false, false, prompt, "unsupported_period"
                    );
                }
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
                if (requests_explicit_period_filter (
                        query,
                        "banking.bnr.bls-c01.credit-standards-net-percentage")) {
                    return new ChartIntent (
                        false, false, false, prompt, "unsupported_period"
                    );
                }
                if (!command_query_is_compatible (
                        query,
                        "banking.bnr.bls-c01.credit-standards-net-percentage")) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }
                return new ChartIntent (false, false, true, query);
            }

            /*
             * Once the user invokes the reserved /chart command, an unknown
             * selector or malformed source token must fail closed instead of
             * falling through to ordinary model chat.
             */
            if (command_matches (trimmed, "/chart")) {
                return new ChartIntent (
                    false, false, false, prompt, "unsupported"
                );
            }

            string routing_prompt = prompt;
            string routing_normalized = normalized;
            if (is_explicitly_negated_chart_request (normalized) ||
                is_hypothetical_chart_request (normalized) ||
                asks_about_quoted_instruction (normalized) ||
                is_capability_only_chart_question (normalized) ||
                is_existing_chart_interpretation_question (normalized)) {
                bool compound_ambiguous = false;
                string? positive_clause = extract_single_positive_chart_clause (
                    prompt, out compound_ambiguous
                );
                if (positive_clause == null) {
                    return new ChartIntent (
                        false, false, false, prompt,
                        compound_ambiguous ? "ambiguous" : "not_chart"
                    );
                }
                /*
                 * The positive clause is routing evidence only. Keep the full
                 * original prompt for response composition so a separate
                 * interpretation/capability question is not silently dropped.
                 */
                routing_normalized = positive_clause.down ();
            }

            bool asks_for_chart = contains_bounded_any (
                routing_normalized,
                {
                    "grafic", "chart", "graph", "plot", "diagram"
                }
            );
            bool requests_visualization = contains_bounded_any (
                routing_normalized,
                {
                    "arată", "arata", "generează", "genereaza",
                    "creează", "creeaza", "desenează", "deseneaza",
                    "afișează", "afiseaza", "vizualizează", "vizualizeaza",
                    "show", "generate", "create", "draw", "make", "plot",
                    "visualize", "visualise",
                    "can you generate", "can you create", "can you make",
                    "could you generate", "could you create", "could you make",
                    "poți genera", "poti genera", "poți crea", "poti crea",
                    "poți să generezi", "poti sa generezi",
                    "poți să creezi", "poti sa creezi"
                }
            );

            if (asks_for_chart && requests_visualization) {
                bool ambiguous = false;
                string? selected_series_id =
                    ChartCatalog.match_request_series_id (routing_normalized, out ambiguous);

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
                if (requests_explicit_period_filter (
                        routing_normalized, selected_series_id)) {
                    return new ChartIntent (
                        false, false, false, prompt, "unsupported_period"
                    );
                }

                if (requests_comparison (routing_normalized) ||
                    requests_additional_series (routing_normalized) ||
                    requests_derived_series (routing_normalized) ||
                    requests_unsupported_chart_kind (routing_normalized) ||
                    !request_subject_matches_series (
                        routing_normalized, selected_series_id
                    )) {
                    return new ChartIntent (
                        false, false, false, prompt, "ambiguous"
                    );
                }

                return new ChartIntent.for_catalog_series (
                    selected_series_id, routing_prompt
                );
            }

            return new ChartIntent (false, false, false, prompt);
        }
    }
}
