namespace AskTheModel {
    public errordomain ProviderError {
        NOT_READY,
        HTTP,
        INVALID_RESPONSE
    }

    public class OllamaProvider : Object {
        private Soup.Session session;
        private string[] roles = {};
        private string[] contents = {};

        public string? base_url { get; private set; default = null; }
        public string? model_name { get; private set; default = null; }
        public uint model_count { get; private set; default = 0; }

        public OllamaProvider () {
            session = new Soup.Session ();
            session.timeout = 300;
        }

        public async bool discover () {
            string[] candidates = {
                "http://127.0.0.1:11434",
                "http://127.0.0.1:11435"
            };

            foreach (string candidate in candidates) {
                try {
                    var message = new Soup.Message (
                        "GET",
                        candidate + "/api/tags"
                    );

                    GLib.Bytes body = yield session.send_and_read_async (
                        message,
                        GLib.Priority.DEFAULT,
                        null
                    );

                    if (message.get_status () != Soup.Status.OK) {
                        continue;
                    }

                    var parser = new Json.Parser ();
                    parser.load_from_data ((string) body.get_data (), -1);

                    Json.Object root = parser.get_root ().get_object ();
                    if (!root.has_member ("models")) {
                        continue;
                    }

                    Json.Array models = root.get_array_member ("models");
                    model_count = models.get_length ();
                    base_url = candidate;
                    model_name = null;

                    if (model_count > 0) {
                        Json.Object first = models.get_object_element (0);

                        if (first.has_member ("model")) {
                            model_name = first.get_string_member ("model");
                        } else if (first.has_member ("name")) {
                            model_name = first.get_string_member ("name");
                        }
                    }

                    return true;
                } catch (GLib.Error error) {
                    continue;
                }
            }

            base_url = null;
            model_name = null;
            model_count = 0;
            return false;
        }

        public bool is_ready () {
            return base_url != null && model_name != null;
        }

        public async string chat (string prompt) throws GLib.Error {
            if (!is_ready ()) {
                throw new ProviderError.NOT_READY (
                    "No local Ollama model is ready."
                );
            }

            var builder = new Json.Builder ();
            builder.begin_object ();

            builder.set_member_name ("model");
            builder.add_string_value (model_name);

            builder.set_member_name ("messages");
            builder.begin_array ();

            for (int i = 0; i < roles.length; i++) {
                builder.begin_object ();
                builder.set_member_name ("role");
                builder.add_string_value (roles[i]);
                builder.set_member_name ("content");
                builder.add_string_value (contents[i]);
                builder.end_object ();
            }

            builder.begin_object ();
            builder.set_member_name ("role");
            builder.add_string_value ("user");
            builder.set_member_name ("content");
            builder.add_string_value (prompt);
            builder.end_object ();

            builder.end_array ();

            builder.set_member_name ("stream");
            builder.add_boolean_value (false);

            builder.end_object ();

            var generator = new Json.Generator ();
            generator.set_root (builder.get_root ());
            string request_body = generator.to_data (null);

            var message = new Soup.Message (
                "POST",
                base_url + "/api/chat"
            );
            message.set_request_body_from_bytes (
                "application/json",
                new GLib.Bytes (request_body.data)
            );

            GLib.Bytes response_body = yield session.send_and_read_async (
                message,
                GLib.Priority.DEFAULT,
                null
            );

            if (message.get_status () != Soup.Status.OK) {
                throw new ProviderError.HTTP (
                    "Ollama returned HTTP %u.".printf (
                        message.get_status ()
                    )
                );
            }

            var parser = new Json.Parser ();
            parser.load_from_data (
                (string) response_body.get_data (),
                -1
            );

            Json.Object root = parser.get_root ().get_object ();
            if (!root.has_member ("message")) {
                throw new ProviderError.INVALID_RESPONSE (
                    "Ollama response did not contain a message."
                );
            }

            Json.Object response_message =
                root.get_object_member ("message");

            if (!response_message.has_member ("content")) {
                throw new ProviderError.INVALID_RESPONSE (
                    "Ollama response did not contain message content."
                );
            }

            string answer =
                response_message.get_string_member ("content").strip ();

            roles += "user";
            contents += prompt;
            roles += "assistant";
            contents += answer;

            return answer;
        }
    }
}
