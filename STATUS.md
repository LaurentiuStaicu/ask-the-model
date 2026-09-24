# Application status

## Release status

**Current release: Ask the Model (AtM) v0.5.0 — Saved Conversation History and State Hardening, released 2026-09-24.**

v0.5.0 retains the repository-grounded and startup-integrity boundaries of v0.3.0/v0.4.0 and adds explicit **Save to History** conversation archiving with exact-context restore, deterministic managed JSON mirrors, hardened durable conversation persistence and the application-owned SQLite Control DB generation model. It also promotes the post-v0.4.0 publication hardening and the effective Flatpak runtime-ref qualification fix.

The project remains in the `0.x` initial-development series. The public API and repository-management surface are not yet considered stable enough for v1.0.

## v0.5.0 release highlights

The v0.5.0 capability boundary promotes the qualified post-v0.4.0 development work:

- **Save to History** is the only user-visible conversation save transition; closing an unarchived chat or exiting normally discards that working conversation instead of silently persisting it;
- History lists explicitly archived conversations only and exposes **Open** plus separately confirmed **Delete permanently**;
- reopening a saved conversation reconstructs its durable transcript/provenance and requalifies the exact saved AI-model name/digest plus repository generation/version/SHA before continuation;
- saved conversations receive deterministic managed JSON mirrors under `~/Ask the Model/Conversation Exports/`; unarchived working chats are not exported;
- permanent deletion is fail-closed across the archived SQLite conversation and its managed JSON mirror;
- committed turns use a separate hardened `conversations.sqlite3` path that preserves provider/display content, exact pinned model/repository identity and citation provenance before provider history advances;
- repository-state runtime authority is the application-owned SQLite `control-state.sqlite3`, with verified legacy cutover, copy-on-write COMPLETE generations, generation-consistent reads and stale-writer guards;
- Control DB connections and authority paths are qualified with SQLite defensive configuration, no-follow opens, writable-authority verification and a qualified XDG state root;
- the effective Flatpak runtime parser accepts the real `runtime/ID/ARCH/BRANCH` form while retaining strict rejection of mismatched runtime identities;
- Flatpak CI separates read-only verification/build from the main-only publication job, with verified artifact handoff and source-commit traceability;
- production retrieval/context limits are shared with observational measurement tooling while the frozen R5 retrieval-quality gate remains unchanged and blocking.

At publication time, the tagged v0.5.0 release and `main` share this capability boundary. Later **AtM Development** builds may move ahead independently and must continue to expose their exact source `main` commit SHA.

## Development after v0.5.0 — optimization master control

Development builds now introduce a user-visible **Optimizations** master switch for the post-v0.5.0 optimization program. Every process starts with the switch **OFF** and the state is not persisted in its first implementation. OFF preserves the post-v0.5.0 baseline for newly started operations; ON permits only optimization slices that have been implemented and qualified. Test-only fault/measurement infrastructure is outside this runtime gate because it is not a production optimization path.

No runtime optimization is considered production-active merely because it is planned in OPT-00; each future slice must prove both OFF-baseline and ON-qualified behavior.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface and retrieval/provenance layer for locally managed AI models and repositories of scientific dynamical models. It is not itself a scientific model and does not replace the canonical models maintained in those repositories.**

The current scientific repository suite is fixed to:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

Each repository remains authoritative for its own model definitions, data, assumptions, provenance, validation and release boundaries.

## Current functional boundary

v0.5.0 provides:

- GTK 4 / Granite desktop shell packaged for the elementary OS 8 Flatpak runtime;
- local Ollama-compatible provider discovery on loopback;
- provider-managed completion-capable AI-model discovery;
- refreshable AI-model selector;
- streamed text-chat responses;
- independent multi-chat tabs;
- fixed EWD/CBD/RMD repository selection before first Send;
- exact remote SHA/version Refresh;
- explicit validated repository Download/Update;
- validated immutable EWD/CBD/RMD snapshots under `~/Ask the Model/Repositories`;
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
- asynchronous G-S0 startup qualification before local-provider discovery;
- effective Flatpak deployment identity and platform fingerprint recording;
- fail-closed qualification of the dedicated `~/Ask the Model` storage boundary;
- durable per-startup qualification records separated from volatile AI-provider state;
- backward-compatible repository-state schema v2 with optional/enrolled local snapshot seals;
- deterministic local snapshot-integrity verification before repository grounding and around index validation/rebuild;
- explicit same-SHA recovery through the existing Download/Update action, with invalid real-directory snapshots quarantined for diagnosis before a revalidated exact-SHA replacement is promoted;
- non-empty repository grounding and Download/Update remain blocked until platform/storage startup qualification has passed; ordinary zero-repository chat remains separate and valid.
- application-owned SQLite Control DB repository-state authority with copy-on-write COMPLETE generations and immutable historical generation reads;
- exact repository-generation pinning for repository-backed conversations;
- hardened durable conversation commits in a separate `conversations.sqlite3` store;
- explicit **Save to History** archive semantics, archive-only History and exact-context continuation qualification;
- deterministic managed JSON mirrors for archived conversations under `~/Ask the Model/Conversation Exports/`;
- fail-closed permanent deletion across archived SQLite state and the managed JSON mirror.

