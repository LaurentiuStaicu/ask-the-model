namespace AskTheModel {
    public errordomain RepositoryError {
        HTTP,
        INVALID_RESPONSE
    }

    public class RepositoryDescriptor : Object {
        public string id { get; construct; }
        public string acronym { get; construct; }
        public string display_name { get; construct; }
        public string owner { get; construct; }
        public string repository { get; construct; }
        public string tracked_branch { get; construct; }

        public RepositoryDescriptor (
            string id,
            string acronym,
            string display_name,
            string owner,
            string repository,
            string tracked_branch
        ) {
            Object (
                id: id,
                acronym: acronym,
                display_name: display_name,
                owner: owner,
                repository: repository,
                tracked_branch: tracked_branch
            );
        }

        public string branch_api_url () {
            return "https://api.github.com/repos/%s/%s/branches/%s".printf (
                owner,
                repository,
                tracked_branch
            );
        }

        public string archive_api_url (string sha) {
            return "https://api.github.com/repos/%s/%s/tarball/%s".printf (
                owner,
                repository,
                sha
            );
        }
    }

    public class RepositoryCatalog : Object {
        public static RepositoryDescriptor[] all () {
            return {
                new RepositoryDescriptor (
                    "ewd",
                    "EWD",
                    "Empirical World3 Dynamics",
                    "LaurentiuStaicu",
                    "empirical-world3-dynamics",
                    "main"
                ),
                new RepositoryDescriptor (
                    "cbd",
                    "CBD",
                    "Cognitive Belief Dynamics",
                    "LaurentiuStaicu",
                    "cognitive-belief-dynamics",
                    "main"
                ),
                new RepositoryDescriptor (
                    "rmd",
                    "RMD",
                    "Romanian Monetary Dynamics",
                    "LaurentiuStaicu",
                    "romanian-monetary-dynamics",
                    "main"
                )
            };
        }
    }

    public class RepositoryClient : Object {
        private Soup.Session session;

        public RepositoryClient () {
            session = new Soup.Session () {
                timeout = 60,
                user_agent = "AskTheModel"
            };
        }

        private void add_github_headers (Soup.Message message) {
            message.request_headers.append (
                "Accept",
                "application/vnd.github+json"
            );
            message.request_headers.append (
                "X-GitHub-Api-Version",
                "2022-11-28"
            );
        }

        public async string resolve_branch_sha (
            RepositoryDescriptor descriptor,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            var message = new Soup.Message (
                "GET",
                descriptor.branch_api_url ()
            );
            add_github_headers (message);

            GLib.Bytes body = yield session.send_and_read_async (
                message,
                GLib.Priority.DEFAULT,
                cancellable
            );

            if (message.get_status () != Soup.Status.OK) {
                throw new RepositoryError.HTTP (
                    "GitHub branch lookup failed with HTTP %u.".printf (
                        message.get_status ()
                    )
                );
            }

            var parser = new Json.Parser ();
            parser.load_from_data ((string) body.get_data (), -1);

            Json.Node root_node = parser.get_root ();
            if (root_node.get_node_type () != Json.NodeType.OBJECT) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "GitHub branch response is not a JSON object."
                );
            }

            Json.Object root = root_node.get_object ();
            if (!root.has_member ("commit")) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "GitHub branch response has no commit object."
                );
            }

            Json.Object commit = root.get_object_member ("commit");
            if (!commit.has_member ("sha")) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "GitHub branch response has no commit SHA."
                );
            }

            string sha = commit.get_string_member ("sha");
            if (!GLib.Regex.match_simple ("^[0-9a-fA-F]{40}$", sha)) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "GitHub branch response contains an invalid commit SHA."
                );
            }

            return sha.down ();
        }
    }
}
