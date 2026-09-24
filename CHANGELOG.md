# Changelog

All notable public releases of Ask the Model are recorded here.

## Unreleased

### Optimization controls

- added one global session-only instrument-style `Optimizations  [switch]` control in the header for the post-v0.5.0 optimization program; every process starts OFF, the state is deliberately not persisted, the switch uses a warm red/green case with a neutral metallic slider and the same ridge-border language as the application frame, the LCD mirrors the state as `OPT OFF` / `OPT ON`, and future runtime optimizations must preserve the baseline path while OFF;
- kept the header compact by omitting redundant OFF/ON side labels; the native GTK switch uses a subdued warm red trough when OFF and a subdued warm green trough when ON, while the LCD retains the explicit `OPT OFF` / `OPT ON` text state.
- added the first runtime optimization behind that gate: when Optimizations mode is ON, repository Download/Update acquires one global nonblocking cross-process authority-mutation lease before staging and holds it through guarded Control DB commit and cleanup; cooperative contention fails before deterministic staging is touched, while Optimizations OFF preserves the v0.5.0 baseline path and ordinary read/grounding paths remain outside the global lease.
- added per-SHA retrieval-index single-flight behind Optimizations ON: valid existing indexes remain lock-free, missing/invalid indexes use one blocking cross-process build flight under the qualified state root, waiters revalidate and reuse a completed index, and the flight owner recovers regular abandoned staging while refusing non-regular staging; Optimizations OFF retains the baseline index lifecycle.

### Qualification infrastructure

