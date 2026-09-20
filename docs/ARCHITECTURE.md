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
- a deterministic repository router that combines scope, normalization, exact/tabular/lexical retrieval and ranking without requiring embeddings.

These backend components are not yet connected to the current GTK conversation flow or Ollama request context. Their presence on development `main` therefore does not make repository-grounded chat a released v0.2.2 feature.

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

## Remaining / not yet user-facing components

### Repository UI and conversation-context service

Responsible for exposing the fixed EWD/CBD/RMD selection in the GTK interface, freezing the selected repository set and snapshot SHAs after the first user turn, and requiring an explicit new-chat transition for scope changes.

The backend can already represent and route a pinned repository set, but this lifecycle is not yet wired into the current application UI.

### Grounded context and citation service

Responsible for selecting retrieved evidence within a turn budget, recording exactly which repository sources were supplied to the AI, mapping temporary source labels to immutable provenance, and distinguishing retrieved source material from AI-generated interpretation.

The retrieval backend now supplies the required source identity and snapshot provenance, but the provider-context and user-visible citation layer is not yet implemented.

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

The remaining grounded-context/citation layer must preserve this per-repository attribution rather than flattening retrieved material into unidentified combined context.

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
