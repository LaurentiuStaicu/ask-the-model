namespace AskTheModel {
    namespace RepositoryGcTrashNative {
        [CCode (
            cname = "atm_repository_gc_trash_scan",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern bool scan (
            string data_root,
            string repository_id,
            out void* scan_state
        ) throws GLib.Error;

        [CCode (
            cname = "atm_repository_gc_trash_scan_free",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern void scan_free (
            void* scan_state
        );

        [CCode (
            cname = "atm_repository_gc_trash_scan_candidate_count",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern size_t candidate_count (
            void* scan_state
        );

        [CCode (
            cname = "atm_repository_gc_trash_scan_candidate_name",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern unowned string? candidate_name (
            void* scan_state,
            size_t index
        );

        [CCode (
            cname = "atm_repository_gc_trash_scan_candidate_sha",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern unowned string? candidate_sha (
            void* scan_state,
            size_t index
        );

        [CCode (
            cname = "atm_repository_gc_trash_scan_diagnostic_count",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern size_t diagnostic_count (
            void* scan_state
        );

        [CCode (
            cname = "atm_repository_gc_trash_scan_diagnostic_name",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern unowned string? diagnostic_name (
            void* scan_state,
            size_t index
        );

        [CCode (
            cname = "atm_repository_gc_trash_scan_diagnostic_reason",
            cheader_filename = "repository_gc_trash_scan.h"
        )]
        public static extern unowned string? diagnostic_reason (
            void* scan_state,
            size_t index
        );
    }

    public class RepositoryGcTrashCandidate : Object {
        public string repository_id { get; construct; }
        public string snapshot_sha { get; construct; }
        public string trash_name { get; construct; }
        public string trash_path { get; construct; }

        public RepositoryGcTrashCandidate (
            string repository_id,
            string snapshot_sha,
            string trash_name,
            string trash_path
        ) {
            Object (
                repository_id: repository_id,
                snapshot_sha: snapshot_sha,
                trash_name: trash_name,
                trash_path: trash_path
            );
        }
    }

    public class RepositoryGcTrashCandidateSet : Object {
        private RepositoryGcTrashCandidate[] candidates_store = {};

        internal void add (
            string repository_id,
            string snapshot_sha,
            string trash_name,
            string trash_path
        ) {
            candidates_store +=
                new RepositoryGcTrashCandidate (
                    repository_id,
                    snapshot_sha,
                    trash_name,
                    trash_path
                );
        }

        public uint count () {
            return (uint) candidates_store.length;
        }

        public RepositoryGcTrashCandidate? at (
            uint index
        ) {
            if (index >= candidates_store.length) {
                return null;
            }

            return candidates_store[index];
        }

        public RepositoryGcTrashCandidate? deterministic_first () {
            RepositoryGcTrashCandidate? best = null;

            foreach (
                RepositoryGcTrashCandidate candidate
                in candidates_store
            ) {
                if (best == null ||
                    GLib.strcmp0 (
                        candidate.repository_id,
                        best.repository_id
                    ) < 0 ||
                    (candidate.repository_id ==
                        best.repository_id &&
                     GLib.strcmp0 (
                        candidate.trash_name,
                        best.trash_name
                     ) < 0)) {
                    best = candidate;
                }
            }

            return best;
        }
    }

    public class RepositoryGcTrashDiscovery : Object {
        private static void inspect_repository (
            string data_root,
            RepositoryDescriptor descriptor,
            RepositoryGcTrashCandidateSet candidates
        ) throws GLib.Error {
            void* scan_state = null;

            if (!RepositoryGcTrashNative.scan (
                    data_root,
                    descriptor.id,
                    out scan_state
                ) ||
                scan_state == null) {
                throw new GLib.IOError.FAILED (
                    "Repository trash namespace could not be scanned safely."
                );
            }

            try {
                size_t count =
                    RepositoryGcTrashNative.
                        candidate_count (
                            scan_state
                        );

                for (
                    size_t i = 0;
                    i < count;
                    i++
                ) {
                    unowned string? trash_name =
                        RepositoryGcTrashNative.
                            candidate_name (
                                scan_state,
                                i
                            );
                    unowned string? snapshot_sha =
                        RepositoryGcTrashNative.
                            candidate_sha (
                                scan_state,
                                i
                            );

                    if (trash_name == null ||
                        snapshot_sha == null ||
                        !GLib.Regex.match_simple (
                            "^[0-9a-f]{40}$",
                            snapshot_sha
                        ) ||
                        !trash_name.has_prefix (
                            snapshot_sha + "-"
                        )) {
                        throw new GLib.IOError.INVALID_DATA (
                            "Native GC trash scanner returned an invalid canonical identity."
                        );
                    }

                    candidates.add (
                        descriptor.id,
                        snapshot_sha,
                        trash_name,
                        GLib.Path.build_filename (
                            data_root,
                            "Repositories",
                            ".trash",
                            descriptor.id,
                            trash_name
                        )
                    );
                }

                size_t diagnostic_count =
                    RepositoryGcTrashNative.
                        diagnostic_count (
                            scan_state
                        );

                for (
                    size_t i = 0;
                    i < diagnostic_count;
                    i++
                ) {
                    unowned string? name =
                        RepositoryGcTrashNative.
                            diagnostic_name (
                                scan_state,
                                i
                            );
                    unowned string? reason =
                        RepositoryGcTrashNative.
                            diagnostic_reason (
                                scan_state,
                                i
                            );

                    if (name == null ||
                        reason == null) {
                        throw new GLib.IOError.INVALID_DATA (
                            "Native GC trash scanner returned an invalid diagnostic."
                        );
                    }

                    if (reason == "malformed-trash-name") {
                        throw new GLib.IOError.INVALID_DATA (
                            "Repository trash contains a malformed isolated-snapshot identity."
                        );
                    }

                    if (reason == "not-real-directory") {
                        throw new GLib.IOError.INVALID_DATA (
                            "Repository trash candidate is not a real directory."
                        );
                    }

                    throw new GLib.IOError.INVALID_DATA (
                        "Repository trash contains an unknown repair condition."
                    );
                }
            } finally {
                RepositoryGcTrashNative.scan_free (
                    scan_state
                );
            }
        }

        public static RepositoryGcTrashCandidateSet discover (
            string data_root
        ) throws GLib.Error {
            if (data_root.length == 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC trash discovery requires a data root."
                );
            }

            var candidates =
                new RepositoryGcTrashCandidateSet ();

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                inspect_repository (
                    data_root,
                    descriptor,
                    candidates
                );
            }

            return candidates;
        }
    }
}