- added C0P-F9 checked-in M1 phase inode evidence: the validator now locks same-SHA replacement to snapshot entries + 1 extraction root, fresh install to replacement + 5 AtM storage-tree directories, and retrieval-index construction to the four-entry cache peak observed independently for CBD/EWD/RMD;
- added C0P-M7 state-root inode headroom measurement: the cold-sidecar guarded Control DB path is exercised with 1/2/3/4/8 unprivileged inode slots available while byte capacity remains abundant, preserving atomic success/fail-closed invariants without selecting a production inode reserve;
- added C0P-F7 checked-in M6 structural stress evidence and validator: snapshot prediction remains covered in all six fixtures, retrieval-index prediction remains explicitly 0/6 with the limiting RMD Markdown case at 17.2604× actual/prediction, and CI prevents the structurally falsified historical hybrid from being silently restored as a conservative unknown-SHA index predictor;
- added C0P-M6 structural capacity qualification: valid synthetic AtM fixtures stress row-heavy CSV, entity-heavy JSON and section/token-heavy Markdown within current parser limits, while the M4/M5 hybrid remains fitted only on the frozen 18 real training SHAs; underprediction is preserved as evidence and no production margin or predictor is selected;
- added C0P-M5 no-refit out-of-sample capacity qualification: the frozen 18-SHA M4 corpus is the only training set, while 12 disjoint EWD/CBD/RMD holdouts at ranks 4/12/20/40 are measured with the real ingest/index runner and evaluated without refitting or automatically adding a margin; this remains evidence-only and selects no production predictor or threshold;
- added C0-M4 historical capacity-amplification qualification: a validated 18-SHA EWD/CBD/RMD corpus reuses the real M1 ingest/index runner, derives per-repository observable-to-allocated-byte envelopes, and performs leave-one-out underprediction tests without selecting any production predictor or margin;
- extended C0-F6 with the supplementary 24-case SQLite sidecar-sensitivity artifact: warm SHM allows lower additional headroom, while cold sidecars retain the 32 KiB fail / 64 KiB success frontier; the checked-in evidence therefore fixes cold-sidecar state as the conservative future admission baseline without selecting a reserve;
- extended the C0-F6 checked-in capacity evidence registry with the exact M2 tmpfs and M3 ext4 state-headroom matrices, artifact digests and source provenance; CI now freezes the reviewed 32–64 KiB qualification frontier while continuing to enforce that no production reserve/threshold is selected;
- added C0-M3 ext4 portability measurement for the state-root reserve: the M2 constrained guarded-Control-DB mutation is repeated on a 64 MiB ext4 loopback filesystem with 0% root-reserved blocks at 1/1000 generations and 16/32/64/128 KiB target headroom, recording actual unprivileged availability and atomic success/fail-closed behavior without selecting a production reserve;
- added C0-M2 state-root reserve scaling measurement: the real guarded Control DB publication path is exercised after 1/100/1000 completed generations at 16/32/64/128 KiB target headroom, recording atomic success versus fail-closed behavior and DB/WAL/SHM footprints without selecting a production reserve;
- added C0-F5 dormant `RepositoryError.NO_SPACE` contract and UI mapping: the error code is appended without renumbering existing RepositoryError values, the existing critical LCD `ERR` segment becomes `NO SPACE` only for this class, accessibility retains the full local-capacity detail, and no storage/admission path emits the new error yet so baseline runtime behavior is unchanged;
- added C0-F4 checked-in capacity evidence registry and validator: M1/E1/E2/E3 artifact provenance and exact EWD/CBD/RMD SHAs are frozen under `benchmarks/capacity-v1`, measured profiles are explicitly exact-SHA-only, unknown SHAs cannot inherit stale byte predictions, and CI enforces that no production threshold/reserve has been selected by this qualification data;
- added C0-F3 two-checkpoint operation phase-plan qualification: pre-download cache admission models only archive staging, while post-download/pre-mutation admission remeasures availability and deliberately excludes the already-present archive to avoid double-counting; extraction/index/state phases use explicit predicted allocations, F2 materialized entries supply data-root inode demand, C0-F1 evaluates shared-filesystem overlap, and same-SHA repair explicitly requires admission before quarantine; no production threshold or Download/Update wiring is introduced;
- added C0-F2 no-write archive pre-scan infrastructure for future capacity admission: the completed tarball can now be inspected before extraction/quarantine using the same path/link/type/declared-size safety limits, while reporting logical regular-file bytes and the unique materialized file/directory count including implicit parent directories and the extraction root; this is not yet wired into runtime admission;
- added C0-M1 measurement-only capacity qualification primitives and a real pinned-repository workflow: logical vs allocated bytes, filesystem/device grouping inputs, unprivileged block/inode availability, fresh-ingest peak sampling, index-build cache peak sampling with the archive retained, and same-SHA quarantine/replacement peak sampling for the frozen EWD/CBD/RMD corpus; no production disk-space admission threshold or runtime refusal is introduced by this slice;
- added C0-E1 genuine ENOSPC qualification on bounded tmpfs filesystems: RMD extraction failure cleans staging and preserves the old repository authority across restart, while EWD SQLite index disk-full leaves no index staging/final artifact, preserves old authority, and classifies the already-promoted new snapshot as recoverable unreferenced state; no production admission or NO_SPACE behavior is introduced;
- added C0-E2 genuine inode-exhaustion qualification using a byte-roomy but inode-limited tmpfs and the exact pinned CBD ingest path; the evidence records pre-operation byte/inode availability, requires real ENOSPC from inode/file-slot exhaustion, cleans extraction staging, preserves the old repository authority across a fresh-process restart check, and still introduces no production inode threshold or admission behavior;
- added C0-F1 phase-aware admission arithmetic as qualification-only code: roots are grouped by filesystem device, only simultaneously active phase requirements are aggregated, bytes and inode/file slots are evaluated independently, shared reserve floors are not double-counted, unknown inode budgets do not create false rejection, and overflow/inconsistent grouping fail closed; no Download/Update path uses the model yet and no reserve threshold is selected;
- added C0-E3 state-root ENOSPC qualification for the authoritative Control DB: a guarded copy-on-write mutation is exercised under genuine low-space tmpfs conditions, the test requires no authority advance and no persisted CANDIDATE generation, removes only its qualification filler, then reopens/validates the DB in a fresh process; the slice still does not choose a permanent state reserve or alter runtime admission;
- added a test-only deterministic crash/restart qualification harness for snapshot promotion and retrieval-index promotion, using explicit subprocess checkpoints plus a fresh-process JSON restart oracle; production builds contain no runtime fault-injection switch and no durability/recovery behavior is changed by this slice;
- qualified the current baseline behavior around promotion boundaries: a crash before snapshot rename leaves no repository authority, a crash after snapshot rename but before Control DB commit leaves an unreferenced promoted snapshot requiring recovery, pre-rename index crashes leave staging requiring recovery, and a post-rename index remains a valid derived cache.

