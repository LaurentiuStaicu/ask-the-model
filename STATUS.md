# Application status

## Release status

**Current release: Ask the Model (AtM) v0.3.0 — Repository-Grounded Multi-Chat, released 2026-09-20.**

v0.3.0 is the first public repository-aware release for the fixed EWD/CBD/RMD scientific dynamical-model suite. It preserves the local Ollama-compatible chat baseline while adding validated repository lifecycle management, deterministic retrieval, independent multi-chat state, frozen per-conversation repository/model identity and exact on-demand citation provenance.

The project remains in the `0.x` initial-development series.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface designed for querying, exploring and discussing repositories of scientific dynamical models through natural language. It is not itself a scientific model and does not embed, redefine or replace the canonical models maintained in those repositories.**

The v0.3.0 repository catalog is fixed to:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

Each scientific model remains authoritative in its own repository. AtM is an access and interaction layer over those external project sources.

## Current functional boundary

v0.3.0 includes:

- the GTK 4 / Granite application shell and elementary OS 8 Flatpak packaging;
- automatic system light/dark appearance following;
- local Ollama-compatible discovery on `127.0.0.1:11434` with `127.0.0.1:11435` compatibility fallback;
- `GET /api/tags` model enumeration and `POST /api/show` capability inspection;
- completion-capable AI-model filtering and manual refresh;
- ordinary zero-repository local chat with streamed responses;
- independent in-memory chat tabs with New, automatic semantic titles of at most three words and per-tab close controls;
- a fixed multi-select EWD/CBD/RMD repository selector;
- repository Refresh plus explicit Download/Update lifecycle actions;
- validated immutable repository source snapshots stored under `~/Ask the Model/Repositories/<id>/snapshots/<sha>/`;
- bounded safe extraction and strict repository manifest/CITATION validation;
- deterministic per-snapshot SQLite/FTS5 indexes with source-role and provenance checks;
- deterministic exact technical-ID, structured entity/relation, lexical BM25 and tabular retrieval;
- conservative Romanian/English query normalization with protected technical identifiers;
- repository ID, repository version and exact snapshot SHA carried on evidence records;
- repository and local-AI model identity frozen on the first Send for each chat;
- current-turn repository grounding that does not persist transient evidence blocks as ordinary chat history;
- transactional grounded turns so failed model calls or invalid citations do not advance retrieval-conversation state;
- current-turn temporary `[S#]` source-label resolution before commit;
- fail-closed handling of unknown source labels;
- compact user-visible numbered sources with repository/version/SHA/logical-source/locator/excerpt provenance and immutable GitHub permalinks when available;
- deterministic multi-turn retrieval state with explicit clarification outcomes;
- real-repository R4 traceability CI and a frozen R5 retrieval benchmark that passes the current provisional engineering gates.

## Conversation scope

Repository selection is optional. Zero selected repositories preserves ordinary local chat.

Before the first Send in a conversation, the repository set and AI model are editable. The first Send pins the validated repository snapshots/index identity and local-AI model identity for that tab. A different scientific scope or AI model belongs in a new chat rather than silently changing the basis of an existing conversation.

Conversation and grounding state are held in memory for the current application process and are not persisted across application restarts.

## Provider and dependency boundary

AtM does not bundle, install, start, stop or update the local AI provider. The provider is an external dependency.

AI models remain provider-managed artifacts. AtM does not download, import, move, update or delete AI model files.

Compatibility is defined by the API behavior documented in `docs/DEPENDENCIES_AND_COMPATIBILITY.md`, not by a particular locally tested provider version. GPU acceleration is provider-owned; AtM contains no vendor-specific inference engine.

## Repository and scientific boundary

Scientific repositories remain external canonical sources. Their documentation, code, data, assumptions, provenance, validation status and release boundaries remain authoritative.

Repository evidence can ground an AI response, but AtM does not present AI-generated interpretation as a canonical model result. v0.3.0 does not execute EWD, CBD or RMD simulations.

The R4/R5 gates are engineering validation of repository retrieval, traceability and conversation behavior against pinned test corpora. They are not scientific-validity certifications of EWD, CBD or RMD.

## Not implemented in v0.3.0

- conversation persistence across application restarts;
- arbitrary repository support beyond EWD/CBD/RMD;
- application/provider host, port, authentication or TLS settings;
- provider installation/start/stop/update management;
- AI-model installation, download, import, update or deletion inside AtM;
- cloud AI providers;
- execution or simulation of scientific models;
- autonomous changes to scientific repositories.

## What v0.3.0 does not claim

- that an AI-generated explanation is a canonical scientific-model result;
- validated scientific inference merely because repository evidence was retrieved;
- model execution or simulation;
- authoritative replacement of repository documentation;
- a production-ready scientific decision system.

See `releases/v0.3.0.md` for the current release description, `CHANGELOG.md` for release history and `docs/DEPENDENCIES_AND_COMPATIBILITY.md` for compatibility requirements.
