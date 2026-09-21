# Application status

## Release status

**Current release: Ask the Model (AtM) v0.3.0 — Repository-Grounded Multi-Chat, released 2026-09-21.**

v0.3.0 is the first public repository-aware release. It extends the v0.2 Local Chat Baseline with validated immutable EWD/CBD/RMD snapshots, deterministic local retrieval, per-chat model/repository pinning, grounded source references and inspectable exact-SHA provenance.

The project remains in the `0.x` initial-development series. The public API and repository-management surface are not yet considered stable enough for v1.0.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface and retrieval/provenance layer for locally managed AI models and repositories of scientific dynamical models. It is not itself a scientific model and does not replace the canonical models maintained in those repositories.**

The current scientific repository suite is fixed to:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

Each repository remains authoritative for its own model definitions, data, assumptions, provenance, validation and release boundaries.

## Current functional boundary

v0.3.0 provides:

- GTK 4 / Granite desktop shell packaged for the elementary OS 8 Flatpak runtime;
- local Ollama-compatible provider discovery on loopback;
- provider-managed completion-capable AI-model discovery;
- refreshable AI-model selector;
- streamed text-chat responses;
- independent in-memory multi-chat tabs;
- fixed EWD/CBD/RMD repository selection before first Send;
- exact remote SHA/version Refresh;
- explicit validated repository Download/Update;
- immutable local snapshots under `~/Ask the Model/Repositories`;
- per-snapshot SQLite/FTS5 retrieval indexes;
- exact technical-ID and structured entity/relation retrieval;
- deterministic lexical BM25 and tabular row-key retrieval;
- conservative Romanian/English query normalization;
- deterministic source authority, deduplication and repository scoping;
- first-Send freeze of AI-model identity and repository snapshot set;
- current-turn-only repository grounding;
- compact numbered source references;
- source-detail windows exposing repository/version, exact snapshot SHA, file/locator, logical source ID, readable excerpt and immutable source permalink;
- ordinary zero-repository local chat as a separate streaming path;
- automatic desktop light/dark appearance.

## Repository lifecycle contract

Repository identity is not inferred heuristically.

For every selected repository, AtM:

1. resolves the tracked branch to an exact Git SHA;
2. reads the repository-declared version from that same revision;
3. downloads that exact archive into staging;
4. applies bounded safe extraction;
5. validates repository identity, manifest and required paths;
6. builds and validates a per-snapshot retrieval index;
7. atomically promotes the immutable snapshot only after validation succeeds.

A failed refresh/update must not:

- mark stale/partial data READY;
- destroy the last valid snapshot;
- silently change the snapshot pinned to an active chat.

Validated snapshots are immutable. Retrieval indexes are regenerable cache data.

## Conversation and provenance contract

The first Send freezes:

- AI-model identity/digest where available;
- repository set, including an empty set;
- repository versions;
- exact repository snapshot SHAs.

Repository updates discovered later apply to future chats.

Grounded evidence is retrieved only for the current turn. Repository text is treated as untrusted data and is not allowed to redefine assistant behavior.

Temporary model-visible source labels are resolved by AtM into persistent provenance objects. User-visible source details can expose an immutable GitHub permalink tied to the exact evidence revision.

## Local verification for v0.3.0

Final smoke testing on elementary OS 8 / GTK 4.14 verified the actual development Flatpak rather than only unit tests.

Verified paths include:

- EWD/CBD/RMD batch Refresh;
- exact remote SHA detection;
- updating only the repository whose SHA changed;
- preservation of previous valid RMD snapshot/index pairs;
- grounded EWD+CBD+RMD conversation;
- first-Send selector freeze;
- compact numbered source references;
- source-detail windows without the GTK 4.14 TextChildAnchor/Popover allocation failure;
- readable Markdown source excerpts;
- immutable GitHub permalink to the exact EWD snapshot and line range.

The Flatpak/Meson test suite covers repository lifecycle, archive safety, manifests, storage, index integrity, retrieval, grounding, citations and conversation pinning.

The reviewed frozen R5 deterministic retrieval benchmark remains separate from scientific-validity claims. It validates engineering retrieval behavior, not the scientific validity of EWD, CBD or RMD.

## Provider and privacy boundary

AtM does not bundle, install, start, stop or update the local AI provider.

The current provider implementation addresses loopback endpoints only. GPU acceleration, remote/cloud behavior and model execution semantics belong to the external provider.

Conversation history is stored only in application memory for the current process and is not persisted across restarts.

The Flatpak receives write access only to the dedicated `~/Ask the Model` directory for repository snapshots. It must not request broad Home or host filesystem access.

## Not implemented in v0.3.0

- persistent conversations across application restarts;
- in-app AI-model download/import/delete;
- provider installation/service management;
- configurable provider host/port/authentication/TLS UI;
- arbitrary unreviewed repository origins;
- full repository removal/history management UI;
- semantic embedding/vector retrieval as a mandatory path;
- execution or simulation of EWD, CBD or RMD;
- autonomous modification of scientific repositories;
- cloud-provider integration owned by AtM.

## What v0.3.0 does not claim

- scientific certification of a repository merely because it is READY;
- validated scientific inference from AI-generated prose;
- scientific-model execution when no model has actually been run;
- replacement of canonical repository documentation;
- production-ready scientific decision support.

## Future development directions

Likely extension areas include:

- persistent conversations and persisted per-chat provenance;
- a dedicated repository-management surface for snapshot history/removal;
- additional reviewed repository families;
- optional semantic retrieval/reranking only where benchmark evidence justifies the local cost;
- additional provider implementations;
- accessibility and interface refinements;
- explicit scientific-model execution with fully identified inputs, parameters, version and outputs.

Any such work should preserve the invariants documented in `docs/DEVELOPMENT_GUIDE.md` and `docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md`.

## Documentation

- `README.md` — public landing page and first-use path;
- `docs/USER_INTERFACE_GUIDE.md` — user-facing controls and state behavior;
- `docs/DEVELOPMENT_GUIDE.md` — code map, invariants and extension points;
- `docs/ARCHITECTURE.md` — application architecture;
- `docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md` — repository/retrieval architecture;
- `docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md` — acceptance gates;
- `releases/v0.3.0.md` — release description;
- `CHANGELOG.md` — release history.