## 0.5.0 - 2026-09-24

Saved Conversation History and State Hardening.

### CI and publication hardening

- split the Flatpak workflow into a read-only verification/build job and a main-only publication job, so pull-request verification no longer receives repository-content write permission;
- preserved the existing `flatpak` verification job identity while transferring only the verified bundle/repository outputs to publication through pinned GitHub artifact actions;
- extended the release-workflow contract validator to enforce the single scoped `contents: write` grant, the build-to-publication dependency and the verified-artifact handoff.
- anchored generated development-repository Git operations to `GITHUB_WORKSPACE`, explicitly trusted only that checked-out workspace for containerized Git ownership checks, and added contract coverage for both boundaries.


### Control State and repository-generation hardening

- added an application-owned SQLite `control-state.sqlite3` foundation with AtM identity/schema checks, foreign-key enforcement, WAL + FULL durability and fail-closed integrity validation;
- added deterministic import of legacy repository-state v1/v2 into a candidate generation and a verified one-time no-replace cutover that never rewrites the legacy JSON source;
- switched development runtime repository-state authority to Control DB: a valid existing DB wins, while an invalid existing DB fails closed without JSON fallback;
- changed normal repository-state mutation to atomic copy-on-write generations, preserving previously COMPLETE generations and rejecting stale expected-generation writers;
- made repository-state reads generation-consistent so one in-memory view cannot mix rows from different active generations;
- pinned non-empty repository-backed conversation grounding and active sessions to the exact immutable repository generation used for validation, while leaving ordinary zero-repository chat independent;
- kept repository grounding read-only with respect to repository authority and required snapshot seals to be persisted before a repository can be pinned for a conversation;
- added Control DB schema v2 with atomic v1→v2 migration and persistent trigger defense-in-depth against direct mutation of COMPLETE generations, invalid active-generation transitions and rewrite of migration history;
- hardened every Control DB connection with SQLite defensive mode, untrusted-schema mode, disabled legacy double-quoted strings and explicitly enabled triggers, and established SQLite 3.37.0 as the minimum supported library version for this path;
- required `SQLITE_OPEN_NOFOLLOW` on every production Control DB open so a symlinked authority path fails closed before bootstrap, migration, validation or repository-state access;
- required every opened Control DB authority to be genuinely read/write via `sqlite3_db_readonly()`, preventing SQLite's historical read-only fallback from qualifying as writable runtime authority;
- qualified the XDG Control DB state root with the same real-directory, current-owner, no-group-or-other-write and exclusive write-probe boundary used for startup storage, aborting startup qualification before authority resolution when that state root is unsafe;
- corrected the effective SQLite compatibility floor from 3.31.0 to 3.37.0 because Control DB schema v1/v2 uses STRICT tables; build-time, runtime, tests and documentation now agree;
- added `STATE-S0-001` through `STATE-S0-013` qualification invariants plus Flatpak/Meson regression coverage for foundation, import, cutover, authority precedence, copy-on-write history, stale-writer rejection, session generation pinning, schema-level immutability, connection hardening, no-follow authority opens, writable-authority qualification and state-root qualification.

### Conversation persistence foundation

