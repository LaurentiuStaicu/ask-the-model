# Development guide

Ask the Model is intentionally split into a small set of boundaries that contributors should preserve while extending the application.

## Project role

AtM is a local-first GTK application that connects:

```text
user
  ↓
Ask the Model
  ├─ local provider / AI model
  └─ validated scientific repository snapshots
       ↓
     retrieval + provenance
```

AtM is not the AI provider, does not own external AI-model files, and is not itself one of the scientific dynamical models it helps explore.

## Current v0.4 architecture

### Provider layer

`src/OllamaProvider.vala` owns provider discovery, model enumeration/capability inspection and streamed chat requests.

The current implementation addresses loopback endpoints only.

### Conversation layer

`ConversationSession.vala`, `ChatRequestBuilder.vala` and the GTK tab state keep model identity, repository scope, in-memory history and current-turn grounding separate.

The first Send freezes model/repository identity for that chat.

Durable history reopen must reuse the persisted snapshot restore boundary: reopening a closed or archived conversation may change only its lifecycle visibility state before reconstructing the saved snapshot, and the resulting tab must remain view-only until the exact persisted model/repository context passes the existing continuation qualification.

### Startup qualification and repository lifecycle

`StartupQualificationService.vala`, `StartupQualificationRecord.vala`, `startup_qualification.c` and the G-S0 native bridge qualify the effective packaged deployment, the dedicated repository-storage boundary and persistent repository state before local readiness is accepted.

`RepositoryLifecycleService.vala` coordinates:

- fixed catalog identity;
- remote SHA/version refresh;
- download/update staging;
- snapshot validation;
- deterministic local snapshot-seal computation;
- stable pre/post seal checks around index preparation;
- integrity-invalid runtime state and same-SHA explicit repair;
- diagnostic quarantine of invalid real-directory snapshots before replacement;
- index construction/validation;
- conversation grounding preparation;
- an authoritative installation-qualification gate for non-empty repository grounding and repository mutation.

Current development runtime repository-state authority is owned by the Control State stack:

- `control_state.c`, `data/schemas/control-state-v2.sql` and `data/schemas/control-state-v1-to-v2.sql` define and validate the current application-owned SQLite authority and its atomic schema migration;
- `data/schemas/control-state-v1.sql` is retained as the qualified legacy Control DB format used for v1→v2 migration compatibility and regression coverage, not as the bootstrap format for new databases;
- `ControlStateNative.vala` exposes the narrow native bridge;
- `ControlRepositoryState.vala` provides generation-consistent Vala reads and guarded copy-on-write mutation;
- `RepositoryState.vala` remains the legacy v1/v2 JSON parser/store used for migration compatibility, regression coverage and recovery evidence, but it is no longer normal runtime authority once the Control DB exists.

The one-time cutover accepts legacy JSON only when `control-state.sqlite3` is absent. A valid existing Control DB wins unconditionally; an invalid existing Control DB fails closed and must not silently fall back to JSON. Normal repository mutations create fresh immutable COMPLETE generations, and repository-backed conversations retain the exact generation from which their repository scope was prepared.

Current Control DB hardening is layered rather than relying on one check: schema-v2 triggers defend persisted generation semantics; every connection requires SQLite >= 3.37.0 plus defensive/untrusted-schema/DQS/trigger configuration; production opens use `SQLITE_OPEN_NOFOLLOW` and require a genuinely read/write `main` database; and startup qualifies the XDG state root before any Control DB existence check, cutover or authority open.

`snapshot_seal.c` computes the local integrity key; `repository_reconcile.c` performs offline exact-SHA reconciliation for G-S0. Native ingestion and validation helpers live in the corresponding C modules under `src/`.

### Retrieval

The deterministic v1 retrieval stack is split across:

- `retrieval_index*.c` — per-snapshot SQLite/FTS5 index lifecycle;
- `retrieval_query.c` — exact/structured/lexical/tabular lookup;
- `retrieval_rank.c` — deterministic ranking;
- `retrieval_scope.c` — allowed repository scope;
- `retrieval_normalize.c` — conservative bilingual normalization;
- `retrieval_router.c` — intent/routing composition;
- `retrieval_conversation.c` — bounded multi-turn retrieval state;
- `grounding_context.c` — bounded untrusted evidence context;
- `citation_labels.c` / `conversation_grounding.c` — temporary source labels and persistent provenance objects.

### Presentation

`Application.vala` owns the GTK shell, selectors, status LCD, tabs, transcript, prompt composer and source-detail windows.

Repository excerpts are treated as untrusted data. Markdown evidence is converted to readable plain text for display rather than executed as GTK/Pango markup.

## Non-negotiable invariants

Changes should preserve the following unless a deliberate architecture change is reviewed:

1. **Exact snapshot identity.** Repository-grounded evidence is tied to an exact Git SHA.
2. **Fail-closed update.** A failed refresh/update must not convert stale or partial state into READY.
3. **Immutable validated snapshots.** Validated repository files are not modified in place.
4. **Regenerable indexes.** Retrieval indexes are cache artifacts rebuilt from immutable snapshots.
5. **Per-chat freeze.** Model and repository identity do not silently change after the first Send.
6. **Current-turn evidence.** Grounding blocks do not accumulate permanently in provider history.
7. **Untrusted repository text.** Retrieved content is data, not instructions.
8. **Canonical scientific boundary.** AtM does not redefine EWD/CBD/RMD scientific status.
9. **Narrow Flatpak filesystem permission.** Do not replace the dedicated application directory with broad Home/host access.
10. **Git SHA and local seal are distinct.** The upstream Git commit SHA is revision provenance; the snapshot seal only detects local content change.
11. **Seal before trust.** A previously sealed snapshot must match its persistent seal before index rebuild/reuse or conversation grounding.
12. **Stable preparation.** Snapshot identity must remain stable across index validation/rebuild before persistence or grounding.
13. **Explicit repair, never silent repin.** A persistent seal mismatch activates repair; same-SHA repair must download the exact archive before quarantining local content and must not replace the expected seal merely because local files changed.
14. **Quarantine is diagnostic.** Invalid real-directory snapshots may be atomically renamed for diagnosis during explicit repair; symlink/non-directory snapshot paths are never followed as repair sources.
15. **Installation qualification before repository authority.** Non-empty repository grounding and repository mutation require successful G-S0 platform/storage qualification; ordinary zero-repository chat remains valid and explicit Refresh remains read-only.
16. **Independent verification is not runtime authority.** G-O0 must remain separately linked from production readiness/seal/repair helpers; it verifies local artifacts in tests/diagnostics but does not become an application READY source.
17. **Single Control DB authority after cutover.** If `control-state.sqlite3` exists, it is the only runtime repository-state authority; invalid Control DB state fails closed and must not fall back to legacy JSON.
18. **Copy-on-write repository history.** Normal repository-state mutations never edit an already COMPLETE generation; they create and validate a successor generation and atomically advance the active pointer.
19. **Generation-consistent access.** A state reader captures one COMPLETE generation before loading repository rows, and a stale writer must be rejected if the active generation has advanced.
20. **Conversation generation pinning.** A repository-backed conversation is prepared read-only from exactly one immutable repository generation and retains that generation identity for its lifetime; later repository updates belong to future conversations.
21. **Schema-level generation immutability.** Control DB schema v2 must prevent direct SQL mutation/deletion of COMPLETE generation state, invalid active-generation transitions and rewrite of migration history.
22. **Hardened SQLite connections.** Every Control DB connection must meet the SQLite >= 3.37.0 floor and enforce defensive mode, untrusted-schema mode, disabled DQS parsing and enabled triggers before schema work.
23. **Real writable authority file.** Production Control DB opens must reject symbolic-link paths and must verify that SQLite opened the `main` database read/write.
24. **Qualified Control DB state root.** The XDG state root must pass the G-S0 real-directory/ownership/write-boundary qualifier before Control DB authority is inspected, created or opened.

## Extension points

### Additional repository types

Do not add arbitrary repository URLs directly from downloaded manifests.

A new repository family should define:

- application-controlled catalog identity;
- supported manifest contract;
- required paths and retrieval roles;
- validation fixtures;
- exact source identity rules;
- benchmark coverage.

### New retrieval methods

Embeddings, vector search or reranking are optional future stages.

Add them only when benchmark evidence demonstrates a material improvement over the deterministic baseline and their local storage/memory/latency cost is justified.

Do not relax the frozen benchmark or provenance requirements to make a new method pass.

### New providers

A provider implementation should preserve the separation between:

- application UI;
- provider process/API;
- external model artifacts.

Document endpoint semantics, model capability discovery, streaming behavior and privacy implications.

### Scientific-model execution

Executing EWD/CBD/RMD is a separate capability from retrieving and discussing repository evidence.

Any future execution path must expose the executed model version, inputs, parameters, outputs and provenance rather than presenting generated text as model execution.

## Tests and gates

The Meson suite covers startup qualification, the fail-closed repository runtime gate, storage-boundary checks, Control DB identity/integrity, deterministic legacy import and cutover, copy-on-write repository generations, generation-consistent reads and stale-writer rejection, deterministic snapshot sealing, repository lifecycle, repository-generation conversation pinning, the independent G-O0 state/snapshot/index/provenance oracle, archive safety, manifests, retrieval, grounding and citations.

The Flatpak workflow validates, in order:

1. release metadata contract;
2. README capability/design contract;
3. Flatpak build and tests;
4. publication only where the workflow rules permit it.

The R5 benchmark is intentionally deterministic and pinned. Development diagnostics may expand, but reviewed qrels/corpus/thresholds are not silently rewritten.

## Release workflow

A version bump is a deliberate release operation, not a side effect of ordinary development.

The synchronized version-bearing surfaces are defined in:

```text
.github/release_metadata_contract.json
```

They include Meson, CITATION, README, STATUS, CHANGELOG, release notes and AppStream metadata.

A release pull request should also update user-facing documentation whenever the capability boundary changes.

## Contribution workflow

Before opening a pull request:

- read `STATUS.md`;
- read the relevant architecture/acceptance document;
- keep the change narrowly scoped;
- add or update regression coverage for behavior changes;
- update documentation if the public/developer contract changes;
- let the Flatpak workflow complete.

For UI changes, report the target GTK/elementary environment and verify both light/dark appearance when relevant.

For repository/retrieval changes, report the exact repository SHAs or test fixtures used.

## Documentation map

- `README.md` — public landing page and first-use path;
- `STATUS.md` — current release and capability boundary;
- `docs/USER_INTERFACE_GUIDE.md` — user-facing controls and state behavior;
- `docs/ARCHITECTURE.md` — application architecture;
- `docs/CONVERSATION_PERSISTENCE_ARCHITECTURE.md` — planned durable conversation storage, transaction and restore contract;
- `docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md` — repository/retrieval design;
- `docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md` — acceptance gates;
- `docs/DEPENDENCIES_AND_COMPATIBILITY.md` — build/runtime/provider compatibility;
- `docs/TROUBLESHOOTING.md` — diagnostics;
- `.github/CONTRIBUTING.md` — contributor entry point.
