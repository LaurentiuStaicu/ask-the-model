[CCode (cheader_filename = "chart_catalog.h")]
namespace AskTheModel.ChartCatalog {
    /*
     * Matches complete registered alias phrases in request text. A result is
     * only a series selector; callers must separately establish a positive,
     * non-hypothetical chart request grounded in the original prompt.
     */
    [CCode (cname = "atm_chart_catalog_match_request_series_id")]
    public static string? match_request_series_id (
        string request,
        out bool ambiguous
    );
}