- defined a separate application-owned `conversations.sqlite3` schema and hardened validation layer, with STRICT tables for conversation identity/model/repository generation, pinned repository snapshots, committed provider-history messages and durable citation provenance;
- added fail-closed semantic validation for repository-scope mismatch, incomplete committed turn pairs, invalid grounded-role state and citation provenance that does not match the conversation's pinned repository set;
- added `SESSION-S0-002` plus Meson regression coverage for bootstrap/reopen, foreign/newer database rejection, valid grounded persistence semantics, invalid citation semantics and symlink rejection;
- added a native atomic mutation API that generates local conversation/message identity, writes repository pins transactionally, allocates turn/sequence numbers inside the store and commits each complete user/assistant turn with its citations and metadata in one SQLite transaction;
- citation provenance is checked against the conversation's exact pinned repository/version/SHA set before write, and late failures roll back already-inserted messages/citations so the previous durable boundary remains unchanged;
- added `SESSION-S0-003` and regression coverage for ordinary and grounded commits, raw-vs-display grounded content, turn sequencing, pre-write provenance rejection and late transactional rollback;
- wired the qualified conversation-store mutation boundary into the Vala conversation domain and development GTK send path while keeping restore/navigation for later gates;
- preserved exact frozen repository ID/version/SHA pins for persistence instead of re-reading a newer Control DB active generation;
- both grounded and ordinary provider calls now support deferred provider-history mutation, and the shared turn committer advances `OllamaConversation` only after the durable SQLite turn succeeds; failed persistence therefore leaves provider history unchanged;
- when conversation persistence cannot be opened, development chat remains explicitly available as unsaved in-memory session state; if a grounded retrieval/session commit has already advanced and the durable turn then fails, that tab is made non-continuable rather than continuing with divergent state;
- added `SESSION-S0-004` with domain-level regression coverage for durable-before-provider-history ordering, failed-write history preservation, explicit unsaved mode and frozen repository-pin propagation.
- CONV-03a adds read-only durable conversation list/snapshot materialization in one SQLite read transaction, preserving exact model identity, repository pins, ordered provider/display message content and citation provenance;
- restored `OllamaConversation` history is rebuilt strictly from durable `provider_content` user/assistant pairs; snapshot loading does not yet authorize continuation, which remains CONV-03b;
- added `SESSION-S0-005` plus native/domain regression coverage for coherent snapshot reads and exact provider-history rebuild.
- CONV-03b adds exact continuation qualification: repository-backed restore can reconstruct grounding from an explicit historical COMPLETE Control DB generation without changing `active_state`, then durable repository generation/version/SHA pins must match the reconstructed context exactly;
- local model inventory now exposes name+digest pairs for restore qualification; a persisted digest must match exactly, while conversations created without a digest remain name-pinned only;
- historical restore failures do not mark the current runtime repository invalid, and generation 0 remains valid only for zero-repository conversations;
- added `SESSION-S0-006` with regression coverage for historical-generation restore, active-authority non-mutation, missing-generation fail-closed behavior, exact model digest checks and durable repository-pin equality.
- closed the remaining citation-provenance write gap before GTK restore: the Vala/native committed-turn bridge now persists the canonical immutable permalink derived from the pinned repository SHA/source locator and snapshot restore returns it unchanged;
- added `SESSION-S0-007` regression coverage for immutable-permalink round-trip through the durable conversation store.
- CONV-04a adds startup restore of non-archived durable conversations into the existing scrollable Gtk.Notebook as explicitly view-only tabs; transcript text, grounded citation buttons and persisted immutable permalinks are restored while prompt/Send remain disabled;
- startup restore removes the default pristine `New` tab only when it is still completely untouched, so asynchronous qualification cannot discard user input begun during startup;
- added `SESSION-S0-008`; continuation qualification/Send enablement remain CONV-04b and archive/delete lifecycle remains CONV-04c.
- CONV-04b now requalifies restored tabs against the exact durable AI model name/digest and historical repository generation/version/SHA context before enabling continuation; successful qualification starts the ConversationSession on the reconstructed historical grounding while leaving selectors locked to that context;
- failed or temporarily unavailable continuation context remains explicitly read-only and can be retried after local model discovery/refresh, repository refresh/update, or completion of another active generation without changing durable history or current repository authority;
- added `SESSION-S0-009`; archive/delete lifecycle remains CONV-04c.
- CONV-04c lifecycle semantics are refined by conversation-store schema v2: explicit tab Close now persists only an independent `open_on_startup=0` state, application shutdown leaves startup-open state unchanged, and startup restores only non-archived conversations still marked open; existing v1 stores migrate transactionally with prior conversations defaulting to open;
- Archive remains distinct from Close and now also clears startup-open state atomically; permanent Delete still requires explicit confirmation and relies on foreign-key cascades so repository pins, messages and citations are removed in the same transaction, while failed close/archive/delete persistence leaves the open tab and durable history intact;
- added a compact per-tab conversation-actions menu, archive/unarchive storage APIs for future history management, permanent-delete APIs and `SESSION-S0-010` regression coverage.
- reconciled STATUS and architecture documentation after CONV-04c so the tagged v0.4.0 release boundary remains historical while development `main` is accurately described as having the complete CONV-01→CONV-04c persistence lifecycle; archived-history browsing/export/import remain future work.
- CONV-05a adds a modal Conversation History surface for durable chats that are no longer open: closed chats can be reopened and archived chats can be unarchived and reopened; history reopen deliberately reuses the restored read-only tab path and exact continuation qualification rather than repinning to current model/repository authority.
- history reopen persists startup-open state before clearing archive state so a failed unarchive leaves the conversation archived and excluded from startup restore; regression coverage now qualifies that ordering and retained startup-open state after unarchive.
- added `SESSION-S0-011`; history reopening is qualified independently from later export/import work.
- CONV-05b adds the deterministic versioned JSON export format at `~/Ask the Model/Conversation Exports/`; CONV-05c makes explicit Archive the only user-visible save operation, so unarchived working chats do not create JSON mirrors and are discarded on tab Close, normal application shutdown or cleanup at the next startup after an interrupted session.
- replaced the per-tab conversation-actions menu with a direct symbolic Archive button; History now contains only explicitly archived conversations, reopening an archive preserves its archived identity, and every History row exposes a separately confirmed destructive `Delete permanently` action.
- archived JSON mirrors use private directory/file modes and atomic consistent replacement; permanent deletion is fail-closed with respect to the managed export, so the SQLite archive is not reported deleted while its JSON copy remains.
- added regression coverage for deterministic serialization, archive-only mirror creation, cleanup of unarchived leftovers, archived resynchronization and fail-closed managed-export deletion; import, retention policy and bulk history management remain future work.
- reconciled the enforced session invariant registry with the CONV-05c lifecycle and added `SESSION-S0-012` for archive-only managed JSON mirrors and fail-closed permanent deletion.


