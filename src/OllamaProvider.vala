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
        private string[] completion_models = {};

        public signal void response_chunk (string chunk);
        public signal void discovery_progress (uint percent);

        public string? base_url { get; private set; default = null; }
        public string? model_name { get; private set; default = null; }
        public uint model_count { get; private set; default = 0; }

        public OllamaProvider () {
            session = new Soup.Session ();
            session.timeout = 300;
        }

        public string[] get_completion_models () {
            return completion_models;
        }

        public bool select_model (string requested_model) {
            foreach (string available_model in completion_models) {
                if (available_model == requested_model) {
                    model_name = requested_model;
                    return true;
                }
            }

            return false;
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
            string? previous_model = model_name;
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
                    string[] detected_completion_models = {};

                    model_count = models.get_length ();
                    base_url = candidate;
                    discovery_progress (0);

                    if (model_count == 0) {
                        discovery_progress (100);
                    }

                    for (uint i = 0; i < model_count; i++) {
                        Json.Object model = models.get_object_element (i);
                        string? candidate_model = null;

                        if (model.has_member ("model")) {
                            candidate_model = model.get_string_member ("model");
                        } else if (model.has_member ("name")) {
                            candidate_model = model.get_string_member ("name");
                        }

                        if (candidate_model != null) {
                            if (yield supports_completion (
                                candidate,
                                candidate_model
                            )) {
                                detected_completion_models += candidate_model;
                            }
                        }

                        uint percent =
                            ((i + 1) * 100) / model_count;
                        discovery_progress (percent);
                    }

                    completion_models = detected_completion_models;
                    model_name = null;

                    if (previous_model != null) {
                        foreach (string detected_model in completion_models) {
                            if (detected_model == previous_model) {
                                model_name = previous_model;
                                break;
                            }
                        }
                    }

                    if (model_name == null && completion_models.length > 0) {
                        model_name = completion_models[0];
                    }

                    return true;
                } catch (GLib.Error error) {
                    continue;
                }
            }

            base_url = null;
            model_name = null;
            model_count = 0;
            completion_models = {};
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
                    "No completion-capable local model is ready. Start the local Ollama-compatible provider and make sure a chat model is installed."
                );
            }
        }

        private async string read_http_error (
            GLib.InputStream input_stream,
            uint status
        ) {
            string detail = "";

            try {
                var data_stream = new GLib.DataInputStream (input_stream);
                string? line = yield data_stream.read_line_utf8_async (
                    GLib.Priority.DEFAULT,
                    null
                );

                if (line != null && line.strip ().length > 0) {
                    var parser = new Json.Parser ();
                    parser.load_from_data (line, -1);
                    Json.Object root = parser.get_root ().get_object ();

                    if (root.has_member ("error")) {
                        detail = " " + root.get_string_member ("error");
                    }
                }
            } catch (GLib.Error error) {
                detail = "";
            }

            return "Local provider returned HTTP %u.%s".printf (
                status,
                detail
            );
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

            // Some reasoning-capable models can spend significant latency
            // on a hidden reasoning trace. The default chat path requests
            // the final answer directly for a more responsive interface.
            builder.set_member_name ("think");
            builder.add_boolean_value (false);

            builder.set_member_name ("stream");
            builder.add_boolean_value (true);

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

            GLib.InputStream input_stream = yield session.send_async (
                message,
                GLib.Priority.DEFAULT,
                null
            );

            if (message.get_status () != Soup.Status.OK) {
                string error_message = yield read_http_error (
                    input_stream,
                    message.get_status ()
                );
                throw new ProviderError.HTTP (error_message);
            }

            var data_stream = new GLib.DataInputStream (input_stream);
            string answer = "";
            bool saw_response = false;

            while (true) {
                string? line = yield data_stream.read_line_utf8_async (
                    GLib.Priority.DEFAULT,
                    null
                );

                if (line == null) {
                    break;
                }

                line = line.strip ();
                if (line.length == 0) {
                    continue;
                }

                var parser = new Json.Parser ();
                parser.load_from_data (line, -1);
                Json.Object root = parser.get_root ().get_object ();
                saw_response = true;

                if (root.has_member ("error")) {
                    throw new ProviderError.HTTP (
                        root.get_string_member ("error")
                    );
                }

                if (root.has_member ("message")) {
                    Json.Object response_message =
                        root.get_object_member ("message");

                    if (response_message.has_member ("content")) {
                        string chunk =
                            response_message.get_string_member ("content");

                        if (chunk.length > 0) {
                            answer += chunk;
                            response_chunk (chunk);
                        }
                    }
                }

                if (root.has_member ("done") &&
                    root.get_boolean_member ("done")) {
                    break;
                }
            }

            if (!saw_response) {
                throw new ProviderError.INVALID_RESPONSE (
                    "Local provider returned an empty response stream."
                );
            }

            roles += "user";
            contents += prompt;
            roles += "assistant";
            contents += answer;

            return answer;
        }
    }
}
