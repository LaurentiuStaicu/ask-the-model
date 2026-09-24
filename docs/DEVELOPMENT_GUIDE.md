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

History reopen must reuse the persisted snapshot restore boundary: only explicitly archived conversations appear in History, reopening an archive must preserve `archived=true`, and the resulting tab must remain view-only until the exact persisted model/repository context passes the existing continuation qualification.

CONV-05c makes Archive the sole user-visible save action. Unarchived rows exist only as transactional working-state records needed by the durable-before-provider-history boundary; tab Close, normal shutdown and next-start cleanup discard them. The direct tab archive button uses a symbolic folder/archive icon instead of a generic lifecycle menu.

`ConversationExport.vala` remains layered above the read-only durable snapshot boundary. `ConversationPersistenceStore` maintains automatic JSON mirrors only for archived conversations under `~/Ask the Model/Conversation Exports/`, using stable conversation-ID filenames and atomic replacement. Permanent Delete removes both archive authorities from the user's perspective: the SQLite archived conversation and its managed JSON mirror, with export removal fail-closed before DB deletion. The existing narrow `~/Ask the Model:create` Flatpak permission is sufficient and must not be broadened. Import is not implied by the export format and requires a separate future design.

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

The Meson suite covers startup qualification, the fail-closed repository runtime gate, storage-boundary checks, Control DB identity/integrity, deterministic legacy import and cutover, copy-on-write repository generations, generation-consistent reads and stale-writer rejection, deterministic snapshot sealing, repository lifecycle, repository-generation conversation pinning, the independent G-O0 state/snapshot/index/provenance oracle, deterministic process-crash/restart qualification at snapshot/index promotion boundaries, archive safety, manifests, retrieval, grounding and citations.

The Flatpak workflow validates, in order:

1. release metadata contract;
2. README capability/design contract;
3. Flatpak build and tests;
4. publication only where the workflow rules permit it.

The R5 benchmark is intentionally deterministic and pinned. Development diagnostics may expand, but reviewed qrels/corpus/thresholds are not silently rewritten.

### Runtime optimization gate

The post-v0.5.0 optimization program has one session-only runtime gate owned by `OptimizationPolicy`.

The application always constructs this policy disabled. The header switch may enable it for the current process only; there is no persisted setting or environment override in the first implementation.

Runtime optimization code must receive/snapshot this policy through explicit program interfaces. It must not read GTK widget state directly and must not invent independent per-feature toggles. Each operation snapshots the mode at entry so a user change cannot partially alter an in-flight mutation or retrieval operation.

When the gate is OFF, runtime behavior must remain on the established baseline path. Test/measurement infrastructure such as OPT-A0, C0-M1 and D0 is not gated because it does not change ordinary application behavior.

### C0-M1 capacity measurement

C0-M1 is qualification infrastructure only. It must not reject Download/Update, select a safety reserve, delete old snapshots, preallocate repository payloads or otherwise alter ordinary runtime behavior.

The Linux measurement contract distinguishes:
- logical regular-file bytes from `st_size`;
- allocated bytes from Linux `st_blocks * 512`;
- filesystem identity from `st_dev`;
- bytes available to an unprivileged process from `statvfs().f_bavail * f_frsize`;
- inode/file slots available to an unprivileged process from `statvfs().f_favail`.

The measurement helpers use no-follow inspection and refuse symlink/special-object inputs rather than traversing through them. For a not-yet-created root, filesystem capacity is measured from the nearest existing ancestor without converting that ancestor into an authority decision.

The real C0-M1 workflow reuses the exact EWD/CBD/RMD SHA corpus frozen by the R5 retrieval benchmark. For each repository it downloads the exact GitHub tarball, runs the real ingest/storage/retrieval-index code in an isolated temporary AtM root, records fresh-install data-root peak allocation, records cache-root peak allocation while the archive remains present during index construction, quarantines the same-SHA snapshot with the production quarantine primitive and measures replacement coexistence during repair, then uploads deterministic JSON artifacts carrying AtM source commit and repository SHA provenance.

