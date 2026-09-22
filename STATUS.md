# Application status

## Release status

**Current release: Ask the Model (AtM) v0.4.0 — Startup Qualification and Snapshot Integrity, released 2026-09-22.**

v0.4.0 retains the repository-grounded multi-chat capability introduced in v0.3.0 and adds offline startup qualification, an authoritative fail-closed installation gate for repository use/mutation, a backward-compatible repository-state v2 integrity field, deterministic local snapshot seals, fail-closed seal comparison before repository use, stable pre/post integrity checks around retrieval-index preparation and independent G-O0 verification of local repository state.

The project remains in the `0.x` initial-development series. The public API and repository-management surface are not yet considered stable enough for v1.0.

## Development after v0.4.0

The tagged public release remains v0.4.0. Development on `main` may be newer than that tag and is published separately through the **AtM Development** Flatpak repository.

Post-v0.4.0 work currently remains engineering/measurement-only:

- future GitHub Release publication is hardened for draft-first asset attachment and optional immutable releases;
- production retrieval/context policy constants are shared between runtime and benchmark tooling without changing their values;
- a production-policy R5 shadow run measures context efficiency while the frozen R5 gate remains unchanged and blocking.

Development Flatpak publications must expose the exact source `main` commit SHA so a development build can be distinguished reproducibly from the tagged v0.4.0 release artifact.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface and retrieval/provenance layer for locally managed AI models and repositories of scientific dynamical models. It is not itself a scientific model and does not replace the canonical models maintained in those repositories.**

The current scientific repository suite is fixed to:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

Each repository remains authoritative for its own model definitions, data, assumptions, provenance, validation and release boundaries.

## Current functional boundary

v0.4.0 provides:

- GTK 4 / Granite desktop shell packaged for the elementary OS 8 Flatpak runtime;
- local Ollama-compatible provider discovery on loopback;
- provider-managed completion-capable AI-model discovery;
- refreshable AI-model selector;
- streamed text-chat responses;
- independent in-memory multi-chat tabs;
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

## Verification for v0.4.0

The v0.4.0 development stack passes the Flatpak/Meson CI gate for startup qualification, authoritative runtime gating of repository grounding/mutation, repository-state migration, deterministic snapshot seals, G-S0 seal ordering, lifecycle seal enrollment/tamper rejection, explicit invalid-snapshot quarantine, stable pre/post snapshot identity across index preparation and the independent G-O0 state/snapshot/index/provenance oracle. The repository-grounded GTK interaction baseline remains inherited from the locally smoke-tested v0.3.0 release on elementary OS 8 / GTK 4.14.

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

Conversation history is stored only in application memory for the current process and is not persisted across restarts.

The Flatpak receives write access only to the dedicated `~/Ask the Model` directory for repository snapshots. It must not request broad Home or host filesystem access.

## Not implemented in v0.4.0

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

## What v0.4.0 does not claim

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
- `releases/v0.4.0.md` — current release description;
- `releases/v0.3.0.md` — previous repository-grounded release description;
- `CHANGELOG.md` — release history.
