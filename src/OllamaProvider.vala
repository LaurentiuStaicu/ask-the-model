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

        private async bool supports_completion (
            string candidate_url,
            string candidate_model
        ) {
            try {
                var builder = new Json.Builder ();
                builder.begin_object ();
                builder.set_member_name ("model");
                builder.add_string_value (candidate_model);
                builder.end_object ();

                var generator = new Json.Generator ();
                generator.set_root (builder.get_root ());
                string request_body = generator.to_data (null);

                var message = new Soup.Message (
                    "POST",
                    candidate_url + "/api/show"
                );
                message.set_request_body_from_bytes (
                    "application/json",
                    new GLib.Bytes (request_body.data)
                );

                GLib.Bytes body = yield session.send_and_read_async (
                    message,
                    GLib.Priority.DEFAULT,
                    null
                );

                if (message.get_status () != Soup.Status.OK) {
                    return false;
                }

                var parser = new Json.Parser ();
                parser.load_from_data ((string) body.get_data (), -1);

                Json.Object root = parser.get_root ().get_object ();
                if (!root.has_member ("capabilities")) {
                    return false;
                }

                Json.Array capabilities =
                    root.get_array_member ("capabilities");

                for (uint i = 0; i < capabilities.get_length (); i++) {
                    if (capabilities.get_string_element (i) == "completion") {
                        return true;
                    }
                }
            } catch (GLib.Error error) {
                return false;
            }

            return false;
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

                    for (uint i = 0; i < model_count; i++) {
                        Json.Object model = models.get_object_element (i);
                        string? candidate_model = null;

                        if (model.has_member ("model")) {
                            candidate_model = model.get_string_member ("model");
                        } else if (model.has_member ("name")) {
                            candidate_model = model.get_string_member ("name");
                        }

                        if (candidate_model == null) {
                            continue;
                        }

                        if (yield supports_completion (
                            candidate,
                            candidate_model
                        )) {
                            model_name = candidate_model;
                            return true;
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

        private async void ensure_ready () throws GLib.Error {
            if (is_ready ()) {
                return;
            }

            bool found = yield discover ();
            if (!found || !is_ready ()) {
                throw new ProviderError.NOT_READY (
                    "No completion-capable local Ollama model is ready. Start Ollama or Alpaca and make sure a chat model is installed."
                );
            }
        }

        public async string chat (string prompt) throws GLib.Error {
            yield ensure_ready ();

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
                string detail = "";

                try {
                    var error_parser = new Json.Parser ();
                    error_parser.load_from_data (
                        (string) response_body.get_data (),
                        -1
                    );
                    Json.Object error_root =
                        error_parser.get_root ().get_object ();

                    if (error_root.has_member ("error")) {
                        detail = " " +
                            error_root.get_string_member ("error");
                    }
                } catch (GLib.Error parse_error) {
                    detail = "";
                }

                throw new ProviderError.HTTP (
                    "Ollama returned HTTP %u.%s".printf (
                        message.get_status (),
                        detail
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
