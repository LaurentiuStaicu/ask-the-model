namespace AskTheModel {
    public class ConversationExport : Object {
        public const string FORMAT =
            "ask-the-model-conversation-export";
        public const int64 SCHEMA_VERSION = 1;

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
