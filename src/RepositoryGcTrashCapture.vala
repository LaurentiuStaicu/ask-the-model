namespace AskTheModel {
    public class RepositoryGcTrashCandidate : Object {
        public string repository_id { get; construct; }
        public string trash_name { get; construct; }
        public int64 isolation_time { get; construct; }

        public RepositoryGcTrashCandidate (
            string repository_id,
            string trash_name,
            int64 isolation_time
        ) {
            Object (
                repository_id: repository_id,
                trash_name: trash_name,
                isolation_time: isolation_time
            );
        }
    }

    public class RepositoryGcTrashCapture : Object {
        public static RepositoryGcTrashCandidate? capture_oldest (
            string data_root
        ) throws GLib.Error {
            bool found;
            string repository_id;
            string trash_name;
            int64 isolation_time;

            if (!RepositoryNative.
                    gc_select_oldest_trash_candidate (
                        data_root,
                        out found,
                        out repository_id,
                        out trash_name,
                        out isolation_time
                    )) {
                throw new GLib.IOError.FAILED (
                    "GC trash capture primitive reported failure."
                );
            }

            if (!found) {
                return null;
            }

            if (repository_id.length == 0 ||
                trash_name.length == 0 ||
                isolation_time <= 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "GC trash capture returned an invalid canonical identity."
                );
            }

            return new RepositoryGcTrashCandidate (
                repository_id,
                trash_name,
                isolation_time
            );
        }
    }
}
