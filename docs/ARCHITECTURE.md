# Application architecture boundary

## Current state — v0.2.2

AtM v0.2.2 provides a functional GTK 4 / Granite local-chat application, desktop integration, AppStream metadata, elementary OS 8 Flatpak packaging and an implemented local AI-provider layer.

The conversational layer is implemented for the current application session. The repository-aware retrieval/provenance layer is not yet implemented.

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

## Planned logical components

### Repository context service

Responsible for representing the fixed v1 scientific-repository catalog (EWD, CBD and RMD), managing validated local snapshots and exposing repository evidence to the future retrieval/context layer.

The repository itself remains canonical. AtM must not silently rewrite repository state or convert generated text into canonical project data.

### Context/provenance service

Responsible for recording which repository sources were supplied to the AI for a response and distinguishing retrieved source material from AI-generated interpretation.

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

## Multi-repository future boundary

A future repository-aware conversation may be grounded in zero to three repository contexts. Zero repositories preserves ordinary local AI chat; repository-grounded v1 scope is limited to EWD, CBD and RMD.

The selected repository set is conversation context, not AI-provider configuration. Repository context and the active local AI model remain separate concepts.

For multi-repository discussions, the future context/provenance layer must preserve source attribution per repository rather than flattening all retrieved material into unidentified combined context.

## Localization and terminology

English is the default application language and Romanian is the planned second supported language. User-visible strings should use the standard gettext/PO workflow.

Canonical terminology is defined in `docs/TERMINOLOGY.md`.

## Compatibility contract

Runtime, provider API, model capability, sandbox and build requirements are maintained in `docs/DEPENDENCIES_AND_COMPATIBILITY.md`.

## Repository/retrieval design baseline

The planned repository-aware architecture and staged acceptance gates are defined in:

- `docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md`;
- `docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md`.

Those documents are design contracts for future implementation. Their presence does not imply that repository ingestion, retrieval or citation functionality is already implemented in v0.2.2.
