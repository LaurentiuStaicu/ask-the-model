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
        public static RepositoryGcTrashCandidate?
        select_one (
            string data_root
        ) throws GLib.Error {
            if (data_root.length == 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC trash discovery requires a data root."
                );
            }

            string? repository_id = null;
            string? snapshot_sha = null;
            string? trash_name = null;

            if (!RepositoryNative.
                    gc_select_canonical_trash_candidate (
                        data_root,
                        out repository_id,
                        out snapshot_sha,
                        out trash_name
                    )) {
                throw new GLib.IOError.FAILED (
                    "GC trash discovery returned an unsuccessful result without an error."
                );
            }

            if (repository_id == null &&
                snapshot_sha == null &&
                trash_name == null) {
                return null;
            }

            if (repository_id == null ||
                snapshot_sha == null ||
                trash_name == null) {
                throw new GLib.IOError.INVALID_DATA (
                    "GC trash discovery returned a partial candidate identity."
                );
            }

            return new RepositoryGcTrashCandidate (
                repository_id ?? "",
                snapshot_sha ?? "",
                trash_name ?? ""
            );
        }
    }
}
