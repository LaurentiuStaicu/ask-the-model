[CCode (cheader_filename = "chart_catalog.h")]
namespace AskTheModel.ChartCatalog {
    /*
     * Matches complete registered alias phrases in request text. A result is
     * only a series selector; callers must separately establish a positive,
     * non-hypothetical chart request grounded in the original prompt.
     */
    [CCode (cname = "atm_chart_catalog_text_has_phrase")]
    public static bool text_has_phrase (string text, string phrase);

    [CCode (cname = "atm_chart_catalog_match_exact_alias_series_id")]
    public static string? match_exact_alias_series_id (
        string subject,
        out bool ambiguous
    );

    [CCode (cname = "atm_chart_catalog_match_request_series_id")]
    public static string? match_request_series_id (
        string request,
        out bool ambiguous
    );

    [CCode (cname = "atm_chart_catalog_response_guidance")]
    public static string? response_guidance (string series_id);

    [CCode (cname = "atm_chart_catalog_chart_only_response")]
    public static string? chart_only_response (
        string series_id,
        bool romanian
    );

}
