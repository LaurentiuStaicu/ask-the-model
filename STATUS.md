# Application status

## Release status

**Current release: Ask the Model (AtM) v0.6.3 — planned release date 2026-10-10; qualified GISTEMP chart admission and Vala ownership correction.**

**Latest published release: v0.6.2 (2026-10-08).** The v0.6.3 candidate contains the merged PR #403 chart correction and will be published by the Flatpak release workflow only after the metadata, build and post-merge release gates pass. v0.6.2 remains the immutable current public release until v0.6.3 is published; v0.6.1 is the preceding GISTEMP chart release.

### v0.6.3 release

The release corrects live GISTEMP chart admission so the reviewed current EWD snapshot is accepted, while unknown repository identities, snapshots and source bytes remain rejected. It also splits repository-identity and snapshot-identity diagnostics and corrects the Vala ownership annotations for borrowed chart identity getters. The release retains the v0.6.2 durable-turn commit boundary and all existing chart-scope restrictions.

### v0.6.3 verification

Release publication is conditional on the metadata validation, Flatpak build/test job and post-merge CI gates. The full interactive GISTEMP path must still be checked on the published Flatpak; focused native sanitizer coverage is not represented as an end-to-end GUI test.

v0.6.0 retains the v0.5.0 repository-grounding, exact-context saved History, hardened SQLite state and startup-integrity boundaries. It adds complete-message semantic transcript presentation, accessible grounded Sources and semantic copy; a user-requested GISTEMP chart path with exact pinned-source revalidation and History reconstruction; and session-only repository optimizations that default to OFF.

### v0.6.2 release

The published v0.6.2 release contains the post-v0.6.1 grounded-turn durability correction. The v0.6.1 release remains immutable and historically unchanged. The correction adds the `SESSION-S0-013` durable-boundary invariant plus structural and semantic regression coverage.

### v0.6.2 verification

The v0.6.2 release candidate passed the post-merge Invariant Registry and Flatpak gates on the exact release commit. The Flatpak release workflow validates version/date metadata, README and publication ordering before building and publishing the immutable release artifact.

The project remains in the `0.x` initial-development series. The public API and repository-management surface are not yet considered stable enough for v1.0.

## v0.6.1 release

- Natural-language requests for the qualified GISTEMP chart are recognized alongside `/chart gistemp`.
- The chart reads the complete pinned, revalidated EWD GISTEMP series directly, rather than depending on sparse retrieval excerpts.
- Scope remains limited to the existing GISTEMP chart; no generic chart support or model-authored values are added.

## v0.6.1 verification

The post-merge release gate for commit `2d6ac8bc38efc657689f19e2c7904e6c81097468` passed all six workflows: C1-I9 purge orchestrator qualification, C1-I10 runtime purge integration qualification, A1-M12 runtime authority replay, A1-M12b lifecycle fsync EIO qualification, Invariant Registry and Flatpak. The published release contains `AskTheModel.flatpak` (480,440 bytes; SHA-256 `ca197f469f337ccf17b65623fa2a64410f7ee4d7d9574250e2620da650759512`).

## v0.6.0 release highlights

- Complete assistant responses receive restrained semantic presentation. Grounded Sources remain accessible buttons, semantic copy preserves their numbered references, and live/History turns share the same completed-turn projection.
- The explicit `/chart gistemp <grounded question>` request renders only the qualified complete annual global GISTEMP series from the exact pinned EWD snapshot. History stores a reconstruction recipe and rebuilds the chart from pinned source data; it does not persist numeric points or pixel state.
- The session-only **Optimizations** switch starts OFF. ON enables individually qualified repository coordination, capacity admission, D1 source-cap policy, selected S1 local durability and bounded C1 reclamation. OFF preserves the established baseline. Reclamation is limited to the qualified post-action path; background, startup and ENOSPC-triggered GC remain disabled.
- Grounded provider turns revalidate model and repository-generation identity before commit. Mismatch or cancellation fails closed before conversation history advances.
- CHART-01/03, PRES-09, D1, C1-I10, A1-M12/M12b, Presentation Interaction, Invariant Registry and Flatpak qualifications passed together on the audited candidate before the release metadata update. The final metadata-bearing head passed the required post-merge release CI gate; Flatpak then published `v0.6.1`.

The release does not claim scientific certification, arbitrary chart support, model-authored scientific values, remote-filesystem durability, or generic automatic cleanup. The scientific repositories remain authoritative for their own models, data and validation.

## v0.6.0 capabilities added after v0.5.0

v0.6.0 includes the OFF-by-default, session-only **Optimizations** gate. The gate is snapshotted once per operation; switching the UI later does not change an operation already in progress.

