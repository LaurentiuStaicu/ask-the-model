# Application architecture boundary

## Released state — v0.2.2

AtM v0.2.2 provides a functional GTK 4 / Granite local-chat application, desktop integration, AppStream metadata, elementary OS 8 Flatpak packaging and an implemented local AI-provider layer.

The released UI remains local-chat only. Repository selection, repository-grounded provider context, citations and provenance presentation are not part of v0.2.2.

## Development-main state — unreleased

The development repository now contains the backend foundations for repository-aware retrieval, while keeping them separate from the released UI capability boundary.

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
- current-turn grounding context and Ollama request construction that keep repository evidence transient instead of persisting grounding blocks in normal chat history;
- current-turn citation-label resolution into persistent provenance objects, including fail-closed handling of invalid provenance and no fabricated metadata for unknown labels;
- real-repository R4 traceability tests spanning EWD, CBD and RMD;
- deterministic multi-turn retrieval state with inherited repository intent/exact anchors, bounded effective follow-up queries and explicit clarification outcomes;
- versioned R5 benchmark/run schemas, a deterministic evaluator, provisional metric gates and CI for the benchmark contract.

These backend components are still not connected end-to-end to the current GTK repository-selection and conversation flow. Grounded-request and citation-provenance building blocks exist and are tested, but the public application does not yet invoke them from the released UI or render user-visible citations. The R5 benchmark framework exists, but a reviewed fixed benchmark corpus and a real gate-passing benchmark run are not yet claimed. Their presence on development `main` therefore does not make repository-grounded chat a released v0.2.2 feature.

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

The current conversation is an in-memory ordered sequence of user and assistant messages held by the provider layer.

The state is not persisted across application restarts. Switching the active AI model does not yet create a separate persisted conversation.

### User interface

The current UI provides:

- elementary-style GTK/Granite shell;
- system color-scheme following;
- AI-model selector;
- manual local-model refresh;
- real model-scan progress and temporary result feedback;
- transcript;
- prompt composer;
- streamed assistant text.

### Graphics compatibility layer

On X11 sessions with no explicit `GSK_RENDERER` and no Wayland display, AtM selects the Cairo renderer before GTK initialization. Wayland sessions and explicit renderer overrides are left untouched.

### Repository and deterministic retrieval backend

Implemented in the repository lifecycle, index, query, ranking, normalization, scope and router modules under `src/`.

The backend preserves immutable snapshot identity and returns evidence objects rather than generated scientific conclusions. The scientific repositories remain canonical; AtM does not rewrite their state or convert AI-generated text into canonical project data.

### R4 grounding and citation backend

Implemented across the grounding-context, conversation-grounding, grounded request-builder and citation-label components under `src/`.

The R4 backend can freeze validated repository snapshots for a conversation, build transient current-turn grounding context, construct grounded Ollama requests without persisting evidence blocks into ordinary chat history, resolve temporary `[S#]` labels only against current-turn evidence and retain citation provenance for later inspection. Real-repository integration tests exercise traceability across EWD, CBD and RMD.

These are backend capabilities. The current GTK application still sends ordinary local-chat prompts through the released UI path and does not render repository citations.

### R5 retrieval-conversation and benchmark framework

The retrieval-conversation backend can preserve relevant repository scope, intent and exact anchors across deterministic follow-up turns, while returning an explicit clarification outcome when a follow-up is too ambiguous to retrieve safely.

R5 also defines versioned benchmark/run schemas, deterministic metric evaluation, provisional gates and CI for the benchmark contract. The framework does not itself constitute a passed R5 benchmark: the fixed reviewed corpus and a real benchmark run meeting the gates remain separate work.

## Remaining / not yet user-facing components

### Repository UI and conversation-context service

Responsible for exposing the fixed EWD/CBD/RMD selection in the GTK interface, freezing the selected repository set and snapshot SHAs after the first user turn, and requiring an explicit new-chat transition for scope changes.

The backend can already represent and route a pinned repository set, but this lifecycle is not yet wired into the current application UI.

### Grounded context and citation service

Responsible for selecting retrieved evidence within a turn budget, recording exactly which repository sources were supplied to the AI, mapping temporary source labels to immutable provenance, and distinguishing retrieved source material from AI-generated interpretation.

Backend building blocks are now implemented and tested for current-turn grounding context, grounded Ollama request construction, temporary source-label resolution, persistent citation provenance and real-repository traceability. The remaining work is end-to-end orchestration from the GTK repository/conversation lifecycle and user-visible citation rendering. These backend primitives are not yet a released repository-grounded chat feature.

### Conversation persistence service

Responsible for future durable local conversation storage, conversation navigation and recording which repository context and AI model were active.

### Application settings

Responsible for future provider endpoint configuration, preferred AI model and application-level preferences. Repository snapshots in the v1 architecture are application-managed inside AtM's Flatpak-local XDG storage rather than configured as arbitrary host filesystem locations.

## Explicitly outside the v0.2.2 boundary

- repository ingestion or retrieval;
- EWD/CBD/RMD context selection;
- source/provenance presentation;
- executing EWD, CBD or RMD simulations;
- modifying scientific-model repository files;
- treating AI responses as canonical scientific results;
- cloud AI providers;
- autonomous changes to scientific models.

Any future model-execution or write-back capability requires a separate architecture and safety review before implementation.

## Multi-repository boundary

The v1 architecture allows zero to three repository contexts. Zero repositories preserves ordinary local AI chat; repository-grounded v1 scope is limited to EWD, CBD and RMD.

The deterministic backend can keep requested repositories in separate evidence sets and refuses to add a repository that is outside the conversation-pinned scope. The selected repository set is conversation context, not AI-provider configuration. Repository context and the active local AI model remain separate concepts.

The remaining UI/orchestration layer must preserve this per-repository attribution rather than flattening retrieved material into unidentified combined context.

## Localization and terminology

English is the default application language and Romanian is the planned second supported language. User-visible strings should use the standard gettext/PO workflow.

Canonical terminology is defined in `docs/TERMINOLOGY.md`.

## Compatibility contract

Runtime, provider API, model capability, sandbox and build requirements are maintained in `docs/DEPENDENCIES_AND_COMPATIBILITY.md`.

## Repository/retrieval design baseline

The repository-aware architecture and staged acceptance gates are defined in:

- `docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md`;
- `docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md`.

Those documents remain the governing design/acceptance contracts. Backend implementation on development `main` does not imply that repository ingestion, retrieval, grounded context or citation functionality is already exposed in the public v0.2.2 application.
