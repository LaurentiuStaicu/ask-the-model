# Changelog

All notable public releases of Ask the Model are recorded here.

## Unreleased

## 0.4.0 - 2026-09-22

Startup Qualification and Snapshot Integrity.

### Startup qualification

- added asynchronous G-S0 startup qualification before local AI-provider discovery without blocking the GTK main thread;
- added deterministic effective Flatpak deployment qualification and platform fingerprinting;
- added fail-closed qualification of the dedicated `~/Ask the Model` storage boundary using no-follow descriptor checks;
- added durable startup-qualification records while keeping volatile provider availability outside installation qualification;
- preserved explicit development/unpackaged execution as unqualified rather than presenting it as a release installation.

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
- added storage regressions proving invalid snapshot quarantine preserves diagnostic content and rejects symlink snapshots.

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