Peak sampling is measurement evidence, not a production reservation guarantee. Production capacity admission remains a later decision and must be derived from measured evidence, separate byte/inode formulas, filesystem grouping and ENOSPC recovery qualification.

### C0-E1 genuine ENOSPC qualification

C0-E1 is test-only qualification infrastructure. It uses bounded `tmpfs` mounts on a standard Linux CI VM to produce genuine filesystem-full failures rather than synthesizing an application error.

Two pinned real-repository paths are qualified:

- **RMD data-root exhaustion:** the exact RMD archive is ingested into an 8 MiB data filesystem. Extraction reaches a real `ENOSPC` write failure. The extraction staging tree is removed, no new final snapshot exists, the previously active Control DB generation/SHA remains unchanged, and a fresh-process verifier observes the same old authority.
- **EWD retrieval-index exhaustion:** the exact EWD archive is retained on an 8 MiB cache filesystem, the snapshot is successfully promoted on a separate roomy data root, and the production SQLite retrieval-index build reaches disk-full. Final and staging index files are absent after failure, the previous Control DB generation/SHA remains active, and the newly promoted snapshot is left only as an unreferenced/recoverable immutable snapshot. A fresh-process verifier confirms the same classification.

This establishes the fail-closed behavior required when real capacity is consumed after any future admission decision. It does not itself add admission, reserve sizing, GC, preallocation or a runtime `NO_SPACE` mapping. Production C0 must continue to assume that another process can consume capacity after preflight and therefore must preserve these recovery properties.


### C0-E2 genuine inode exhaustion qualification

C0-E2 is also test-only qualification infrastructure. It mounts a roomy byte-capacity `tmpfs` with a deliberately small `nr_inodes` limit and runs the exact pinned CBD repository ingest path.

The runner records `statvfs().f_bavail × f_frsize` and `f_favail` immediately before the attempted ingest, after the old repository authority has been seeded. Qualification requires:
- substantial byte capacity still available before the operation;
- an intentionally insufficient inode/file-slot budget for the pinned CBD snapshot;
- a genuine filesystem `ENOSPC` during extraction;
- no new final snapshot and no extraction staging residue;
- the previous Control DB generation/SHA and snapshot remaining unchanged;
- a fresh-process restart verifier confirming the old authority.

This evidence is specifically for C0-T3. It does not select the future inode reserve or production refusal threshold; it proves only that inode exhaustion is observable through the same fail-closed storage path and that admission must compare predicted new entries against unprivileged `f_favail`.


### C0-F1 phase-aware admission model

C0-F1 fixes the admission arithmetic without wiring it into Download/Update.

Inputs are:
- one availability record for each AtM root (`data`, `cache`, `state`), including `st_dev`, user-available bytes and user-available inode/file slots;
- an ordered set of operation phases, each carrying the additional bytes and entries attributable to each root during that phase;
- optional byte/inode headroom floors, still supplied as policy inputs rather than frozen constants.

The model groups roots by `st_dev`. For each filesystem group it:
1. uses the minimum contemporaneous availability reported by any root on that filesystem;
2. sums only the root requirements that coexist in the same named phase;
3. takes the maximum phase sum as the operation peak;
4. applies the strictest shared headroom floor for roots on that filesystem rather than summing duplicate reserve floors;
5. compares bytes and inode/file slots independently;
6. skips inode rejection only when the filesystem's inode budget is explicitly classified as not meaningful/known.

This deliberately avoids the incorrect pattern of summing independent per-root maxima that never coexist. It also avoids double-counting already-allocated old snapshots/quarantine: those blocks are already reflected in `f_bavail`; only the additional replacement requirement belongs in the operation phase.

C0-F1 remains a pure qualification model. No production path calls it yet, and no reserve value is selected by this slice. The model exists so C0-T1/T2/T3/T6/T7 can be exercised deterministically before runtime admission is authorized.


