# Application architecture boundary

## Released state — v0.4.0

AtM v0.4.0 provides a functional GTK 4 / Granite local-chat application, desktop integration, AppStream metadata, elementary OS 8 Flatpak packaging, an implemented local AI-provider layer, repository-grounded EWD/CBD/RMD conversation paths and a non-visual startup qualification/snapshot-integrity layer.

Repository selection, validated exact-SHA snapshots, deterministic retrieval, per-chat repository/model pinning, grounded citations and on-demand provenance inspection remain part of the released capability boundary; v0.4.0 additionally qualifies the effective deployment/storage state and verifies local snapshot integrity before repository readiness is accepted.

## Repository-aware implementation — released in v0.3.0

The repository/retrieval backend and GTK orchestration are now part of the released capability boundary.

Implemented and tested backend layers include:

- the fixed EWD/CBD/RMD catalog and SHA-pinned repository ingestion path;
- bounded safe archive extraction and immutable validated snapshots;
- deterministic per-snapshot SQLite/FTS5 indexes with source roles and snapshot identity;
- exact technical-ID, structured entity/relation, FTS5/BM25 and tabular row-key retrieval;
- intent-aware source authority, deterministic ordering and logical-source deduplication;
- conversation-pinned repository scoping;
- conservative Romanian/English query aliasing with protected identifiers preserved;
- repository ID, repository-declared version and snapshot SHA carried on evidence records;
- a deterministic repository router that combines scope, normalization, exact/tabular/lexical retrieval and ranking without requiring embeddings;
- immutable per-conversation repository pinning with snapshot/index identity checks and post-freeze scope-mutation rejection;
- a non-visual conversation-session boundary with explicit start/reset semantics, transactional grounded-turn prepare/commit/abort, and pinned local-AI model identity plus provider digest when available;
- current-turn grounding context and Ollama request construction that keep repository evidence transient instead of persisting grounding blocks in normal chat history;
- current-turn citation-label resolution into persistent provenance objects, including fail-closed handling of invalid provenance and no fabricated metadata for unknown labels;
- real-repository R4 traceability tests spanning EWD, CBD and RMD;
- deterministic multi-turn retrieval state with inherited repository intent/exact anchors, bounded effective follow-up queries and explicit clarification outcomes;
- versioned R5 benchmark/run schemas, a reviewed frozen EWD/CBD/RMD corpus, a deterministic real-corpus runner, provisional metric gates and CI that now passes the fixed deterministic baseline.

The v0.3.0 GTK application connects repository selection end-to-end to per-conversation grounded message generation. Each chat tab owns independent provider history and a `ConversationSession`; the first Send freezes repository snapshots plus AI-model identity for that tab, and subsequent grounded turns use the pinned retrieval scope. Temporary model labels are resolved before a grounded turn is committed, unknown labels fail closed, and user-visible compact source references open provenance details including repository/version, exact snapshot SHA, logical source ID, physical locator, evidence excerpt and immutable permalink when available. This is the released v0.3.0 capability boundary.

## Implemented logical components

### Local AI provider service

Implemented in `src/OllamaProvider.vala`.

Responsibilities:

- discover a local Ollama-compatible service;
- prefer `127.0.0.1:11434` and fall back to `127.0.0.1:11435`;
- enumerate installed models through `GET /api/tags`;
- inspect capabilities through `POST /api/show`;
- expose only completion-capable models to the chat selector;
- send multi-turn requests through `POST /api/chat`;
- stream newline-delimited response objects to the UI;
- keep current-session user/assistant history in memory.

AtM does not own the provider process. It does not install, start, stop, update or configure Ollama.

### Conversation state

Each open chat tab keeps its own user/assistant provider history in memory and owns a separate `ConversationSession`:

