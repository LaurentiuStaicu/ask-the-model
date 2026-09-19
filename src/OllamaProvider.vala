namespace AskTheModel {
    public class OllamaProvider : Object {
        private Soup.Session session;

        public string? base_url { get; private set; default = null; }
        public uint model_count { get; private set; default = 0; }

        public OllamaProvider () {
            session = new Soup.Session ();
            session.timeout = 2;
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

                    model_count = root.get_array_member ("models").get_length ();
                    base_url = candidate;
                    return true;
                } catch (GLib.Error error) {
                    continue;
                }
            }

            base_url = null;
            model_count = 0;
            return false;
        }
    }
}