### C0-E3 Control DB state-root ENOSPC qualification

C0-E3 qualifies the third storage filesystem: the authoritative Control DB state root.

The test mounts an isolated bounded `tmpfs`, seeds one valid COMPLETE repository generation, consumes only qualification-owned free capacity down to a small measured headroom, and then calls the real guarded copy-on-write Control DB mutation. A qualifying failure must be a genuine disk-full/ENOSPC condition.

After the failed mutation the test removes only its filler file and requires:
- the active generation and repository SHA to remain unchanged;
- the guarded mutation to return no new generation identifier;
- zero persisted `CANDIDATE` generations;
- the Control DB to reopen and pass full validation;
- a separate fresh-process verifier to observe the same old authority.

This slice measures and qualifies fail-closed behavior; it does not yet turn the observed low-space boundary into a permanent state-root reserve. SQLite may react to `SQLITE_FULL` by rolling back a statement or the whole transaction depending on where the error occurs, so AtM retains its explicit rollback path and verifies the persisted result instead of assuming one internal SQLite outcome.

### C0-F2 archive pre-scan

C0-F2 adds a no-write archive inspection primitive for the future disk/inode admission path. It is deliberately not wired into Download/Update yet.

The inspector opens the completed repository tarball with libarchive, walks headers without extracting payloads and applies the same top-level-prefix, path traversal, link, special-entry and declared-size limits used by the extraction path.

It records:
- total archive header entries;
- unique regular files that would be materialized;
- unique directories that would be materialized, including implicit parent directories absent as explicit tar entries;
- total declared logical regular-file bytes;
- largest declared regular-file size;
- total new materialized filesystem entries, including the operation-owned extraction root.

This gives a pre-extraction inode requirement that is materially stronger than raw tar header count. For example, a file at `a/b/c.txt` consumes parent-directory inodes even if those directories do not appear as separate archive entries.

The inspection result is a planning input, not a replacement for extraction validation. Extraction still rechecks actual streamed byte counts and remains authoritative if archive payload data disagrees with header metadata.

No filesystem-allocation byte threshold is derived directly from logical tar sizes in this slice because compressed/deduplicating/sparse-capable filesystems can make logical bytes differ from allocated bytes. Production byte admission continues to use measured phase evidence plus filesystem availability, while C0-F2 closes the archive-entry/inode observability gap.

### C0-F3 two-checkpoint operation phase construction

C0-F3 converts explicit capacity predictions into the phase arrays consumed by C0-F1, but deliberately separates two different availability baselines.

#### Checkpoint A — before archive staging

Before the archive `.part` path is created, only the predicted archive allocation/inode demand belongs to the operation. This checkpoint can reject clearly insufficient cache capacity before deterministic staging begins.

No archive inspection exists yet at this point.

#### Checkpoint B — after completed archive + pre-scan, before mutation

After the completed archive exists, AtM must measure filesystem availability **again**. The archive is now already consuming the measured `f_bavail` / `f_favail` pool and therefore must not be charged a second time as additional demand.

From this post-download baseline the mutation plan contains:

1. **Extraction:** new snapshot allocation and the exact materialized-entry requirement from C0-F2.
2. **Index build:** the new snapshot remains while predicted additional index allocation is created.
3. **State commit:** snapshot + completed index remain while predicted state-root mutation allocation occurs.

This separation prevents a subtle double-counting error: using the pre-download archive requirement again after F2 inspection would subtract the same completed archive from capacity twice.

Fresh install, different-SHA update and same-SHA repair share the same post-download **additional-space** arithmetic when no speculative reclamation is allowed. Existing active snapshots and an invalid same-SHA snapshot renamed into quarantine already consumed capacity before Checkpoint B and are not counted again. Same-SHA repair carries an explicit `must_admit_before_quarantine` contract.

C0-F3 does not infer allocated bytes from tar logical bytes. Archive, snapshot, index and state predictions remain explicit later-policy inputs; F2 contributes the inode/materialization requirement; F1 groups roots by `st_dev`, applies reserve floors and evaluates phase overlap.

