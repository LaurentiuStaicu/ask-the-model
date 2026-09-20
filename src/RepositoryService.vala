namespace AskTheModel {
    public errordomain RepositoryError {
        HTTP,
        INVALID_RESPONSE,
        TOO_LARGE,
        STORAGE,
        NOT_READY
    }

    public errordomain CitationError {
        INVALID_SHA,
        INVALID_SOURCE,
        INVALID_LOCATOR
    }

    public class RepositoryDescriptor : Object {
        public string id { get; construct; }
        public string acronym { get; construct; }
        public string display_name { get; construct; }
        public string owner { get; construct; }
        public string repository { get; construct; }
        public string tracked_branch { get; construct; }
        public string supported_version { get; construct; }

        public RepositoryDescriptor (
            string id,
            string acronym,
            string display_name,
            string owner,
            string repository,
            string tracked_branch,
            string supported_version = "0.1.0"
        ) {
            Object (
                id: id,
                acronym: acronym,
                display_name: display_name,
                owner: owner,
                repository: repository,
                tracked_branch: tracked_branch,
                supported_version: supported_version
            );
        }

        public string selector_label (
            string? version = null
        ) {
            return "%s (%s) v%s".printf (
                acronym,
                display_name,
                version ?? supported_version
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

        public string citation_api_url (string sha) {
            return "https://api.github.com/repos/%s/%s/contents/CITATION.cff?ref=%s".printf (
                owner,
                repository,
                sha
            );
        }

        private static bool citation_source_path_is_safe (
            string source_path
        ) {
            if (source_path.length == 0 ||
                GLib.Path.is_absolute (source_path) ||
                source_path.has_prefix ("/")) {
                return false;
            }

            foreach (string segment in source_path.split ("/")) {
                if (segment.length == 0 ||
                    segment == "." ||
                    segment == "..") {
                    return false;
                }
            }

            return true;
        }

        private static string? citation_line_anchor (
            string? locator
        ) throws CitationError {
            if (locator == null ||
                !locator.has_prefix ("lines:")) {
                return null;
            }

            if (!GLib.Regex.match_simple (
                    "^lines:[1-9][0-9]*-[1-9][0-9]*$",
                    locator
                )) {
                throw new CitationError.INVALID_LOCATOR (
                    "Citation line locator is invalid."
                );
            }

            string[] range = locator.substring (6).split ("-");
            uint64 start = uint64.parse (range[0]);
            uint64 end = uint64.parse (range[1]);

            if (end < start) {
                throw new CitationError.INVALID_LOCATOR (
                    "Citation line range is reversed."
                );
            }

            if (start == end) {
                return "#L%llu".printf (start);
            }

            return "#L%llu-L%llu".printf (
                start,
                end
            );
        }

        public string immutable_file_permalink (
            string sha,
            string source_path,
            string? locator = null
        ) throws CitationError {
            if (!GLib.Regex.match_simple (
                    "^[0-9a-f]{40}$",
                    sha
                )) {
                throw new CitationError.INVALID_SHA (
                    "Citation snapshot SHA must be canonical lowercase hex."
                );
            }

            if (!citation_source_path_is_safe (source_path)) {
                throw new CitationError.INVALID_SOURCE (
                    "Citation source path is not repository-relative and safe."
                );
            }

            string escaped_path = GLib.Uri.escape_string (
                source_path,
                "/",
                false
            );
            string url =
                "https://github.com/%s/%s/blob/%s/%s".printf (
                    owner,
                    repository,
                    sha,
                    escaped_path
                );

            string? anchor = citation_line_anchor (locator);

            if (anchor != null) {
                if (source_path.down ().has_suffix (".md") ||
                    source_path.down ().has_suffix (".markdown")) {
                    url += "?plain=1";
                }

                url += anchor;
            }

            return url;
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
                    "main",
                    "0.1.0"
                ),
                new RepositoryDescriptor (
                    "cbd",
                    "CBD",
                    "Cognitive Belief Dynamics",
                    "LaurentiuStaicu",
                    "cognitive-belief-dynamics",
                    "main",
                    "0.1.0"
                ),
                new RepositoryDescriptor (
                    "rmd",
                    "RMD",
                    "Romanian Monetary Dynamics",
                    "LaurentiuStaicu",
                    "romanian-monetary-dynamics",
                    "main",
                    "0.1.0"
                )
            };
        }
    }

    public class RepositoryClient : Object {
        public const uint64 MAX_ARCHIVE_BYTES = 128 * 1024 * 1024;
        private const int STREAM_BUFFER_BYTES = 64 * 1024;

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
            try {
                parser.load_from_data (
                    (string) body.get_data (),
                    (ssize_t) body.get_size ()
                );
            } catch (GLib.Error error) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "GitHub branch response is not valid JSON: %s".printf (
                        error.message
                    )
                );
            }

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

        public async string resolve_remote_version (
            RepositoryDescriptor descriptor,
            string sha,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (!GLib.Regex.match_simple ("^[0-9a-f]{40}$", sha)) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Refusing to inspect CITATION.cff for an invalid commit SHA."
                );
            }

            var message = new Soup.Message (
                "GET",
                descriptor.citation_api_url (sha)
            );
            message.request_headers.append (
                "Accept",
                "application/vnd.github.raw+json"
            );
            message.request_headers.append (
                "X-GitHub-Api-Version",
                "2022-11-28"
            );

            GLib.Bytes body = yield session.send_and_read_async (
                message,
                GLib.Priority.DEFAULT,
                cancellable
            );

            if (message.get_status () != Soup.Status.OK) {
                throw new RepositoryError.HTTP (
                    "GitHub CITATION.cff lookup failed with HTTP %u.".printf (
                        message.get_status ()
                    )
                );
            }

            unowned uint8[] bytes = body.get_data ();
            string version;

            try {
                if (!RepositoryNative.cff_extract_version (
                        bytes,
                        body.get_size (),
                        out version
                    )) {
                    throw new RepositoryError.INVALID_RESPONSE (
                        "GitHub CITATION.cff contains invalid version metadata."
                    );
                }
            } catch (RepositoryError error) {
                throw error;
            } catch (GLib.Error error) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "GitHub CITATION.cff contains invalid version metadata: %s".printf (
                        error.message
                    )
                );
            }

            return version;
        }

        public static string staging_archive_path (
            RepositoryDescriptor descriptor,
            string sha,
            bool partial = false
        ) {
            string filename = sha + ".tar.gz" + (partial ? ".part" : "");
            return GLib.Path.build_filename (
                GLib.Environment.get_user_cache_dir (),
                "repository-staging",
                descriptor.id,
                filename
            );
        }

        public static async uint64 copy_stream_bounded (
            GLib.InputStream input,
            GLib.OutputStream output,
            uint64 maximum_bytes,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            uint64 total = 0;
            uint8[] buffer = new uint8[STREAM_BUFFER_BYTES];

            while (true) {
                ssize_t count = yield input.read_async (
                    buffer,
                    GLib.Priority.DEFAULT,
                    cancellable
                );

                if (count == 0) {
                    break;
                }

                uint64 next_total = total + (uint64) count;
                if (next_total > maximum_bytes) {
                    throw new RepositoryError.TOO_LARGE (
                        "Repository archive exceeds the configured download limit."
                    );
                }

                uint8[] chunk = buffer[0:(int) count];
                size_t written = 0;
                bool complete = yield output.write_all_async (
                    chunk,
                    GLib.Priority.DEFAULT,
                    cancellable,
                    out written
                );

                if (!complete || written != (size_t) count) {
                    throw new RepositoryError.STORAGE (
                        "Repository archive could not be written completely."
                    );
                }

                total = next_total;
            }

            return total;
        }

        public async string download_archive_to_staging (
            RepositoryDescriptor descriptor,
            string sha,
            GLib.Cancellable? cancellable = null
        ) throws GLib.Error {
            if (!GLib.Regex.match_simple ("^[0-9a-fA-F]{40}$", sha)) {
                throw new RepositoryError.INVALID_RESPONSE (
                    "Refusing to download an archive for an invalid commit SHA."
                );
            }

            string partial_path = staging_archive_path (
                descriptor,
                sha.down (),
                true
            );
            string final_path = staging_archive_path (
                descriptor,
                sha.down ()
            );
            string? staging_directory = GLib.Path.get_dirname (partial_path);

            if (staging_directory == null ||
                GLib.DirUtils.create_with_parents (
                    staging_directory,
                    0700
                ) != 0) {
                throw new RepositoryError.STORAGE (
                    "Repository staging directory could not be created."
                );
            }

            GLib.File partial_file = GLib.File.new_for_path (partial_path);
            GLib.File final_file = GLib.File.new_for_path (final_path);
            GLib.InputStream? input = null;
            GLib.FileOutputStream? output = null;

            try {
                output = yield partial_file.replace_async (
                    null,
                    false,
                    GLib.FileCreateFlags.PRIVATE,
                    GLib.Priority.DEFAULT,
                    cancellable
                );

                var message = new Soup.Message (
                    "GET",
                    descriptor.archive_api_url (sha.down ())
                );
                add_github_headers (message);

                input = yield session.send_async (
                    message,
                    GLib.Priority.DEFAULT,
                    cancellable
                );

                if (message.get_status () != Soup.Status.OK) {
                    throw new RepositoryError.HTTP (
                        "GitHub archive download failed with HTTP %u.".printf (
                            message.get_status ()
                        )
                    );
                }

                int64 declared_length =
                    message.response_headers.get_content_length ();
                if (declared_length > 0 &&
                    (uint64) declared_length > MAX_ARCHIVE_BYTES) {
                    throw new RepositoryError.TOO_LARGE (
                        "Repository archive exceeds the configured download limit."
                    );
                }

                yield copy_stream_bounded (
                    input,
                    output,
                    MAX_ARCHIVE_BYTES,
                    cancellable
                );

                yield output.close_async (
                    GLib.Priority.DEFAULT,
                    cancellable
                );
                output = null;

                yield input.close_async (
                    GLib.Priority.DEFAULT,
                    cancellable
                );
                input = null;

                yield partial_file.move_async (
                    final_file,
                    GLib.FileCopyFlags.OVERWRITE,
                    GLib.Priority.DEFAULT,
                    cancellable,
                    null
                );

                return final_path;
            } catch (GLib.Error error) {
                if (output != null && !output.is_closed ()) {
                    try {
                        yield output.close_async (
                            GLib.Priority.DEFAULT,
                            null
                        );
                    } catch (GLib.Error close_error) {
                    }
                }

                if (input != null && !input.is_closed ()) {
                    try {
                        yield input.close_async (
                            GLib.Priority.DEFAULT,
                            null
                        );
                    } catch (GLib.Error close_error) {
                    }
                }

                try {
                    partial_file.delete (null);
                } catch (GLib.Error delete_error) {
                }

                throw error;
            }
        }
    }
}