## Repository lifecycle contract

Repository identity is not inferred heuristically.

For every selected repository, AtM:

1. resolves the tracked branch to an exact Git SHA;
2. reads the repository-declared version from that same revision;
3. downloads that exact archive into staging;
4. applies bounded safe extraction;
5. validates repository identity, manifest and required paths;
6. builds and validates a per-snapshot retrieval index;
7. computes a deterministic local snapshot seal before and after retrieval-index preparation;
8. requires a stable pre/post seal before accepting the snapshot;
9. atomically persists the exact Git SHA, repository version and local seal only after validation succeeds.

A failed refresh/update must not:

- mark stale/partial data READY;
- destroy the last valid snapshot;
- silently change the snapshot pinned to an active chat.

Validated snapshots are treated as immutable. The exact Git SHA remains the upstream revision identity; the local snapshot seal is a separate deterministic tamper-detection key. Retrieval indexes remain regenerable cache data. If a sealed snapshot becomes locally invalid, AtM marks Download as required; for the same remote SHA it obtains the exact archive first, quarantines the invalid real directory without following symlinks, then validates and promotes the replacement.

## Conversation and provenance contract

The first Send freezes:

- AI-model identity/digest where available;
- repository set, including an empty set;
- repository versions;
- exact repository snapshot SHAs.

Repository updates discovered later apply to future chats.

Grounded evidence is retrieved only for the current turn. Repository text is treated as untrusted data and is not allowed to redefine assistant behavior.

Temporary model-visible source labels are resolved by AtM into persistent provenance objects. User-visible source details can expose an immutable GitHub permalink tied to the exact evidence revision.

## Verification for v0.5.0

The v0.5.0 stack passes the Flatpak/Meson and invariant gates for startup qualification, repository snapshot integrity, Control DB authority/generation semantics, durable conversation persistence, exact-context History restore, archive-only managed exports, fail-closed permanent deletion, retrieval/provenance behavior and the independent G-O0 state/snapshot/index/provenance oracle. The packaged baseline remains elementary OS 8 / GTK 4.14; automated release qualification does not convert generated AI text into a scientific-model result.

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

The Flatpak/Meson test suite covers startup qualification, the fail-closed repository runtime gate, storage-boundary checks, repository-state migration, snapshot integrity, repository lifecycle, archive safety, manifests, index integrity, independent G-O0 verification, retrieval, grounding, citations and conversation pinning. G-O0 is a test/diagnostic verification instrument and does not become a second application runtime authority.

The reviewed frozen R5 deterministic retrieval benchmark remains separate from scientific-validity claims. It validates engineering retrieval behavior, not the scientific validity of EWD, CBD or RMD.

## Provider and privacy boundary

AtM does not bundle, install, start, stop or update the local AI provider.

The current provider implementation addresses loopback endpoints only. GPU acceleration, remote/cloud behavior and model execution semantics belong to the external provider.

v0.5.0 persists only conversations explicitly saved to History. Working turns use the separate local conversation store as a transactional boundary, but Close or normal application exit discards unarchived conversations; startup removes unarchived leftovers from an interrupted prior session. Saved History entries retain exact model/repository provenance, and permanent Delete removes both the archived SQLite record and its managed JSON export.

The Flatpak receives write access only to the dedicated `~/Ask the Model` directory for validated repository snapshots and managed conversation exports. It must not request broad Home or host filesystem access.

## Not implemented in v0.5.0

- import of saved-conversation JSON archives;
- retention-policy and bulk History management;
- in-app AI-model download/import/delete;
- provider installation/service management;
- configurable provider host/port/authentication/TLS UI;
- arbitrary unreviewed repository origins;
- full repository removal/history management UI;
- semantic embedding/vector retrieval as a mandatory path;
- execution or simulation of EWD, CBD or RMD;
- autonomous modification of scientific repositories;
- cloud-provider integration owned by AtM.

## What v0.5.0 does not claim

- scientific certification of a repository merely because it is READY;
- validated scientific inference from AI-generated prose;
- scientific-model execution when no model has actually been run;
- replacement of canonical repository documentation;
- production-ready scientific decision support.

## Future development directions

Likely extension areas include:

- conversation import, retention policy and bulk conversation-history management;
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
- `releases/v0.5.0.md` — current release description;
- `releases/v0.4.0.md` — previous startup-integrity release description;
- `CHANGELOG.md` — release history.