- the first Send prepares and freezes the selected repository snapshots for that tab;
- the selected local AI model name is pinned at session start;
- the provider model digest is also pinned when discovery exposes one;
- later turns revalidate the pinned model identity before generation;
- grounded retrieval turns use explicit prepare/resolve/commit/abort semantics, so failed model calls or invalid citation labels do not advance follow-up retrieval state;
- grounded provider history is committed only after citation resolution and retrieval-turn commit succeed;
- New creates another independent tab rather than resetting existing conversations;
- closing a tab discards only that tab's in-memory provider/session state.

Conversation state is not persisted across application restarts.

### User interface

The v0.4.0 UI provides:

- elementary-style GTK/Granite shell and system color-scheme following;
- separate AI-model and multi-repository selectors with explicit refresh/download/update lifecycle controls;
- the embedded one-line status LCD;
- real multi-chat notebook pages with a compact New mini-tab, independent tab state, automatic short titles and per-tab close controls;
- selector locking after the first Send for each conversation;
- transcript and prompt composer per tab;
- streamed ordinary local-chat answers;
- validated repository-grounded answers whose temporary `[S#]` labels are removed before display;
- compact `Sources: [1] [2]…` references that open exact provenance details.

### Graphics compatibility layer

On X11 sessions with no explicit `GSK_RENDERER` and no Wayland display, AtM selects the Cairo renderer before GTK initialization. Wayland sessions and explicit renderer overrides are left untouched.

### Repository and deterministic retrieval backend

Implemented in the repository lifecycle, index, query, ranking, normalization, scope and router modules under `src/`.

The backend preserves immutable snapshot identity and returns evidence objects rather than generated scientific conclusions. The scientific repositories remain canonical; AtM does not rewrite their state or convert AI-generated text into canonical project data.

### G-S0 startup qualification and local snapshot integrity — released in v0.4.0

v0.4.0 adds a non-visual startup qualification layer above the repository-grounded path introduced in v0.3.0.

G-S0 qualifies the effective Flatpak deployment identity, the dedicated `~/Ask the Model` storage boundary and persisted repository state before local repository readiness is accepted. Startup reconciliation is offline and does not perform repository Refresh or Download/Update.

Validated repository snapshots also receive a deterministic local `snapshot seal v1`. The seal is a SHA-256 comparison key over sorted repository-relative paths, semantic regular/executable file mode, file size and per-file SHA-256 content hashes. Timestamps, UID and GID are excluded so irrelevant filesystem metadata changes do not change the seal.

The local seal is deliberately separate from the upstream Git commit SHA. The exact Git SHA remains the repository revision identity; the seal answers only whether the local snapshot content still matches the content previously accepted by AtM.

The G-S0 installation result is authoritative for repository runtime use. Non-empty repository grounding and repository mutation remain fail-closed until platform and storage qualification have passed. Ordinary zero-repository chat remains independent, and explicit repository Refresh remains a separate read-only action.

G-O0 is an independent verification-only oracle used by tests and diagnostics. It parses persistent state and repository artifacts independently, recomputes schema-v2 snapshot seals without linking the production seal implementation, validates SQLite/index/source provenance directly and rejects controlled post-READY perturbations in a separate process. It performs no network or repair action and does not become a second runtime READY authority.

Persistent repository state evolves backward-compatibly from schema v1 to schema v2. Existing v1 state remains valid with no seal. A seal may be enrolled only after strict local snapshot/version/index validation succeeds. Once a seal exists, a mismatch makes the repository locally unusable until an explicit repository lifecycle action replaces or repairs the snapshot; G-S0 does not silently repin content.

Both startup reconciliation and normal repository preparation use pre/post seal checks around index validation/rebuild so a snapshot that changes while it is being prepared cannot become READY.

### R4 grounding and citation backend

Implemented across the grounding-context, conversation-grounding, grounded request-builder and citation-label components under `src/`.

The R4 backend can freeze validated repository snapshots for a conversation, build transient current-turn grounding context, construct grounded Ollama requests without persisting evidence blocks into ordinary chat history, resolve temporary `[S#]` labels only against current-turn evidence and retain citation provenance for later inspection. Real-repository integration tests exercise traceability across EWD, CBD and RMD.

