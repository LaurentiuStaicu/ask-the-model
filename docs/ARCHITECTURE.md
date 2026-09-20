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
- conservative Romanian/English query aliasing with protected identifiers preserved;
- repository ID, repository-declared version and snapshot SHA carried on evidence records;
- a deterministic repository router that combines scope, normalization, exact/tabular/lexical retrieval and ranking without requiring embeddings;
- validated per-conversation repository/snapshot pinning and frozen retrieval scopes;
- bounded current-turn grounding contexts with cross-repository balancing and application-owned temporary source labels;
- citation-label resolution and independent citation provenance objects;
- immutable GitHub file permalinks built only from the fixed repository catalog plus exact snapshot SHA;
- a grounded Ollama request-construction path that keeps current-turn evidence transient and strips stale temporary source labels from replayed assistant history;
- end-to-end R4 grounding/citation traceability tests against real EWD, CBD and RMD snapshots;
- the R5 pinned-corpus benchmark/run schema, deterministic evaluator and metric-regression CI.

These backend components are not yet connected to the current GTK repository-selection/Send lifecycle, and citations are not yet rendered to the user. The ordinary GTK Send action still uses the non-grounded chat path. Their presence on development `main` therefore does not make repository-grounded chat a released v0.2.2 feature.

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
- provide both ordinary and grounded request-construction paths;
- keep repository grounding instructions/evidence transient to the current grounded request rather than adding them to persistent chat history;
- stream newline-delimited response objects to the UI;
- keep current-session user/assistant history in memory.

AtM does not own the provider process. It does not install, start, stop, update or configure Ollama.

### Conversation state

The visible/provider conversation remains an in-memory ordered sequence of user and assistant messages held by the provider layer.

Development `main` also contains a separate repository-grounding state that validates and freezes the selected repository versions, exact snapshot SHAs, snapshot roots and retrieval indexes before repository retrieval can use them. Zero repositories remains a valid frozen state for ordinary local chat. This repository-grounding state is not yet created by the current GTK conversation flow.

Retrieved evidence is deliberately not part of persistent provider history. Grounded requests can add system grounding rules and one current-turn evidence block to the outgoing request while the provider continues to retain only the original user prompt and assistant response. Temporary `[S#]` labels from prior assistant turns are removed only from replayed provider history so that a new turn cannot accidentally rebind an old label to a new source; the original answer/citation object is not rewritten.

Conversation state is not persisted across application restarts. Switching the active AI model does not yet create a separate persisted conversation.

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

### Repository UI and conversation controller

Responsible for exposing the fixed EWD/CBD/RMD selection in the GTK interface, freezing the selected repository set and snapshot SHAs on the first user turn, invoking repository retrieval/grounding for later turns, and requiring an explicit New Chat transition for scope changes.

The backend already validates and freezes the selected repository snapshots and can derive retrieval scopes from that frozen state. The missing work is application-controller and GTK wiring.

### Grounded context and citation presentation

The backend already selects retrieved evidence within explicit source/byte budgets, records exactly which repository sources were supplied to the AI, maps temporary `[S#]` labels to deep-copied immutable provenance, constructs trusted commit-pinned GitHub file links, and can compose a transient grounded provider request.

Remaining work is to connect that path to the GTK Send lifecycle, retain citation objects with their originating visible answer, define the approved citation interaction/presentation, and render citation details without trusting the model to construct URLs or provenance.

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
