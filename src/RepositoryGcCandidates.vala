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

    public class RepositoryGcCandidateDiagnostic : Object {
        public string repository_id { get; construct; }
        public string entry_name { get; construct; }
        public string reason { get; construct; }

        public RepositoryGcCandidateDiagnostic (
            string repository_id,
            string entry_name,
            string reason
        ) {
            Object (
                repository_id: repository_id,
                entry_name: entry_name,
                reason: reason
            );
        }
    }

    public class RepositoryGcCandidateSet : Object {
        private RepositoryGcSnapshotCandidate[] candidates_store = {};
        private RepositoryGcCandidateDiagnostic[] diagnostics_store = {};

        internal void add_candidate (
            RepositoryGcSnapshotCandidate candidate
        ) {
            candidates_store += candidate;
        }

        internal void add_diagnostic (
            RepositoryGcCandidateDiagnostic diagnostic
        ) {
            diagnostics_store += diagnostic;
        }

        public uint candidate_count () {
            return (uint) candidates_store.length;
        }

        public RepositoryGcSnapshotCandidate? candidate_at (
            uint index
        ) {
            if (index >= candidates_store.length) {
                return null;
            }

            return candidates_store[index];
        }

        public uint diagnostic_count () {
            return (uint) diagnostics_store.length;
        }

        public RepositoryGcCandidateDiagnostic? diagnostic_at (
            uint index
        ) {
            if (index >= diagnostics_store.length) {
                return null;
            }

            return diagnostics_store[index];
        }
    }

    public class RepositoryGcCandidateDiscovery : Object {
        public static RepositoryGcCandidateSet discover (
            string data_root,
            RepositoryGcDurableRoots protected_roots
        ) throws GLib.Error {
            if (data_root.length == 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC candidate discovery requires a data root."
                );
            }

            var result = new RepositoryGcCandidateSet ();

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                void* scan_state = null;

                if (!RepositoryGcCandidateNative.scan (
                        data_root,
                        descriptor.id,
                        out scan_state
                    ) ||
                    scan_state == null) {
                    throw new GLib.IOError.FAILED (
                        "GC candidate discovery could not scan repository snapshot metadata."
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
                                "GC candidate scanner returned an invalid snapshot SHA."
                            );
                        }

                        if (protected_roots.protects_snapshot (
                                descriptor.id,
                                snapshot_sha
                            )) {
                            continue;
                        }

                        result.add_candidate (
                            new RepositoryGcSnapshotCandidate (
                                descriptor.id,
                                snapshot_sha,
                                GLib.Path.build_filename (
                                    data_root,
                                    "Repositories",
                                    descriptor.id,
                                    "snapshots",
                                    snapshot_sha
                                )
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
                            reason == null ||
                            name.length == 0 ||
                            reason.length == 0) {
                            throw new GLib.IOError.INVALID_DATA (
                                "GC candidate scanner returned an invalid diagnostic."
                            );
                        }

                        result.add_diagnostic (
                            new RepositoryGcCandidateDiagnostic (
                                descriptor.id,
                                name,
                                reason
                            )
                        );
                    }
                } finally {
                    RepositoryGcCandidateNative.scan_free (
                        scan_state
                    );
                }
            }

            return result;
        }
    }
}