- retuned the CONV-05c per-tab save control to use Adwaita `document-save-symbolic` with the explicit tooltip/accessibility label `Save to History`, replacing the ambiguous generic folder symbol; behavior is unchanged.

### Startup qualification fixes

- accept the effective Flatpak runtime identity in both `runtime/ID/ARCH/BRANCH` and legacy `ID/ARCH/BRANCH` forms while still rejecting wrong ref kinds, IDs, architectures and branches;
- align G-S0 deployment fixtures with the fully qualified runtime ref observed in real `/.flatpak-info` metadata and retain regression coverage for the legacy triple form.

### Release/distribution hardening

- changed future GitHub Release publication to draft-first `create → attach asset → publish` ordering so future immutable releases can be enabled without losing the Flatpak asset;
- added a release-workflow contract test for draft resumption, exact-commit guards and immutable-release fail-closed behavior;
- made the generated `flatpak-repo` development channel record the exact source `main` Git SHA in a machine-readable `SOURCE_COMMIT` marker, its README and publication commit message.

### Retrieval measurement

- centralized the unchanged production retrieval/context limits in `src/retrieval_policy.h` so runtime code and measurement tooling share one policy definition;
- added a measurement-only production-policy companion to frozen R5, including context precision, required-evidence context recall, model-visible source count, evidence bytes and latency diagnostics;
- retained frozen R5 as the blocking retrieval-quality gate; the production-policy run remains observational and does not change runtime limits.

## 0.4.0 - 2026-09-22

Startup Qualification and Snapshot Integrity.

### Startup qualification