v0.6.0 also includes the qualified display-only **Presentation layer** for chat transcripts. Assistant responses are normalized as complete messages into an AtM-owned semantic document and rendered into the existing GTK text buffer with restrained headings, lists, quotes and code presentation. Markdown emphasis is flattened rather than shown as raw delimiters or reintroduced as bold/italic; bold is reserved for the `You:` and `Assistant:` labels, using neutral light/dark palette colors. User text is preserved literally, grounded Sources buttons remain separate, and provider/persistence/retrieval/scientific content is unchanged by the presentation projection. Live generation now uses a non-text spinner beside a temporary `Assistant:` label, a one-shot near-bottom-aware scroll to the start of that response, and a non-copyable dashed turn separator after the completed answer/Sources; provider chunks remain hidden until the complete response is normalized. Grounded Sources are real accessible buttons numbered in validated first-use citation order and shared by live/History rendering. Semantic transcript copy serializes source anchors as `[N]` while omitting spinner/separator anchors, and Markdown Source excerpts use the same qualified Presentation plain-text projection; only the immutable citation provenance link is active. PRES-08 now applies the same completed-turn separator decoration to live and History. This capability is part of v0.6.0 and is not retroactively attributed to the tagged v0.5.0 release.

With Optimizations ON, the currently qualified runtime optimizations are:
- one global cross-process repository authority-mutation lease for Download/Update;
- one per-SHA retrieval-index single-flight for missing/invalid derived indexes;
- shared read leases for positive repository generations used by repository-backed conversation grounding/History requalification, held for the active session lifetime so future GC can recognize live runtime roots;
- production local-capacity admission for Download/Update, using pre-download, post-download/pre-mutation and immediate pre-Control-DB-publication checkpoints;
- the D1 retrieval/context policy selected from the current-main D0 rebaseline: each Send keeps six ranked results per repository and the 32-KiB context budget, while the model-visible source cap changes from the OFF baseline of 12 to the qualified ON value of 4; frozen R5 quality/provenance gates remain blocking and the operation-level gate snapshot is fixed before the Send's first async boundary.

Capacity admission is deliberately conservative. Exact C0-M1 repository ID + SHA profiles may use their frozen allocated-byte evidence. A new/unknown SHA receives no inherited historical snapshot/index byte prediction because structural stress falsified that predictor as a conservative bound. Structural inode checks and the selected 128 KiB / four-inode Control DB publication headroom remain active. Only proven preflight insufficiency is surfaced as `NO SPACE`; post-admission storage failures retain the established fail-closed path.

With Optimizations OFF, optional optimization paths are not activated and the established baseline repository behavior is retained.


### Qualified chart presentation development

v0.6.0 includes a narrowly qualified GISTEMP chart presentation path. The reviewed GTK Chart/Data component is used by both live and History rendering. A chart is requested explicitly with `/chart gistemp <grounded question>`; the command is not model-derived and supplies no scientific values or chart configuration. The live path re-qualifies the pinned EWD snapshot through Admission → VerifiedSeries → ChartSpec before rendering and persists only the existing CHART-05 reconstruction recipe. The current admission remains limited to the complete annual global GISTEMP source, 1880–2025, and unsupported or unavailable data fails closed. No generic chart intent, arbitrary model-authored series, STEP/BAR, missing/event series or additional scientific source has been enabled.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface and retrieval/provenance layer for locally managed AI models and repositories of scientific dynamical models. It is not itself a scientific model and does not replace the canonical models maintained in those repositories.**

The current scientific repository suite is fixed to:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

Each repository remains authoritative for its own model definitions, data, assumptions, provenance, validation and release boundaries.

## Current functional boundary

v0.6.0 retains the v0.5.0 baseline and adds:

- GTK 4 / Granite desktop shell packaged for the elementary OS 8 Flatpak runtime;
- local Ollama-compatible provider discovery on loopback;
- provider-managed completion-capable AI-model discovery;
- refreshable AI-model selector;
- text-chat responses assembled from the local provider's streamed `/api/chat` transport;
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
- ordinary zero-repository local chat as a separate local-provider path;
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


- complete assistant responses rendered through the semantic Presentation document, with accessible grounded Sources and semantic transcript copy;
- live grounded-turn cancellation and exact model/repository-generation revalidation immediately before persistence commit;
- an explicit GISTEMP chart request limited to the complete annual global source and exact pinned EWD snapshot, with fail-closed History reconstruction;
- a session-only Optimizations switch defaulting OFF; qualified ON paths include repository leases, D1 source-cap 4, local capacity admission, selected S1 durability and bounded C1 reclamation, while OFF preserves baseline behavior.

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

## Verification for v0.6.0

The audited release candidate passed all 11 required workflows together before the synchronized metadata update: CHART-01, CHART-03, Presentation Interaction, D1, C1-I9, C1-I10, C1-M1, A1-M12, A1-M12b, Invariant Registry and Flatpak. The final metadata-bearing head must pass the same release CI gate before merge or tag creation. The chart qualification covers the native source/specification and GTK component, durable recipe reconstruction and Flatpak suite; it does not claim a dedicated full GTK Application interaction test for live request through restored History presentation.

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

## Not implemented in v0.6.0

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

## What v0.6.0 does not claim

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
- `releases/v0.6.1.md` — current release description;
- `releases/v0.5.0.md` — previous release description;
- `releases/v0.4.0.md` — previous startup-integrity release description;
- `CHANGELOG.md` — release history.
