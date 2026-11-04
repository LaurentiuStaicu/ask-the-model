[CCode (cheader_filename = "chart_spec_v4.h")]
namespace AskTheModel.ChartPresentationNative {
    /* Pure 27-field chart-domain value tuple shared by chart reconstruction
     * and persistence. This VAPI intentionally has no GTK or store dependency. */
    [CCode (cname = "AtmConversationChartPresentationInput", cheader_filename = "chart_spec_v4.h", has_type_id = false)]
    public struct PresentationInput {
        public unowned string? presentation_schema;
        public unowned string? presentation_spec_id;
        public unowned string? series_id;
        public unowned string? catalog_schema;
        public unowned string? adapter_id;
        public unowned string? series_profile_id;
        public unowned string? series_profile_version;
        public unowned string? admission_profile_id;
        public unowned string? admission_profile_version;
        public unowned string? reconstruction_profile_id;
        public unowned string? source_chart_schema;
        public unowned string? source_chart_spec_id;
        public unowned string? x_coordinate_semantics;
        public unowned string? x_frequency;
        public unowned string? x_unit;
        public unowned string? x_dimension;
        public unowned string? subject;
        public unowned string? attribute;
        public unowned string? y_unit;
        public unowned string? y_dimension;
        public unowned string? series_semantics;
        public unowned string? missing_policy;
        public unowned string? missing_coordinate;
        public unowned string? missing_reason;
        public unowned string? display_profile_id;
        public unowned string? display_profile_version;
        public unowned string? render_contract;
    }
}