- added asynchronous G-S0 startup qualification before local AI-provider discovery without blocking the GTK main thread;
- added deterministic effective Flatpak deployment qualification and platform fingerprinting;
- added fail-closed qualification of the dedicated `~/Ask the Model` storage boundary using no-follow descriptor checks;
- added durable startup-qualification records while keeping volatile provider availability outside installation qualification;
- preserved explicit development/unpackaged execution as unqualified rather than presenting it as a release installation;
- made successful platform/storage startup qualification an authoritative runtime gate for non-empty repository grounding and repository mutation, while retaining ordinary zero-repository chat and keeping explicit read-only Refresh separate.

### Repository snapshot integrity

- added deterministic `snapshot seal v1` over sorted relative paths, semantic file mode, file size and per-file SHA-256 content;
- rejected symlinks and unsupported filesystem entry types while sealing;
- added backward-compatible repository-state schema v2 with exact SHA/version plus an optional/enrolled local snapshot seal;
- migrated legacy schema-v1 state only after strict local snapshot/version/index validation succeeds;
- verified an enrolled seal before any retrieval-index reuse/rebuild and classified mismatch as `SNAPSHOT_INVALID`;
- added pre/post seal checks around both startup reconciliation and normal lifecycle index preparation to detect local mutation during preparation;
- rejected locally modified sealed snapshots before conversation grounding while preserving the expected persistent seal;
- made an integrity-invalid repository require the existing Download action;
- added explicit same-SHA repair that downloads the exact archive before moving local content, quarantines the invalid real-directory snapshot for diagnosis and only then validates/promotes the replacement;
- refused to follow or quarantine symlink/non-directory snapshot paths as valid repair sources.

### Regression coverage

- added clean-install, unpackaged-development, invalid-state and missing-snapshot G-S0 tests;
- added deterministic seal tests for file order, content, executable mode, mtime stability, symlink rejection and add/remove behavior;
- added schema-v1 to schema-v2 seal migration, malformed-seal and wrong-SHA enrollment tests;
- added isolated lifecycle tests proving legacy seal enrollment, pre-index seal rejection, repair-action activation and fail-closed grounding after local snapshot modification;
- added storage regressions proving invalid snapshot quarantine preserves diagnostic content and rejects symlink snapshots;
- added fail-closed lifecycle coverage for repository grounding/mutation before or after negative installation qualification;
- added an independently linked G-O0 oracle for persistent state, exact snapshot identity, schema-v2 snapshot seals, SQLite integrity/foreign keys and source provenance;
- added a separate-process production-READY divergence harness and controlled corruption matrix so production readiness is not verified by calling the same readiness path again.

### Build / repository security

- pinned all externally referenced GitHub Actions in the Flatpak workflow to verified full-length commit SHAs while retaining version comments, and enabled weekly Dependabot updates for those action references.

### Documentation

- corrected the v0.3.0 compatibility limitations so they no longer incorrectly list repository ingestion/retrieval as unavailable, and added a CI guard against reintroducing that contradiction;
- tightened the repository README by moving detailed model-selection, quantization, storage and performance guidance to the dedicated model/troubleshooting documents while preserving first-use discovery guidance;
- aligned `docs/MODEL_GUIDE.md` with the v0.3.0 capability boundary and removed stale v0.2.2 references;
- strengthened the README design/validation contract so detailed model-management material does not drift back into the landing page.

## 0.3.0 - 2026-09-21

Repository-Grounded Multi-Chat.

### User-facing capabilities

- added the fixed EWD/CBD/RMD repository selector to the GTK conversation flow;
- added explicit repository Refresh and Download/Update lifecycle controls;
- added a subdued repository/model status LCD;
- added independent multi-chat tabs with per-tab in-memory provider and retrieval state;
- froze AI-model and repository snapshot identity on the first Send for each chat;
- retained ordinary zero-repository streaming chat;
- added repository-grounded current-turn generation;
- added compact numbered source references;
- added source-detail windows with repository/version, exact snapshot SHA, source path/locator, logical source ID, readable excerpt and immutable GitHub permalink;
- added readable plain-text presentation for Markdown source evidence after local GTK smoke testing.