### C0-F4 capacity evidence registry

C0-F4 freezes reviewed C0 qualification evidence under `benchmarks/capacity-v1/evidence.json`.

The registry exists to separate **measurement evidence** from future **production policy**. It carries the source/action provenance and exact repository SHAs for C0-M1 plus the genuine byte, inode and state-root exhaustion qualifications from E1/E2/E3.

Repository capacity observations are keyed by exact `repository_id + repository_sha`. A later SHA must not inherit an older SHA's measured allocated-byte values as a capacity guarantee. If a future runtime policy has no exact qualified profile, its proactive prediction state is `UNQUALIFIED_NO_PROACTIVE_PREDICTION`; fail-closed ENOSPC recovery remains the safety fallback.

The checked-in M1 values are observations from one qualification environment, not filesystem-independent upper bounds. In particular they must not be converted into one archive-size multiplier across CBD/EWD/RMD.

The registry deliberately states `production_thresholds_selected: false`. Reserve sizes, refusal thresholds and production `NO_SPACE` behavior remain later reviewed policy. The invariant-registry CI gate validates evidence provenance, exact-SHA alignment with the frozen R5 corpus and preservation of this non-threshold boundary.

### C0-F5 NO_SPACE error and UI contract

C0-F5 defines the repository-capacity error surface before any production admission threshold is selected.

`RepositoryError.NO_SPACE` is a distinct repository error code. It is appended after the existing error codes so current RepositoryError numeric values are not renumbered.

The semantic class covers two later production cases:

1. **Preflight refusal:** capacity admission can prove the operation is already unsafe before the relevant deterministic staging/mutation checkpoint.
2. **Post-admission exhaustion:** a real write still fails because local capacity changed or prediction/reserve evidence was insufficient.

The detail text must distinguish these cases. Recommended wording is:
- preflight: `Not enough local storage to safely update <REPO>.`
- post-admission: `Local storage became full while updating <REPO>; the previous qualified repository remains active.`

The short LCD representation reuses the existing critical repository-error segment and changes its text from `ERR` to `NO SPACE` only for this error class. No extra LCD segment is added, so ordinary layout is unchanged. The full detail remains available through the existing tooltip/accessibility description.

`NO_SPACE` is not an offline/network condition. It must not set the repository `OFFLINE` state.

This slice adds only dormant error/UI plumbing. Current storage/admission code does not emit `RepositoryError.NO_SPACE`; therefore baseline Download/Update behavior is unchanged. When production C0 is later wired, admission-generated `NO_SPACE` must remain behind the OFF-by-default Optimizations snapshot. Mapping actual post-admission ENOSPC to this semantic class may be shared by both modes because it improves error precision without authorizing proactive admission, but that mapping must be separately reviewed with native errno/SQLite result preservation rather than message-string parsing.

SQLite `SQLITE_FULL` is the canonical database-full signal. Generic `SQLITE_IOERR` is not automatically equivalent to no-space; qualification has already observed that extremely tight state headroom can fail earlier during WAL/SHM setup. Native error classification must therefore preserve specific underlying results instead of treating every I/O error as `NO_SPACE`.

### C0-M2 state-root reserve scaling measurement

C0-M2 extends the C0-E3 Control DB disk-full qualification into a measurement matrix. It remains qualification-only and selects no production reserve.

The runner creates real Control DB history with the production guarded copy-on-write publication path, then constrains the state filesystem to a target remaining byte band and attempts one more guarded mutation.

The current matrix measures:
- 1, 100 and 1000 completed repository generations;
- target state headroom of 16 KiB, 32 KiB, 64 KiB and 128 KiB;
- Control DB allocated bytes before the constrained mutation;
- actual available bytes/inodes immediately before the mutation;
- mutation success/failure class;
- generation before/after;
- persisted CANDIDATE count;
- DB/WAL/SHM allocation after the qualification-owned filler is removed;
- the exact underlying error for failed cases.