These capabilities are wired into the released v0.3.0 GTK path. Grounded answers are held until current-turn citation labels are resolved; unknown labels fail closed. Successful grounded answers retain turn-owned provenance and render compact source-reference controls in the transcript.

### Development Control State after v0.4.0 — current `main`

The tagged v0.4.0 release continues to use the repository-state behavior documented above. Current development `main` adds a separately qualified application-owned SQLite Control State for repository authority without retroactively changing the release description.

The development Control State path:

- identifies the database with an AtM-specific SQLite `application_id` and supported `user_version`;
- enforces foreign keys, WAL journaling, FULL synchronous durability, schema/integrity checks and fail-closed handling of foreign, newer, corrupt or incomplete databases;
- can deterministically import validated legacy repository-state schema v1/v2 into a private candidate generation;
- publishes the first authoritative database through a verified no-replace cutover only when the authoritative path is absent;
- treats an existing valid Control DB as the sole repository-state authority and never falls back to legacy JSON when an existing DB is invalid;
- retains legacy `repository-state.json` as migration/recovery evidence instead of normal runtime authority;
- represents repository mutations as copy-on-write generations so previously COMPLETE generations remain unchanged;
- guards mutation with the generation a caller actually read, rejecting stale writers after another transaction advances `active_state`;
- loads all repository rows for one store from one captured immutable generation;
- pins a non-empty repository-backed `ConversationGrounding` and `ConversationSession` to the exact generation used for validation, while zero-repository chat remains valid with no repository-generation dependency;
- migrates valid Control DB schema v1 state atomically to schema v2 and uses persistent triggers to defend COMPLETE-generation immutability, forward-only COMPLETE activation and append-only migration history;
- requires SQLite >= 3.37.0 and configures each Control DB connection with defensive mode, untrusted-schema mode, disabled legacy DQS parsing and explicit trigger enablement before schema work;
- opens Control DB authority with `SQLITE_OPEN_NOFOLLOW`, verifies the `main` database is genuinely read/write and rejects unsafe XDG state roots before authority resolution.

Conversation grounding does not mutate repository authority while pinning. Required snapshot seals must already exist in the pinned generation, and a later repository update may advance the active generation without changing the generation identity retained by an already-started conversation.

The detailed state contract and staged qualification history are maintained in `docs/CONTROL_STATE.md` and the `STATE-S0-001` through `STATE-S0-012` invariant registry entries.

### R5 retrieval-conversation and benchmark framework

The retrieval-conversation backend can preserve relevant repository scope, intent and exact anchors across deterministic follow-up turns, while returning an explicit clarification outcome when a follow-up is too ambiguous to retrieve safely.

R5 defines versioned benchmark/run schemas, deterministic metric evaluation, provisional gates and CI for the benchmark contract. The reviewed corpus is pinned to exact EWD/CBD/RMD v0.1.0 SHAs, and the real-corpus runner executes the same R3 retrieval-conversation and R4 bounded-grounding primitives against those snapshots. The frozen deterministic baseline now meets every provisional R5 gate.

## Remaining / future components

### Repository lifecycle and conversation-context service

The development UI exposes the fixed EWD/CBD/RMD selector together with separate repository refresh, Download/Update and repository-status controls. Refresh performs a read-only GitHub check; Download/Update uses the validated snapshot lifecycle. Persistent source snapshots are stored under `~/Ask the Model/Repositories`, while retrieval indexes remain private XDG cache data.

The repository/snapshot freeze, transactional grounded-turn lifecycle and AI-model identity pinning are wired to each GTK chat tab. First Send starts the tab session atomically; repository and AI-model selectors remain visible but become insensitive for that conversation; New creates a fresh independently editable tab; switching tabs restores the pinned display state of the selected conversation.

### Grounded context and citation service