### Repository lifecycle and safety

- implemented exact tracked-branch SHA resolution before repository download;
- implemented bounded safe archive extraction and strict repository-manifest validation;
- implemented immutable validated local snapshots under `~/Ask the Model/Repositories`;
- implemented fail-closed remote identity refresh;
- hardened multi-repository Refresh so an early failure cannot leave stale remote state on unvisited selected repositories;
- preserved previous valid snapshots and indexes across successful updates;
- added service-level cancellation coverage for repository I/O;
- restricted Flatpak filesystem access to the dedicated `~/Ask the Model` directory.

### Retrieval and provenance

- implemented deterministic per-snapshot SQLite/FTS5 retrieval index schema v2;
- indexed manifest-declared status, canonical, structural, evidence, tabular and implementation roles;
- added exact technical-ID, structured entity/relation, lexical BM25 and tabular row-key retrieval;
- added deterministic source authority, deduplication and repository scoping;
- added conservative Romanian/English runtime normalization;
- added deterministic multi-turn retrieval state with explicit clarification outcomes;
- attached repository ID, repository version and exact snapshot SHA to evidence records;
- kept retrieved repository content as untrusted current-turn data rather than persistent provider-history instructions;
- resolved temporary model-visible source labels into persistent provenance objects;
- generated immutable commit-pinned GitHub source links.

### Benchmark and regression gates

- added real-repository grounding traceability checks across EWD, CBD and RMD;
- added a reviewed frozen R5 corpus and deterministic real-corpus runner;
- added versioned R5 run schemas, metrics, provisional gates and per-topic diagnostics;
- kept benchmark corpus/qrels/threshold immutability separate from development diagnostics;
- verified a non-canonical live-repository compatibility smoke without modifying the canonical frozen benchmark;
- expanded regression coverage for lifecycle, index integrity, grounding, citation resolution, conversation pinning and cancellation.

### GTK and interface hardening

- aligned AI-model and repository selectors to the compact neutral suite palette;
- fixed GTK 4.14 citation-detail presentation by replacing the unreliable TextChildAnchor/Popover path with a transient source-detail window;
- converted Markdown/HTML presentation syntax in source excerpts into readable plain text without executing repository content as GTK/Pango markup;
- locally smoke-tested grounded EWD+CBD+RMD chat, selector freeze, source-detail windows and immutable permalinks on elementary OS 8 / GTK 4.14.

### Documentation and contributor experience

- added `docs/USER_INTERFACE_GUIDE.md`;
- added `docs/DEVELOPMENT_GUIDE.md` with code map, invariants, extension points and release workflow;
- updated README, STATUS, AppStream, desktop metadata, CITATION and release contracts for the repository-grounded capability boundary;
- retained synchronized release metadata validation and README capability/design validation before Flatpak builds.

### Scope boundary

v0.3.0 does not add persistent conversations, in-app AI-model file management, provider service management, arbitrary unreviewed repository origins, scientific-model execution/simulation or autonomous modification of scientific repositories.

EWD, CBD and RMD remain authoritative for their own scientific content and validation.

## 0.2.2 - 2026-09-20

Documentation and public-metadata consistency correction.

### Included

- redesigned the main repository README around first-time local-AI user needs;
- added step-by-step setup expectations, automatic model-discovery explanation and practical model-management guidance;
- added `docs/MODEL_GUIDE.md` for model sources, GGUF import, quantization, storage, deletion, hardware and licensing;
- added `docs/TROUBLESHOOTING.md` for provider, model, performance, storage and Flatpak diagnostics;
- reduced README visual density with compact metadata badges, smaller explanatory text and progressive disclosure for secondary sections;
- corrected `CITATION.cff` to the current release and capability boundary;
- corrected AppStream and desktop-launcher metadata so implemented local chat is clearly separated from the planned scientific-repository roadmap;
- preserved the v0.2.1 release asset rather than mutating an already-published version.

### Application status

The local-chat behavior is unchanged from v0.2.1. This patch changes documentation and packaged public metadata only.

