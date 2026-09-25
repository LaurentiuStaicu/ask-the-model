namespace AskTheModel {
    public errordomain RepositoryGenerationLeaseError {
        BUSY,
        IO
    }

    namespace RepositoryGenerationLeaseNative {
        [CCode (
            cname = "atm_repository_generation_lease_try_acquire_shared",
            cheader_filename = "repository_generation_lease.h"
        )]
        public static extern bool try_acquire_shared (
            string state_root,
            int64 generation_id,
            out int lease_fd,
            out bool contended
        ) throws GLib.Error;

        [CCode (
            cname = "atm_repository_generation_lease_try_acquire_exclusive",
            cheader_filename = "repository_generation_lease.h"
        )]
        public static extern bool try_acquire_exclusive (
            string state_root,
            int64 generation_id,
            out int lease_fd,
            out bool contended
        ) throws GLib.Error;

        [CCode (
            cname = "atm_repository_generation_lease_release",
            cheader_filename = "repository_generation_lease.h"
        )]
        public static extern void release (
            int lease_fd
        );
    }

    public class RepositoryGenerationLease : Object {
        private int lease_fd = -1;

        public int64 generation_id {
            get;
            private set;
            default = 0;
        }

        private RepositoryGenerationLease (
            int lease_fd,
            int64 generation_id
        ) {
            this.lease_fd = lease_fd;
            this.generation_id = generation_id;
        }

        ~RepositoryGenerationLease () {
            if (lease_fd >= 0) {
                RepositoryGenerationLeaseNative.release (
                    lease_fd
                );
                lease_fd = -1;
            }
        }

        public static RepositoryGenerationLease
        acquire_shared (
            string state_root,
            int64 generation_id
        ) throws RepositoryGenerationLeaseError {
            int lease_fd;
            bool contended;

            try {
                bool acquired =
                    RepositoryGenerationLeaseNative.
                        try_acquire_shared (
                            state_root,
                            generation_id,
                            out lease_fd,
                            out contended
                        );

                if (!acquired) {
                    throw new RepositoryGenerationLeaseError.IO (
                        "Repository-generation shared coordination returned no result."
                    );
                }
            } catch (GLib.Error error) {
                throw new RepositoryGenerationLeaseError.IO (
                    "Repository-generation shared coordination failed: %s".printf (
                        error.message
                    )
                );
            }

            if (contended) {
                throw new RepositoryGenerationLeaseError.BUSY (
                    "Repository generation is temporarily unavailable because an exclusive lifecycle operation holds it."
                );
            }

            if (lease_fd < 0) {
                throw new RepositoryGenerationLeaseError.IO (
                    "Repository-generation shared coordination returned no lease."
                );
            }

            return new RepositoryGenerationLease (
                lease_fd,
                generation_id
            );
        }

        internal bool is_held () {
            return lease_fd >= 0;
        }
    }

    namespace ConversationGroundingNative {
        [CCode (
            cname = "atm_conversation_grounding_state_new",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern void* state_new ();

        [CCode (
            cname = "atm_conversation_grounding_state_free",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern void state_free (void* state);

        [CCode (
            cname = "atm_conversation_grounding_add_ready_repository",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool add_ready_repository (
            void* state,
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string snapshot_root,
            string index_path
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_freeze",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool freeze (
            void* state
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_is_frozen",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool is_frozen (void* state);

        [CCode (
            cname = "atm_conversation_grounding_repository_count",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern uint repository_count (void* state);

        [CCode (
            cname = "atm_conversation_grounding_prepare_turn",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool prepare_turn (
            void* state,
            string query,
            out bool has_grounding,
            out bool needs_clarification,
            out string? system_instructions,
            out string? evidence_text,
            out string? post_evidence_reminder
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_resolve_turn_citations",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool resolve_turn_citations (
            void* state,
            string model_output,
            out void* resolution
        ) throws GLib.Error;

        [CCode (
            cname = "atm_citation_resolution_free",
            cheader_filename = "citation_labels.h"
        )]
        public static extern void citation_resolution_free (
            void* resolution
        );

        [CCode (
            cname = "atm_citation_resolution_count",
            cheader_filename = "citation_labels.h"
        )]
        public static extern uint citation_resolution_count (
            void* resolution
        );

        [CCode (
            cname = "atm_citation_resolution_unknown_count",
            cheader_filename = "citation_labels.h"
        )]
        public static extern uint citation_resolution_unknown_count (
            void* resolution
        );

        [CCode (
            cname = "atm_citation_resolution_get",
            cheader_filename = "citation_labels.h"
        )]
        public static extern void* citation_resolution_get (
            void* resolution,
            uint index
        );

        [CCode (
            cname = "atm_citation_resolution_unknown_get",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_resolution_unknown_get (
            void* resolution,
            uint index
        );

        [CCode (
            cname = "atm_citation_reference_label",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_label (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_repository_id",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_repository_id (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_repository_version",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_repository_version (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_snapshot_sha",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_snapshot_sha (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_logical_source_id",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_logical_source_id (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_source_path",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_source_path (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_locator",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_locator (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_title",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_title (
            void* citation
        );

        [CCode (
            cname = "atm_citation_reference_excerpt",
            cheader_filename = "citation_labels.h"
        )]
        public static extern unowned string? citation_reference_excerpt (
            void* citation
        );

        [CCode (
            cname = "atm_conversation_grounding_commit_turn",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern bool commit_turn (
            void* state
        ) throws GLib.Error;

        [CCode (
            cname = "atm_conversation_grounding_abort_turn",
            cheader_filename = "conversation_grounding.h"
        )]
        public static extern void abort_turn (void* state);
    }

    public class ConversationRepositoryPin : Object {
        public string repository_id { get; construct; }
        public string repository_version { get; construct; }
        public string snapshot_sha { get; construct; }

        public ConversationRepositoryPin (
            string repository_id,
            string repository_version,
            string snapshot_sha
        ) {
            Object (
                repository_id: repository_id,
                repository_version: repository_version,
                snapshot_sha: snapshot_sha
            );
        }
    }

    public class CitationReference : Object {
        public string label;
        public string repository_id;
        public string repository_version;
        public string snapshot_sha;
        public string logical_source_id;
        public string source_path;
        public string locator;
        public string? title;
        public string? excerpt;
        public string? immutable_permalink;

        public CitationReference (
            string label,
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string logical_source_id,
            string source_path,
            string locator,
            string? title,
            string? excerpt,
            string? immutable_permalink = null
        ) {
            Object ();
            this.label = label;
            this.repository_id = repository_id;
            this.repository_version = repository_version;
            this.snapshot_sha = snapshot_sha;
            this.logical_source_id = logical_source_id;
            this.source_path = source_path;
            this.locator = locator;
            this.title = title;
            this.excerpt = excerpt;
            this.immutable_permalink = immutable_permalink;
        }
    }

    public class CitationResolution : Object {
        private CitationReference[] citations_store = {};
        private string[] unknown_labels_store = {};

        internal void add_citation (
            CitationReference citation
        ) {
            citations_store += citation;
        }

        internal void add_unknown_label (
            string label
        ) {
            unknown_labels_store += label;
        }

        public uint citation_count () {
            return citations_store.length;
        }

        public CitationReference? citation_at (
            uint index
        ) {
            if (index >= citations_store.length) {
                return null;
            }

            return citations_store[index];
        }

        public uint unknown_label_count () {
            return unknown_labels_store.length;
        }

        public string? unknown_label_at (
            uint index
        ) {
            if (index >= unknown_labels_store.length) {
                return null;
            }

            return unknown_labels_store[index];
        }
    }

    public class ConversationGrounding : Object {
        private void* state = null;
        private int64 pinned_repository_generation_id = 0;
        private ConversationRepositoryPin[] pinned_repositories = {};
        private RepositoryGenerationLease? generation_lease = null;

        public ConversationGrounding () {
            state = ConversationGroundingNative.state_new ();
        }

        ~ConversationGrounding () {
            generation_lease = null;

            if (state != null) {
                ConversationGroundingNative.state_free (state);
                state = null;
            }
        }

        internal void hold_generation_lease (
            RepositoryGenerationLease lease
        ) throws GLib.Error {
            if (is_frozen ()) {
                throw new GLib.IOError.FAILED (
                    "Repository-generation lease cannot change after conversation grounding is frozen."
                );
            }

            if (lease.generation_id <= 0 ||
                !lease.is_held ()) {
                throw new GLib.IOError.INVALID_DATA (
                    "Repository-generation lease is invalid."
                );
            }

            if (generation_lease != null) {
                throw new GLib.IOError.FAILED (
                    "Conversation grounding already owns a repository-generation lease."
                );
            }

            if (pinned_repository_generation_id > 0 &&
                pinned_repository_generation_id !=
                    lease.generation_id) {
                throw new GLib.IOError.INVALID_DATA (
                    "Repository-generation lease does not match the pinned generation."
                );
            }

            generation_lease = lease;
        }

        internal bool has_generation_lease () {
            return generation_lease != null &&
                generation_lease.is_held ();
        }

        internal int64 generation_lease_id () {
            return generation_lease != null
                ? generation_lease.generation_id
                : 0;
        }

        public bool add_ready_repository (
            string repository_id,
            string repository_version,
            string snapshot_sha,
            string snapshot_root,
            string index_path
        ) throws GLib.Error {
            bool added =
                ConversationGroundingNative.add_ready_repository (
                    state,
                    repository_id,
                    repository_version,
                    snapshot_sha,
                    snapshot_root,
                    index_path
                );

            if (added) {
                pinned_repositories +=
                    new ConversationRepositoryPin (
                        repository_id,
                        repository_version,
                        snapshot_sha
                    );
            }

            return added;
        }

        public void pin_repository_generation (
            int64 generation_id
        ) throws GLib.Error {
            if (is_frozen ()) {
                throw new GLib.IOError.FAILED (
                    "Repository generation cannot change after conversation grounding is frozen."
                );
            }

            if (generation_id <= 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "Repository generation identifier must be positive."
                );
            }

            if (pinned_repository_generation_id != 0 &&
                pinned_repository_generation_id !=
                    generation_id) {
                throw new GLib.IOError.INVALID_DATA (
                    "Conversation grounding already has a different repository generation."
                );
            }

            if (generation_lease != null &&
                generation_lease.generation_id !=
                    generation_id) {
                throw new GLib.IOError.INVALID_DATA (
                    "Pinned repository generation does not match the held runtime lease."
                );
            }

            pinned_repository_generation_id =
                generation_id;
        }

        public int64 repository_generation_id () {
            return pinned_repository_generation_id;
        }

        public bool freeze () throws GLib.Error {
            if (repository_count () > 0 &&
                pinned_repository_generation_id <= 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "Repository-backed conversation grounding requires a pinned Control DB generation."
                );
            }

            return ConversationGroundingNative.freeze (state);
        }

        public bool is_frozen () {
            return ConversationGroundingNative.is_frozen (state);
        }

        public uint repository_count () {
            return ConversationGroundingNative.repository_count (
                state
            );
        }

        public ConversationRepositoryPin? repository_pin_at (
            uint index
        ) {
            if (index >= pinned_repositories.length) {
                return null;
            }

            return pinned_repositories[index];
        }

        public bool prepare_turn (
            string query,
            out bool needs_clarification,
            out string? system_instructions,
            out string? evidence_text,
            out string? post_evidence_reminder
        ) throws GLib.Error {
            bool has_grounding;

            if (!ConversationGroundingNative.prepare_turn (
                    state,
                    query,
                    out has_grounding,
                    out needs_clarification,
                    out system_instructions,
                    out evidence_text,
                    out post_evidence_reminder
                )) {
                return false;
            }

            return has_grounding;
        }

        public CitationResolution resolve_turn_citations (
            string model_output
        ) throws GLib.Error {
            void* native_resolution = null;

            if (!ConversationGroundingNative.resolve_turn_citations (
                    state,
                    model_output,
                    out native_resolution
                )) {
                throw new GLib.IOError.FAILED (
                    "Citation resolution failed."
                );
            }

            var result = new CitationResolution ();

            try {
                uint count =
                    ConversationGroundingNative.citation_resolution_count (
                        native_resolution
                    );

                for (uint i = 0; i < count; i++) {
                    void* native_citation =
                        ConversationGroundingNative.citation_resolution_get (
                            native_resolution,
                            i
                        );

                    if (native_citation == null) {
                        continue;
                    }

                    unowned string? label =
                        ConversationGroundingNative.citation_reference_label (
                            native_citation
                        );
                    unowned string? repository_id =
                        ConversationGroundingNative.citation_reference_repository_id (
                            native_citation
                        );
                    unowned string? repository_version =
                        ConversationGroundingNative.citation_reference_repository_version (
                            native_citation
                        );
                    unowned string? snapshot_sha =
                        ConversationGroundingNative.citation_reference_snapshot_sha (
                            native_citation
                        );
                    unowned string? logical_source_id =
                        ConversationGroundingNative.citation_reference_logical_source_id (
                            native_citation
                        );
                    unowned string? source_path =
                        ConversationGroundingNative.citation_reference_source_path (
                            native_citation
                        );
                    unowned string? locator =
                        ConversationGroundingNative.citation_reference_locator (
                            native_citation
                        );
                    unowned string? title =
                        ConversationGroundingNative.citation_reference_title (
                            native_citation
                        );
                    unowned string? excerpt =
                        ConversationGroundingNative.citation_reference_excerpt (
                            native_citation
                        );

                    if (label == null ||
                        repository_id == null ||
                        repository_version == null ||
                        snapshot_sha == null ||
                        logical_source_id == null ||
                        source_path == null ||
                        locator == null) {
                        throw new GLib.IOError.INVALID_DATA (
                            "Resolved citation provenance is incomplete."
                        );
                    }

                    result.add_citation (
                        new CitationReference (
                        label,
                        repository_id,
                        repository_version,
                        snapshot_sha,
                        logical_source_id,
                        source_path,
                        locator,
                        title,
                        excerpt
                    )
                    );
                }

                uint unknown_count =
                    ConversationGroundingNative.citation_resolution_unknown_count (
                        native_resolution
                    );

                for (uint i = 0; i < unknown_count; i++) {
                    unowned string? unknown =
                        ConversationGroundingNative.citation_resolution_unknown_get (
                            native_resolution,
                            i
                        );

                    if (unknown != null) {
                        result.add_unknown_label (unknown);
                    }
                }
            } finally {
                if (native_resolution != null) {
                    ConversationGroundingNative.citation_resolution_free (
                        native_resolution
                    );
                }
            }

            return result;
        }

        public bool commit_turn () throws GLib.Error {
            return ConversationGroundingNative.commit_turn (state);
        }

        public void abort_turn () {
            ConversationGroundingNative.abort_turn (state);
        }
    }
}