A case passes the measurement harness only if it is atomic in one of two ways:
- `SUCCESS_ATOMIC`: the next generation becomes active, validation passes and zero CANDIDATE generations remain;
- `FAIL_CLOSED_OLD_AUTHORITY`: the old generation remains active, validation passes and zero CANDIDATE generations remain.

The harness does **not** require a chosen headroom band to succeed. Its purpose is to observe whether the success/failure frontier changes with Control DB history size while preserving authority invariants.

The resulting M2 artifact is evidence for a later state-root safety-reserve rationale. It must not be converted directly into a universal reserve without reviewing the observed matrix, SQLite/runtime version and filesystem environment.

### C0-M3 ext4 state-reserve portability measurement

C0-M3 repeats the C0-M2 constrained Control DB publication measurement on a bounded ext4 loopback filesystem.

The ext4 image is created with:
- 64 MiB filesystem size;
- 4096-byte blocks;
- 0% super-user reserved blocks (`mke2fs -m 0`), so unprivileged `f_bavail` is not intentionally reduced by ext4's default root reserve;
- normal ext4 journaling and allocation behavior.

The matrix uses 1 and 1000 completed generations at target headroom bands of 16/32/64/128 KiB. These two history sizes bracket the M2 range while keeping the portability run focused on filesystem behavior rather than repeating the entire tmpfs matrix.

The same atomicity rule applies:
- `SUCCESS_ATOMIC`, or
- `FAIL_CLOSED_OLD_AUTHORITY`,
with zero persisted CANDIDATE generations in either case.

Actual `f_bavail` after the qualification filler is recorded and is authoritative; the target headroom is only the requested calibration point because ext4 metadata/extent allocation can make the final free-byte count differ from the nominal target.

C0-M3 selects no production reserve. It tests whether the M2 tmpfs frontier is stable on a filesystem with persistent allocation and journaling semantics closer to a typical Linux home/storage volume.

### C0-F6 M2/M3 evidence freeze

C0-F6 extends the checked-in capacity evidence registry with the reviewed
state-root scaling and ext4 portability artifacts.

The registry now preserves:
- C0-M2 tmpfs observations at 1/100/1000 Control DB generations;
- C0-M3 ext4 observations at 1/1000 generations;
- exact Actions run IDs, artifact IDs/digests and source commits;
- actual unprivileged free bytes at each constrained mutation;
- Control DB allocated bytes and atomic success/fail-closed classification.

CI validates the reviewed matrices exactly. The evidence remains explicitly
qualification-only: `production_thresholds_selected` must remain false, and
the stable ~32–64 KiB observed frontier must not be silently promoted into a
runtime reserve without a separate policy decision and boundary qualification.

The registry also preserves a supplementary 24-case warm/cold SQLite sidecar
experiment. A pre-existing 32 KiB SHM sidecar reduced additional headroom
enough for 32 KiB to succeed, while the cold state required the 64 KiB band.
Because production guarded mutations open and close the Control DB store per
call, the cold-sidecar case is the conservative admission baseline; warm
sidecars may reduce actual demand but cannot justify a smaller reserve.

### C0-M4 historical amplification measurement

C0-M4 addresses the remaining unknown-SHA byte-prediction gap without changing runtime policy.

The frozen corpus under `benchmarks/capacity-v2/` contains six exact SHAs for each of EWD, CBD and RMD: five stratified points from recent default-branch history plus the exact C0-M1 pinned SHA. The corpus validator requires 18 unique SHAs and keeps the pinned anchors aligned with R5.

Every sample is measured with the same real ingest/storage/retrieval-index runner used by C0-M1. M4 derives transparent per-repository relationships from observables available after archive download/pre-scan:
- archive allocated bytes;
- snapshot logical regular bytes;
- materialized/observed entry count;
- fresh snapshot additional allocated peak;
- index-build additional allocated peak.