Responsible for selecting retrieved evidence within a turn budget, recording exactly which repository sources were supplied to the AI, mapping temporary source labels to immutable provenance, and distinguishing retrieved source material from AI-generated interpretation.

Current-turn grounding context, grounded Ollama request construction, temporary source-label resolution, persistent citation provenance and real-repository traceability are implemented and wired through the released v0.3.0 GTK path. User-visible references are intentionally compact; detailed provenance is disclosed on demand in a transient source-detail window rather than a permanent pane.

### Conversation persistence service

Responsible for future durable local conversation storage, conversation navigation and recording which repository context and AI model were active.

The persistence boundary is specified in `docs/CONVERSATION_PERSISTENCE_ARCHITECTURE.md`. Conversation history is user data in a separate application-owned SQLite database, not an extension of repository Control DB authority. The GTK lifecycle keeps tab closing separate from durable lifecycle: Archive is an explicit metadata mutation and permanent Delete is an explicit destructive transaction, with dependent conversation rows removed by schema foreign-key cascade. Development `main` now includes the qualified store, atomic committed-turn writes, read-only durable snapshot loading and exact continuation-qualification primitives. Historical repository grounding is reconstructed from the persisted COMPLETE Control DB generation without changing current `active_state`; reconstructed repository ID/version/SHA pins and the locally discovered AI model name/digest must match the durable conversation identity before continuation can later be enabled. CONV-04a additionally restores non-archived durable snapshots into view-only Gtk.Notebook tabs. CONV-04b may clear that view-only boundary only after exact model name/digest qualification and reconstruction of the persisted historical repository generation with repository ID/version/SHA equality; the restored ConversationSession then remains pinned to that historical context. Archive/delete remains a separate CONV-04c gate.

### Application settings

Responsible for future provider endpoint configuration, preferred AI model and application-level preferences. Repository source snapshots use the fixed visible location `~/Ask the Model/Repositories`; derived indexes and application state remain in AtM's private XDG cache/state locations. Arbitrary repository storage locations are not a v1 setting.

## Explicitly outside the v0.4.0 boundary

- persistent conversation storage across application restarts;
- arbitrary unreviewed repository origins beyond the fixed EWD/CBD/RMD catalog;
- full repository snapshot-history/removal management UI;
- in-app AI-model download/import/delete;
- provider installation/service management and configurable remote-provider settings;
- executing EWD, CBD or RMD simulations;
- modifying scientific-model repository files;
- treating AI responses as canonical scientific results;
- cloud AI providers owned/configured directly by AtM;
- autonomous changes to scientific models.

Any future model-execution or write-back capability requires a separate architecture and safety review before implementation.

## Multi-repository boundary

The v1 architecture allows zero to three repository contexts. Zero repositories preserves ordinary local AI chat; repository-grounded v1 scope is limited to EWD, CBD and RMD.

The deterministic backend can keep requested repositories in separate evidence sets and refuses to add a repository that is outside the conversation-pinned scope. The selected repository set is conversation context, not AI-provider configuration. Repository context and the active local AI model remain separate concepts.

The wired GTK UI/orchestration layer preserves this per-repository attribution through conversation-pinned scope, per-source provenance and compact source references rather than flattening retrieved material into unidentified combined context.

## Localization and terminology

English is the default application language and Romanian is the planned second supported language. User-visible strings should use the standard gettext/PO workflow.

Canonical terminology is defined in `docs/TERMINOLOGY.md`.

## Compatibility contract

Runtime, provider API, model capability, sandbox and build requirements are maintained in `docs/DEPENDENCIES_AND_COMPATIBILITY.md`.

## Repository/retrieval design baseline

The repository-aware architecture and staged acceptance gates are defined in:

- `docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md`;
- `docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md`.

Those documents remain the governing design/acceptance contracts for the repository-grounded functionality introduced in v0.3.0, the v0.4.0 startup/integrity hardening and later extensions.
