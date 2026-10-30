namespace AskTheModel {
    public class ConversationExport : Object {
        public const string FORMAT =
            "ask-the-model-conversation-export";
        public const int64 SCHEMA_VERSION = 3;

        private static void add_nullable_string (
            Json.Builder builder,
            string name,
            string? value
        ) {
            builder.set_member_name (name);

            if (value == null) {
                builder.add_null_value ();
                return;
            }

            builder.add_string_value (value);
        }

        private static void add_repository (
            Json.Builder builder,
            ConversationPersistenceRepository repository
        ) {
            builder.begin_object ();

            builder.set_member_name ("repository_id");
            builder.add_string_value (repository.repository_id);

            builder.set_member_name ("repository_version");
            builder.add_string_value (
                repository.repository_version
            );

            builder.set_member_name ("snapshot_sha");
            builder.add_string_value (repository.snapshot_sha);

            builder.end_object ();
        }

        private static void add_citation (
            Json.Builder builder,
            ConversationPersistenceCitation citation
        ) {
            builder.begin_object ();

            builder.set_member_name ("label");
            builder.add_string_value (citation.label);

            builder.set_member_name ("repository_id");
            builder.add_string_value (citation.repository_id);

            builder.set_member_name ("repository_version");
            builder.add_string_value (
                citation.repository_version
            );

            builder.set_member_name ("snapshot_sha");
            builder.add_string_value (citation.snapshot_sha);

            builder.set_member_name ("logical_source_id");
            builder.add_string_value (
                citation.logical_source_id
            );

            builder.set_member_name ("source_path");
            builder.add_string_value (citation.source_path);

            builder.set_member_name ("locator");
            builder.add_string_value (citation.locator);

            add_nullable_string (
                builder,
                "title",
                citation.title
            );
            add_nullable_string (
                builder,
                "excerpt",
                citation.excerpt
            );
            add_nullable_string (
                builder,
                "immutable_permalink",
                citation.immutable_permalink
            );

            builder.end_object ();
        }

        private static void add_chart_series (
            Json.Builder builder,
            ConversationPersistenceChartSeries series
        ) {
            builder.begin_object ();

            builder.set_member_name ("series_profile");
            builder.add_string_value (series.series_profile);

            builder.set_member_name ("admission_profile");
            builder.add_string_value (series.admission_profile);

            builder.set_member_name ("scientific_id");
            builder.add_string_value (series.scientific_id);

            builder.set_member_name ("qualified_id");
            builder.add_string_value (series.qualified_id);

            builder.set_member_name ("repository_id");
            builder.add_string_value (series.repository_id);

            builder.set_member_name ("repository_version");
            builder.add_string_value (series.repository_version);

            builder.set_member_name ("snapshot_sha");
            builder.add_string_value (series.snapshot_sha);

            builder.set_member_name ("source_path");
            builder.add_string_value (series.source_path);

            builder.end_object ();
        }

        private static void add_chart_presentation (
            Json.Builder builder,
            ConversationPersistenceChartPresentation presentation
        ) {
            builder.begin_object ();

            builder.set_member_name ("presentation_schema");
            builder.add_string_value (presentation.presentation_schema);
            builder.set_member_name ("presentation_spec_id");
            builder.add_string_value (presentation.presentation_spec_id);
            builder.set_member_name ("series_id");
            builder.add_string_value (presentation.series_id);
            builder.set_member_name ("catalog_schema");
            builder.add_string_value (presentation.catalog_schema);
            builder.set_member_name ("adapter_id");
            builder.add_string_value (presentation.adapter_id);
            builder.set_member_name ("series_profile_id");
            builder.add_string_value (presentation.series_profile_id);
            builder.set_member_name ("series_profile_version");
            builder.add_string_value (presentation.series_profile_version);
            builder.set_member_name ("admission_profile_id");
            builder.add_string_value (presentation.admission_profile_id);
            builder.set_member_name ("admission_profile_version");
            builder.add_string_value (presentation.admission_profile_version);
            builder.set_member_name ("reconstruction_profile_id");
            builder.add_string_value (presentation.reconstruction_profile_id);
            builder.set_member_name ("source_chart_schema");
            builder.add_string_value (presentation.source_chart_schema);
            builder.set_member_name ("source_chart_spec_id");
            builder.add_string_value (presentation.source_chart_spec_id);
            builder.set_member_name ("x_coordinate_semantics");
            builder.add_string_value (presentation.x_coordinate_semantics);
            builder.set_member_name ("x_frequency");
            builder.add_string_value (presentation.x_frequency);
            builder.set_member_name ("x_unit");
            builder.add_string_value (presentation.x_unit);
            builder.set_member_name ("x_dimension");
            builder.add_string_value (presentation.x_dimension);
            builder.set_member_name ("subject");
            builder.add_string_value (presentation.subject);
            builder.set_member_name ("attribute");
            builder.add_string_value (presentation.attribute);
            builder.set_member_name ("y_unit");
            builder.add_string_value (presentation.y_unit);
            builder.set_member_name ("y_dimension");
            builder.add_string_value (presentation.y_dimension);
            builder.set_member_name ("series_semantics");
            builder.add_string_value (presentation.series_semantics);
            builder.set_member_name ("missing_policy");
            builder.add_string_value (presentation.missing_policy);
            add_nullable_string (
                builder, "missing_coordinate", presentation.missing_coordinate
            );
            add_nullable_string (
                builder, "missing_reason", presentation.missing_reason
            );
            builder.set_member_name ("display_profile_id");
            builder.add_string_value (presentation.display_profile_id);
            builder.set_member_name ("display_profile_version");
            builder.add_string_value (presentation.display_profile_version);
            builder.set_member_name ("render_contract");
            builder.add_string_value (presentation.render_contract);

            builder.end_object ();
        }

        private static void add_chart (
            Json.Builder builder,
            ConversationPersistenceChart chart
        ) {
            builder.begin_object ();

            builder.set_member_name ("ordinal");
            builder.add_int_value (chart.ordinal);

            builder.set_member_name ("chart_schema");
            builder.add_string_value (chart.chart_schema);

            builder.set_member_name ("chart_spec_id");
            builder.add_string_value (chart.chart_spec_id);

            builder.set_member_name ("chart_kind");
            builder.add_string_value (chart.chart_kind);

            builder.set_member_name ("reconstruction_profile");
            builder.add_string_value (
                chart.reconstruction_profile
            );

            add_nullable_string (
                builder,
                "presentation_schema",
                chart.presentation_schema
            );
            builder.set_member_name ("presentation");
            if (chart.presentation == null) {
                builder.add_null_value ();
            } else {
                add_chart_presentation (builder, chart.presentation);
            }

            builder.set_member_name ("series");
            builder.begin_array ();

            foreach (
                ConversationPersistenceChartSeries series
                in chart.series
            ) {
                add_chart_series (builder, series);
            }

            builder.end_array ();
            builder.end_object ();
        }

        private static void add_message (
            Json.Builder builder,
            ConversationPersistenceMessage message
        ) {
            builder.begin_object ();

            builder.set_member_name ("sequence_no");
            builder.add_int_value (message.sequence_no);

            builder.set_member_name ("turn_no");
            builder.add_int_value (message.turn_no);

            builder.set_member_name ("role");
            builder.add_string_value (message.role);

            builder.set_member_name ("provider_content");
            builder.add_string_value (
                message.provider_content
            );

            builder.set_member_name ("display_content");
            builder.add_string_value (
                message.display_content
            );

            builder.set_member_name ("grounded");
            builder.add_boolean_value (message.grounded);

            builder.set_member_name ("created_at_us");
            builder.add_int_value (message.created_at_us);

            builder.set_member_name ("citations");
            builder.begin_array ();

            foreach (
                ConversationPersistenceCitation citation
                in message.citations
            ) {
                add_citation (builder, citation);
            }

            builder.end_array ();

            builder.set_member_name ("charts");
            builder.begin_array ();

            foreach (
                ConversationPersistenceChart chart
                in message.charts
            ) {
                add_chart (builder, chart);
            }

            builder.end_array ();
            builder.end_object ();
        }

        public static string serialize_snapshot (
            ConversationPersistenceSnapshot snapshot
        ) {
            var builder = new Json.Builder ();

            builder.begin_object ();

            builder.set_member_name ("format");
            builder.add_string_value (FORMAT);

            builder.set_member_name ("schema_version");
            builder.add_int_value (SCHEMA_VERSION);

            builder.set_member_name ("conversation");
            builder.begin_object ();

            builder.set_member_name ("conversation_id");
            builder.add_string_value (
                snapshot.conversation_id
            );

            builder.set_member_name ("title");
            builder.add_string_value (snapshot.title);

            builder.set_member_name ("created_at_us");
            builder.add_int_value (snapshot.created_at_us);

            builder.set_member_name ("updated_at_us");
            builder.add_int_value (snapshot.updated_at_us);

            builder.set_member_name ("model");
            builder.begin_object ();

            builder.set_member_name ("name");
            builder.add_string_value (snapshot.model_name);

            add_nullable_string (
                builder,
                "digest",
                snapshot.model_digest
            );

            builder.end_object ();

            builder.set_member_name (
                "repository_generation_id"
            );
            builder.add_int_value (
                snapshot.repository_generation_id
            );

            builder.set_member_name ("archived");
            builder.add_boolean_value (snapshot.archived);

            builder.set_member_name ("repositories");
            builder.begin_array ();

            foreach (
                ConversationPersistenceRepository repository
                in snapshot.repositories
            ) {
                add_repository (builder, repository);
            }

            builder.end_array ();

            builder.set_member_name ("messages");
            builder.begin_array ();

            foreach (
                ConversationPersistenceMessage message
                in snapshot.messages
            ) {
                add_message (builder, message);
            }

            builder.end_array ();
            builder.end_object ();
            builder.end_object ();

            Json.Node root = builder.get_root ();
            var generator = new Json.Generator ();
            generator.set_root (root);
            generator.pretty = true;

            return generator.to_data (null) + "\n";
        }
    }
}
