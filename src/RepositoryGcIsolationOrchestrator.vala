namespace AskTheModel {
    public enum RepositoryGcIsolationOutcome {
        NOT_ENABLED,
        B0_CONTENDED,
        NO_CANDIDATE,
        B2_CONTENDED,
        DURABLE_ROOT_APPEARED,
        ISOLATED
    }

    public class RepositoryGcIsolationResult : Object {
        public RepositoryGcIsolationOutcome outcome { get; construct; }
        public string? repository_id { get; construct; }
        public string? snapshot_sha { get; construct; }
        public string? trash_path { get; construct; }

        public RepositoryGcIsolationResult (
            RepositoryGcIsolationOutcome outcome,
            string? repository_id = null,
            string? snapshot_sha = null,
            string? trash_path = null
        ) {
            Object (
                outcome: outcome,
                repository_id: repository_id,
                snapshot_sha: snapshot_sha,
                trash_path: trash_path
            );
        }
    }

    internal delegate void RepositoryGcIsolationTestHook (
        string checkpoint,
        RepositoryGcSnapshotCandidate candidate
    ) throws GLib.Error;

    public class RepositoryGcIsolationOrchestrator : Object {
        private static RepositoryGcIsolationTestHook? test_hook = null;

        internal static void set_test_hook (
            owned RepositoryGcIsolationTestHook? hook
        ) {
            test_hook = (owned) hook;
        }

        private static void run_test_hook (
            string checkpoint,
            RepositoryGcSnapshotCandidate candidate
        ) throws GLib.Error {
            if (test_hook != null) {
                test_hook (
                    checkpoint,
                    candidate
                );
            }
        }

        private static bool sha40_is_valid (
            string value
        ) {
            return GLib.Regex.match_simple (
                "^[0-9a-f]{40}$",
                value
            );
        }

        private static void
        sort_generation_ids_ascending (
            int64[] generation_ids
        ) {
            for (
                int i = 1;
                i < generation_ids.length;
                i++
            ) {
                int64 key = generation_ids[i];
                int j = i - 1;

                while (
                    j >= 0 &&
                    generation_ids[j] > key
                ) {
                    generation_ids[j + 1] =
                        generation_ids[j];
                    j--;
                }

                generation_ids[j + 1] = key;
            }
        }

        private static int64[]
        referencing_generations (
            string control_state_path,
            RepositoryGcSnapshotCandidate candidate
        ) throws GLib.Error {
            int64[] complete_generations;

            if (!ControlStateNative.
                    list_complete_generation_ids_readonly (
                        control_state_path,
                        out complete_generations
                    )) {
                throw new GLib.IOError.FAILED (
                    "GC isolation could not enumerate COMPLETE generations."
                );
            }

            int64[] references = {};

            foreach (
                int64 generation_id
                in complete_generations
            ) {
                if (generation_id <= 0) {
                    throw new GLib.IOError.INVALID_DATA (
                        "GC isolation observed an invalid COMPLETE generation identifier."
                    );
                }

                bool present;
                string? snapshot_sha;
                string? repository_version;
                string? snapshot_seal;

                ControlStateNative.
                    load_repository_values_at_generation_readonly (
                        control_state_path,
                        generation_id,
                        candidate.repository_id,
                        out present,
                        out snapshot_sha,
                        out repository_version,
                        out snapshot_seal
                    );

                if (!present) {
                    continue;
                }

                if (snapshot_sha == null ||
                    repository_version == null ||
                    repository_version.length == 0 ||
                    !sha40_is_valid (snapshot_sha)) {
                    throw new GLib.IOError.INVALID_DATA (
                        "GC isolation observed invalid immutable repository identity."
                    );
                }

                if (snapshot_sha == candidate.snapshot_sha) {
                    references += generation_id;
                }
            }

            sort_generation_ids_ascending (
                references
            );
            return references;
        }

        private static void
        release_generation_exclusions (
            int[] lease_fds
        ) {
            for (
                int i = lease_fds.length - 1;
                i >= 0;
                i--
            ) {
                if (lease_fds[i] >= 0) {
                    RepositoryNative.
                        release_generation_lease (
                            lease_fds[i]
                        );
                }
            }
        }

        public static RepositoryGcIsolationResult
        isolate_one (
            bool optimized_operation,
            string data_root,
            string state_root,
            string control_state_path,
            ConversationPersistenceStore conversation_store
        ) throws GLib.Error {
            if (!optimized_operation) {
                return new RepositoryGcIsolationResult (
                    RepositoryGcIsolationOutcome.NOT_ENABLED
                );
            }

            if (data_root.length == 0 ||
                state_root.length == 0 ||
                control_state_path.length == 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC isolation requires data, state and Control DB paths."
                );
            }

            int b0_fd = -1;
            bool b0_contended = false;

            if (!RepositoryNative.
                    try_acquire_mutation_lease (
                        state_root,
                        out b0_fd,
                        out b0_contended
                    )) {
                throw new GLib.IOError.FAILED (
                    "GC isolation could not acquire the B0 mutation lease."
                );
            }

            if (b0_contended) {
                if (b0_fd >= 0) {
                    RepositoryNative.
                        release_mutation_lease (
                            b0_fd
                        );
                    throw new GLib.IOError.INVALID_DATA (
                        "Contended B0 lease unexpectedly returned an owned file descriptor."
                    );
                }

                return new RepositoryGcIsolationResult (
                    RepositoryGcIsolationOutcome.B0_CONTENDED
                );
            }

            if (b0_fd < 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "Uncontended B0 acquisition returned no owned file descriptor."
                );
            }

            int[] exclusion_fds = {};

            try {
                RepositoryGcDurableRoots fresh_roots =
                    RepositoryGcDurableRootCollector.
                        collect (
                            state_root,
                            control_state_path,
                            conversation_store
                        );

                RepositoryGcCandidateSet candidates =
                    RepositoryGcCandidateDiscovery.
                        discover (
                            data_root,
                            fresh_roots
                        );

                RepositoryGcSnapshotCandidate? candidate =
                    candidates.at (0);

                if (candidate == null) {
                    return new RepositoryGcIsolationResult (
                        RepositoryGcIsolationOutcome.NO_CANDIDATE
                    );
                }

                run_test_hook (
                    "after-candidate-selection",
                    candidate
                );

                int64[] references =
                    referencing_generations (
                        control_state_path,
                        candidate
                    );

                foreach (
                    int64 generation_id
                    in references
                ) {
                    int lease_fd = -1;
                    bool contended = false;

                    if (!RepositoryNative.
                            try_acquire_generation_lease_exclusive (
                                state_root,
                                generation_id,
                                out lease_fd,
                                out contended
                            )) {
                        throw new GLib.IOError.FAILED (
                            "GC isolation could not acquire a B2 generation exclusion."
                        );
                    }

                    if (contended) {
                        if (lease_fd >= 0) {
                            RepositoryNative.
                                release_generation_lease (
                                    lease_fd
                                );
                            throw new GLib.IOError.INVALID_DATA (
                                "Contended B2 exclusion unexpectedly returned an owned file descriptor."
                            );
                        }

                        return new RepositoryGcIsolationResult (
                            RepositoryGcIsolationOutcome.B2_CONTENDED,
                            candidate.repository_id,
                            candidate.snapshot_sha
                        );
                    }

                    if (lease_fd < 0) {
                        throw new GLib.IOError.INVALID_DATA (
                            "Uncontended B2 exclusion returned no owned file descriptor."
                        );
                    }

                    exclusion_fds += lease_fd;
                }

                run_test_hook (
                    "after-b2-exclusions",
                    candidate
                );

                RepositoryGcDurableRoots durable_roots =
                    RepositoryGcDurableRootCollector.
                        collect_durable_only (
                            control_state_path,
                            conversation_store
                        );

                if (durable_roots.protects_snapshot (
                        candidate.repository_id,
                        candidate.snapshot_sha
                    )) {
                    return new RepositoryGcIsolationResult (
                        RepositoryGcIsolationOutcome.DURABLE_ROOT_APPEARED,
                        candidate.repository_id,
                        candidate.snapshot_sha
                    );
                }

                string trash_path;

                if (!RepositoryNative.
                        gc_isolate_snapshot_to_trash (
                            data_root,
                            candidate.repository_id,
                            candidate.snapshot_sha,
                            out trash_path,
                            null
                        )) {
                    throw new GLib.IOError.FAILED (
                        "GC isolation primitive reported failure."
                    );
                }

                return new RepositoryGcIsolationResult (
                    RepositoryGcIsolationOutcome.ISOLATED,
                    candidate.repository_id,
                    candidate.snapshot_sha,
                    trash_path
                );
            } finally {
                release_generation_exclusions (
                    exclusion_fds
                );
                RepositoryNative.release_mutation_lease (
                    b0_fd
                );
            }
        }
    }
}