### Scope boundary

Repository-aware retrieval, scientific provenance, persistent conversations, provider settings and in-app AI-model download/delete remain unimplemented.

## 0.2.1 - 2026-09-19

Installation and release-distribution correction.

### Included

- automatic publication of `AskTheModel.flatpak` as a GitHub Release asset;
- stable latest-release direct-download path for the Flatpak bundle;
- clear normal-user installation order for Flatpak, provider, model and AtM;
- explicit separation of end-user requirements from native/Flatpak development dependencies;
- documented elementary OS / Ubuntu development package names;
- Node 24-compatible `actions/github-script@v9` in the release workflow;
- integrity guard that refuses to attach a missing release asset if the release tag does not point to the workflow commit.

### Application status

The local-chat behavior is unchanged from v0.2.0. This patch corrects installation and distribution plumbing and documentation without introducing new chat or scientific-repository functionality.

### Scope boundary

The external provider/model boundary, loopback-only provider behavior and scientific-model boundary remain unchanged.

## 0.2.0 - 2026-09-19

Local Chat Baseline.

### Included

- functional local chat through an Ollama-compatible `/api/chat` provider;
- local provider discovery on `127.0.0.1:11434` with `11435` compatibility fallback;
- installed-model enumeration through `/api/tags`;
- capability inspection through `/api/show`;
- automatic exclusion of embedding-only models;
- refreshable AI-model selector;
- real model-scan progress and temporary scan-result feedback;
- streamed assistant responses;
- in-memory multi-turn conversation history;
- automatic elementary OS color-scheme following;
- X11-specific Cairo renderer fallback for the GTK/GSK compatibility path;
- elementary OS 8 Flatpak packaging and continuous Flatpak build verification;
- documented provider, dependency and compatibility requirements.

### Application status

v0.2.0 is the first functional local-chat baseline. The application can discover compatible local AI models, select a chat-capable model and maintain a streamed in-session conversation.

The preferred architecture uses a standalone local Ollama-compatible service. The inference provider remains external to AtM.

### Compatibility

- provider compatibility is defined by the required Ollama-compatible API behavior rather than a particular locally tested provider version;
- hardware acceleration is handled by the external provider and is not part of AtM's hardware requirements;
- the default chat request sets `think: false` to reduce hidden-reasoning latency on supported models.

### Scope boundary

This release does not implement scientific-repository ingestion or retrieval, repository context selection, source provenance, persistent conversations, provider settings, AI-model management or scientific-model execution.

AtM remains an application interface. EWD, CBD, RMD and other scientific repositories remain canonical for their own model content and scientific status.

## 0.1.1 - 2026-09-19

Application-scope and documentation correction.

### Included

- clarified AtM as a local conversational access layer for scientific model repositories;
- explicitly documented EWD, CBD and RMD as the initial repository suite;
- added canonical application-boundary documentation in `STATUS.md`;
- added citation metadata in `CITATION.cff`;
- added standardized repository identity and release documentation;
- aligned desktop and AppStream metadata with the application's actual purpose.

### Application status

This release does not add AI chat functionality. It corrects and formalizes the application's documented purpose, repository relationship and metadata.

AtM does not contain, redefine or replace EWD, CBD, RMD or other scientific models. Each model remains canonical in its own repository.

### Scope boundary

AI chat functionality, repository access, local provider integration, conversation management and settings are not part of this release.

## 0.1.0 - 2026-09-19

Initial application baseline.

### Included

- GTK 4 and Granite 7 application shell;
- stable application ID `io.github.laurentiustaicu.ask_the_model`;
- Meson build and installation configuration;
- freedesktop desktop launcher metadata;
- AppStream application metadata;
- elementary-compatible application icon sizes;
- standardized Ask the Model project identity and MIT licensing.

### Application status

This release establishes the native application shell and desktop integration baseline.

### Scope boundary

AI chat functionality, local model-provider integration, conversation management and settings are not implemented in this release.

Ask the Model is an application interface. It does not contain or constitute an AI model itself.
