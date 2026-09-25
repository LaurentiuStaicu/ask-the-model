namespace AskTheModel {
    namespace RepositoryGcCandidateNative {
        [CCode (
            cname = "atm_repository_gc_candidate_scan",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern bool scan (
            string data_root,
            string repository_id,
            out void* scan_state
        ) throws GLib.Error;

        [CCode (
            cname = "atm_repository_gc_candidate_scan_free",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern void scan_free (
            void* scan_state
        );

        [CCode (
            cname = "atm_repository_gc_candidate_scan_candidate_count",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern size_t candidate_count (
            void* scan_state
        );

        [CCode (
            cname = "atm_repository_gc_candidate_scan_candidate_sha",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern unowned string? candidate_sha (
            void* scan_state,
            size_t index
        );

        [CCode (
            cname = "atm_repository_gc_candidate_scan_diagnostic_count",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern size_t diagnostic_count (
            void* scan_state
        );

        [CCode (
            cname = "atm_repository_gc_candidate_scan_diagnostic_name",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern unowned string? diagnostic_name (
            void* scan_state,
            size_t index
        );

        [CCode (
            cname = "atm_repository_gc_candidate_scan_diagnostic_reason",
            cheader_filename = "repository_gc_candidate_scan.h"
        )]
        public static extern unowned string? diagnostic_reason (
            void* scan_state,
            size_t index
        );
    }

    public class RepositoryGcSnapshotCandidate : Object {
        public string repository_id { get; construct; }
        public string snapshot_sha { get; construct; }
        public string snapshot_path { get; construct; }

        public RepositoryGcSnapshotCandidate (
            string repository_id,
            string snapshot_sha,
            string snapshot_path
        ) {
            Object (
                repository_id: repository_id,
                snapshot_sha: snapshot_sha,
                snapshot_path: snapshot_path
            );
        }
    }

    public class RepositoryGcCandidateSet : Object {
        private RepositoryGcSnapshotCandidate[] candidates_store = {};
        private uint quarantine_entries_store = 0;

        internal void add (
            string repository_id,
            string snapshot_sha,
            string snapshot_path
        ) {
            if (contains (
                    repository_id,
                    snapshot_sha
                )) {
                return;
            }

            candidates_store +=
                new RepositoryGcSnapshotCandidate (
                    repository_id,
                    snapshot_sha,
                    snapshot_path
                );
        }

        internal void note_quarantine_entry () {
            quarantine_entries_store++;
        }

        public uint count () {
            return (uint) candidates_store.length;
        }

        public RepositoryGcSnapshotCandidate? at (
            uint index
        ) {
            if (index >= candidates_store.length) {
                return null;
            }

            return candidates_store[index];
        }

        public bool contains (
            string repository_id,
            string snapshot_sha
        ) {
            foreach (
                RepositoryGcSnapshotCandidate candidate
                in candidates_store
            ) {
                if (candidate.repository_id == repository_id &&
                    candidate.snapshot_sha == snapshot_sha) {
                    return true;
                }
            }

            return false;
        }

        public uint quarantine_entry_count () {
            return quarantine_entries_store;
        }
    }

    public class RepositoryGcCandidateDiscovery : Object {
        private static void inspect_repository (
            string data_root,
            RepositoryDescriptor descriptor,
            RepositoryGcDurableRoots protected_roots,
            RepositoryGcCandidateSet candidates
        ) throws GLib.Error {
            void* scan_state = null;

            if (!RepositoryGcCandidateNative.scan (
                    data_root,
                    descriptor.id,
                    out scan_state
                ) ||
                scan_state == null) {
                throw new GLib.IOError.FAILED (
                    "Repository snapshot namespace could not be scanned safely for GC candidates."
                );
            }

            try {
                size_t candidate_count =
                    RepositoryGcCandidateNative.
                        candidate_count (
                            scan_state
                        );

                for (
                    size_t i = 0;
                    i < candidate_count;
                    i++
                ) {
                    unowned string? snapshot_sha =
                        RepositoryGcCandidateNative.
                            candidate_sha (
                                scan_state,
                                i
                            );

                    if (snapshot_sha == null ||
                        !GLib.Regex.match_simple (
                            "^[0-9a-f]{40}$",
                            snapshot_sha
                        )) {
                        throw new GLib.IOError.INVALID_DATA (
                            "Native GC candidate scanner returned an invalid snapshot SHA."
                        );
                    }

                    if (protected_roots.protects_snapshot (
                            descriptor.id,
                            snapshot_sha
                        )) {
                        continue;
                    }

                    candidates.add (
                        descriptor.id,
                        snapshot_sha,
                        GLib.Path.build_filename (
                            data_root,
                            "Repositories",
                            descriptor.id,
                            "snapshots",
                            snapshot_sha
                        )
                    );
                }

                size_t diagnostic_count =
                    RepositoryGcCandidateNative.
                        diagnostic_count (
                            scan_state
                        );

                for (
                    size_t i = 0;
                    i < diagnostic_count;
                    i++
                ) {
                    unowned string? name =
                        RepositoryGcCandidateNative.
                            diagnostic_name (
                                scan_state,
                                i
                            );
                    unowned string? reason =
                        RepositoryGcCandidateNative.
                            diagnostic_reason (
                                scan_state,
                                i
                            );

                    if (name == null ||
                        reason == null) {
                        throw new GLib.IOError.INVALID_DATA (
                            "Native GC candidate scanner returned an invalid diagnostic."
                        );
                    }

                    if (reason == "quarantine-entry") {
                        candidates.note_quarantine_entry ();
                        continue;
                    }

                    if (reason == "unexpected-basename") {
                        throw new GLib.IOError.INVALID_DATA (
                            "Repository snapshot directory contains an unexpected entry that is not a canonical SHA."
                        );
                    }

                    if (reason == "malformed-quarantine") {
                        throw new GLib.IOError.INVALID_DATA (
                            "Repository snapshot directory contains a malformed quarantine entry."
                        );
                    }

                    if (reason == "not-real-directory") {
                        throw new GLib.IOError.INVALID_DATA (
                            "Repository snapshot candidate is not a real directory."
                        );
                    }

                    throw new GLib.IOError.INVALID_DATA (
                        "Repository snapshot directory contains an unknown repair condition."
                    );
                }
            } finally {
                RepositoryGcCandidateNative.scan_free (
                    scan_state
                );
            }
        }

        public static RepositoryGcCandidateSet discover (
            string data_root,
            RepositoryGcDurableRoots protected_roots
        ) throws GLib.Error {
            if (data_root.length == 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC candidate discovery requires a data root."
                );
            }

            var candidates =
                new RepositoryGcCandidateSet ();

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                inspect_repository (
                    data_root,
                    descriptor,
                    protected_roots,
                    candidates
                );
            }

            return candidates;
        }
    }
}
