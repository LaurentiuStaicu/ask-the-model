namespace AskTheModel {
    public class RepositoryGcTrashCandidate : Object {
        public string repository_id { get; construct; }
        public string snapshot_sha { get; construct; }
        public string trash_name { get; construct; }

        public RepositoryGcTrashCandidate (
            string repository_id,
            string snapshot_sha,
            string trash_name
        ) {
            Object (
                repository_id: repository_id,
                snapshot_sha: snapshot_sha,
                trash_name: trash_name
            );
        }
    }

    public class RepositoryGcTrashDiscovery : Object {
        private static bool sha40_is_valid (
            string value
        ) {
            return GLib.Regex.match_simple (
                "^[0-9a-f]{40}$",
                value
            );
        }

        public static RepositoryGcTrashCandidate? select_one (
            string data_root
        ) throws GLib.Error {
            bool found;
            string repository_id;
            string snapshot_sha;
            string trash_name;

            if (!RepositoryNative.
                    gc_select_canonical_trash_candidate (
                        data_root,
                        out found,
                        out repository_id,
                        out snapshot_sha,
                        out trash_name
                    )) {
                throw new GLib.IOError.FAILED (
                    "GC trash discovery primitive reported failure."
                );
            }

            if (!found) {
                return null;
            }

            if (repository_id.length == 0 ||
                trash_name.length == 0 ||
                !sha40_is_valid (
                    snapshot_sha
                ) ||
                !trash_name.has_prefix (
                    snapshot_sha + "-"
                )) {
                throw new GLib.IOError.INVALID_DATA (
                    "GC trash discovery returned an invalid canonical identity."
                );
            }

            return new RepositoryGcTrashCandidate (
                repository_id,
                snapshot_sha,
                trash_name
            );
        }
    }
}