The qualification artifact evaluates simple envelope families and performs leave-one-out validation. A held-out SHA is never used to establish the envelope used to predict itself. Underprediction is reported as a failure/sensitivity measure rather than hidden by fitting the full corpus.

M4 deliberately does **not** choose a production predictor or margin. Its purpose is to determine whether repository-specific historical variability is stable enough to support a later conservative policy. If leave-one-out evidence is poor, unknown SHAs remain unqualified for proactive byte prediction and must not inherit stale exact-SHA measurements.

### Repository authority-mutation lease

When an operation snapshots optimization mode ON, repository Download/Update uses one application-owned exclusive nonblocking lease at `<state_root>/repository-mutation.lock` before any selected-repository staging or authority mutation begins.

The native lease helper opens the coordination file with `O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW`, requires a user-owned regular file, rejects multiple-hard-link state, revalidates the path against the opened inode and acquires `flock(LOCK_EX | LOCK_NB)`. The descriptor remains open for the complete operation and releasing/closing it drops the kernel-held lease. File existence alone is never interpreted as ownership.

The lock order is fixed:

`global repository mutation lease → filesystem staging/preparation → guarded Control DB transaction`

The existing SQLite generation guard remains mandatory defense in depth and is not replaced by the lease. Cooperative contention is surfaced as `RepositoryError.BUSY` before deterministic staging paths are modified.

The global lease applies only to authority-changing Download/Update work. Remote Refresh, existing-snapshot reads, snapshot/seal validation and ordinary grounding do not take the global authority lease. A missing/invalid derived retrieval index encountered by grounding remains the responsibility of the later per-SHA single-flight workstream, not B0.

This coordination contract is intentionally limited to AtM's supported local-storage/same-host boundary. SQLite WAL itself requires same-host shared memory and is not a cross-host network-filesystem design.

### Retrieval-index per-SHA single-flight

When an operation snapshots Optimizations ON, retrieval-index ensure keeps the valid-index fast path lock-free. If the exact index is missing or invalid, the worker joins one blocking exclusive build flight identified by repository ID plus exact snapshot SHA under:

`<state_root>/retrieval-index-locks/<repository_id>/<sha>.lock`

The coordination directory is created one component at a time and each application-owned component is required to be a real effective-user-owned directory. The lock file uses the shared native coordination-lease helper with defensive no-follow open and inode revalidation.

The fixed lock order is:

`optional global repository mutation lease → per-SHA index-build lease`

A grounding/read operation never acquires the global authority lease merely to rebuild derived cache. It may take only the per-SHA build flight inside the existing repository-preparation worker thread.

After a waiter acquires the per-SHA lease, it revalidates the final index. If the previous builder completed, the waiter returns that validated index as REUSED. Otherwise only the flight owner may remove an invalid final index or a real regular abandoned staging file and perform the rebuild/promotion. A symlink, directory or other unexpected staging object fails closed.

The per-SHA lock is blocking by design in this first slice and no arbitrary timeout is imposed. The wait occurs off the GTK main loop, and monotonic wait duration is measured internally for diagnostics. Lock files are persistent coordination metadata; normal release never unlinks them.

When Optimizations is OFF, the established post-v0.5.0 retrieval-index ensure path remains unchanged.

### Recovery fault qualification

The OPT-A0 recovery harness is test-only. Native checkpoint calls compile to no-ops in the production application; only the dedicated recovery helper is built with `ATM_TEST_FAULT_INJECTION`.

The blocking harness launches a helper subprocess with explicit checkpoint/control file descriptors, terminates it at named promotion boundaries and runs a new verifier process against the same isolated state root. This qualifies deterministic **process-crash** behavior and must not be described as proof of physical power-loss durability.

The initial oracle covers snapshot pre/post-rename and retrieval-index validated/pre/post-rename boundaries. It intentionally reports existing recovery gaps rather than repairing them; durability barriers, stale-staging cleanup and cross-process leases belong to later OPT workstreams.

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
