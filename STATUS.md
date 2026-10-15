# Application status

## Release status

**Current release: Ask the Model (AtM) v0.7.1 — released 2026-10-14. Immutable public release.** This patch corrects transcript-embedded chart sizing and replaces misleading partial-evidence prose for chart-only GISTEMP and Energy Institute requests with concise deterministic responses, while preserving grounded answers for explicit interpretation, year-value and table requests.

The verified release workflow publishes the immutable [v0.7.1 release](https://github.com/LaurentiuStaicu/ask-the-model/releases/tag/v0.7.1). Issue #463 records the chart acceptance findings and release provenance. The historical v0.7.0 and v0.6.3 tags and bundles remain immutable and unchanged.

**Development status checked 2026-10-15:** v0.7.1 remains the latest immutable public release. Development `main` now additionally integrates the bounded, source-specific BNR BLS C01 quarterly ChartSpec v3, Data/renderer, schema-v5 History reconstruction and Application route through PRs #471–#479. This route is not part of the v0.7.1 artifact, and v0.8.0 has not been published. General CHART-01 qualification remains open in issue #343.

### Integrated development milestones

- **M11 startup cleanup reconciliation:** PR #433 (`c528b65a39b6f4e0ca46c695230c90cf027c7942`) reconciles managed exports before startup cleanup and retains unarchived SQLite rows when reconciliation is failed or unknown. The old stacked PRs #381/#384 were deliberately closed; #433 is the rebased, corrected integration.
- **CHART-M1c generic source-bound Evidence/SRA/series hardening:** PR #423 (`83fb4e93ed00cc793a51ff3fd88dd31b35efdb8d`) integrated the generic chain. PR #430 (`c35b39d45018221f51e256ed1dc9b353a5c24916`) consolidated descriptor binding, SRA identity/qualifier integrity and fail-closed contract tests. Old stacked PR #380 was closed as superseded, not left as a pending mainline gate.
- **Energy Institute qualification:** PR #435 integrated the source-specific TES VerifiedSeries wrapper; PR #438 (`84121ea7a3f7e616de86f068f8d3a726d843bd2e`) admitted exactly two complete four-file EWD snapshot bundles under admission profile v3. The approved F0-19 claim is reconstruction of the pinned processed EWD snapshot only, not independent raw-to-processed reproduction.
- **C2a/C2b native chart contracts:** PRs #439/#440 integrated source-bound ChartSpec v2 and the separate TES display projection while preserving GISTEMP v1 identities and rendering rules.
- **C2c/C2d/C2e application path:** PR #441 (`9c50ffac42dc4b2d1d36111451423aac4d5770d1`) integrated conversation-store v4; PR #442 (`cd7351cc3d8de3f868e5a7ebb07233a9e5f8099a`) integrated native EI History reconstruction; PR #443 (`66a32cbd8858f8ce0a49704c6c542ceb580d68c2`) integrated the source-qualified `/chart energy-institute` route, EI Chart/Data presentation and schema-v2 History dispatch. All nine workflows passed on exact C2e candidate `a82003b78042b129797f6f2851c79e3de282ee5b`; post-merge Flatpak and Invariant Registry passed.
- **PRES-09 GTK Application E2E:** PR #447 (`1b27af38109879ec620925af1862224c755d2734`) integrates the test-only GTK Application harness. All 12 exact-head workflows passed on candidate `f4c5c879fed905556bd7bc77c67c4a93735770b6`, including live EI v2 / GISTEMP v1 composer flows, Save to History, new-process History reconstruction, exact Data/provenance comparison and tampered-source fail-closed behavior. The test seams and provider/fixture overrides are test-only; no release metadata changed.
- **Documentation synchronization:** PRs #444/#445 aligned the implementation record, application status, chart spec, renderer and History contract with the integrated code. This status record now also includes the bounded BNR C01 SeriesContract gate from PR #454; this documentation update changes neither application code nor release metadata.
- **BNR BLS C01 quarterly SeriesContract:** PR #454 (`41b98fce58e7ce039b2174c391b1397be7241709`) integrates a native/test-only adapter from the exact five-file RMD panel and finalized SRA to the generic `AtmSeriesContract`. It retains 11 exact numeric observations and one authority-qualified `MISSING` at 2025-Q2, without a synthetic CSV row. All 10 exact-head workflows passed, including native ASan/UBSan and Flatpak's bundled tests. This does not mint a legacy `AtmVerifiedSeries`, add chart/History/application authority, or change the release.

- **BNR BLS C01 complete bounded route:** PRs #471–#479 are integrated in dependency order; final merge commit #479 is `91093e7c96be56703052a3eb30565d1c84c351e5` (2026-10-15). The stack adds source-specific VerifiedSeries, ChartSpec v3, bounded quarterly projection, a gap-aware Cairo renderer and GTK Data view, schema-v5 transactional migration and History reconstruction, plus `/chart bnr-bls-c01` and guarded natural-language routing. The exact-head CI runs inspected for all nine PRs passed, including CHART-01 native ASan/UBSan, CHART-03 GTK, CHART-04 resize, CHART-05 persistence, PRES-09 Application E2E, Presentation Interaction, Invariant Registry, Flatpak and existing durability/reclamation checks. Those are exact PR-head results, not a claim that the final v0.8.0 release artifact has been tested or published.

### Remaining qualification boundary

The public v0.7.1 artifact remains immutable and exposes only its two qualified chart routes: GISTEMP v1 and Energy Institute TES v2. Development `main` additionally contains the bounded BNR BLS C01 v3 route integrated by PRs #471–#479; that capability must not be attributed to the v0.7.1 download, and v0.8.0 is not yet published.

The BNR route admits only the exact pinned RMD snapshot `e43eb4e899f123d3b1aa0f6e20b7c8ed35434170` and one source-qualified quarterly profile. It keeps all twelve coordinates, with eleven numeric observations and a source-authorized `MISSING` at 2025-Q2. That quarter has no numeric payload and no physical CSV row; the GTK Data view displays `none` (not the internal zero sentinel), and the line renderer does not bridge the gap. History stores a reconstruction recipe and requalifies the pinned source; it does not store chart values, projected coordinates or pixels as independent scientific truth.

The exact PR-head workflow runs inspected for #471–#479 succeeded, including live Application/History E2E and tampered-source rejection. Before v0.8.0 publication, run the complete release gate on the final metadata-bearing commit, verify the produced artifact/tag/release, and manually accept that exact artifact on elementary OS 8. The PR CI is not a substitute for this final release and on-device evidence.

The bounded BNR gap policy does not generalize to arbitrary missing values or event/discontinuity series. Arbitrary sources, generic multi-series comparison, derived-series calculations, M4 reduction and independent Energy Institute raw-to-processed reproduction remain unqualified; issue #343 stays open for broader CHART-01 qualification. Optimizations remain session-only and default OFF.

### v0.7.1 patch release

v0.7.1 fixes the too-narrow natural viewport for transcript-embedded Energy Institute charts and prevents chart-only GISTEMP/Energy Institute requests from presenting short retrieval excerpts as incomplete data tables. The concise chart-only response points users to Chart/Data for values and source provenance. Explicit interpretation and year/value/table requests remain grounded requests. On-device acceptance on elementary OS 8 and exact-head CI are recorded in issue #463. This patch does not widen chart-source eligibility or close broader CHART-01 qualification in issue #343.

### v0.7.0 release

v0.7.0 adds the source-qualified Energy Institute TES v2 chart route alongside the existing GISTEMP v1 route. Saved recipes reconstruct chart/Data views from their exact pinned processed EWD source. Startup reconciliation retains recoverable unarchived conversation state when export reconciliation fails or remains uncertain, and generic SeriesContract validation binds scope and numeric payloads to authorized finalized SRA facts. These additions do not qualify independent Energy Institute raw-to-processed reproduction, BNR C01 application charting, arbitrary sources, generic multi-series/derived charts or broad missing/event/discontinuity semantics. Optimizations remain session-only and default OFF.

Automated application-level acceptance and the metadata-bearing release workflow are the evidence basis for publication. A separate manual launch/visual review of the produced Flatpak on a physical elementary OS 8 installation was not performed; that remaining acceptance item is explicitly tracked in issue #463.

### v0.6.3 release

The release corrects live GISTEMP chart admission so the reviewed current EWD snapshot is accepted, while unknown repository identities, snapshots and source bytes remain rejected. It also splits repository-identity and snapshot-identity diagnostics and corrects the Vala ownership annotations for borrowed chart identity getters. The release retains the v0.6.2 durable-turn commit boundary and all existing chart-scope restrictions.

### v0.6.3 verification

The immutable v0.6.3 release has been published after the release workflow completed. The focused native sanitizer coverage documented for this correction is not, by itself, a full end-to-end GUI interaction test; automated qualification and manual observation are distinct evidence classes.

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

With Optimizations OFF, optional optimization paths are not activated and the established baseline repository behavior is retained, apart from mode-independent correctness coordination. The global B0 repository-authority mutation lease and B2 shared generation lease for positive-generation grounding remain active in both OFF and ON modes; they do not authorize destructive GC.


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
