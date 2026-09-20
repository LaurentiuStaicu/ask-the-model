# Changelog

All notable public releases of Ask the Model are recorded here.

## Unreleased

No unreleased changes are documented yet.

## 0.3.0 - 2026-09-20

Repository-Grounded Multi-Chat.

### Repository lifecycle and deterministic retrieval

- implemented the fixed EWD/CBD/RMD repository lifecycle with tracked-branch SHA resolution, bounded safe extraction, strict manifest/CITATION validation and immutable snapshot promotion;
- implemented deterministic per-snapshot SQLite/FTS5 indexing with source roles, structured entities/relations, Markdown sections, tabular datasets, integrity checks and rebuild-from-snapshot behavior;
- added exact technical-ID, structured entity/relation, lexical BM25 and tabular row-key retrieval;
- added intent-aware source authority, logical-source deduplication, conversation-pinned scope and conservative Romanian/English query normalization;
- promoted manifest-declared status sources into retrieval authority;
- carried repository ID, repository version and exact snapshot SHA on evidence records;
- added deterministic multi-turn retrieval with inherited scope/intent/exact anchors and explicit clarification outcomes;
- retained embeddings as optional future scope rather than a requirement for v0.3.0.

### Grounded chat and provenance

- froze validated repository snapshots/index identity and selected local-AI model identity on the first Send of each chat;
- added current-turn grounded Ollama requests without persisting evidence blocks as normal chat-history context;
- made grounded provider-history commit transactional with retrieval-turn commit;
- resolved temporary `[S#]` labels against current-turn evidence before commit and rejected unknown labels;
- preserved deep-copied per-turn citation provenance;
- rendered compact user-visible numbered source references with repository/version/SHA/logical-source/locator/excerpt metadata and immutable source permalinks;
- retained ordinary zero-repository local chat as a separate streaming path.

### Multi-chat and interface

- added real independent chat tabs with separate Ollama history and ConversationSession state;
- added the compact New mini-tab, automatic semantic titles capped at three words and per-tab close controls;
- kept repository and AI-model selectors visible but insensitive after a conversation is pinned;
- restored each tab's pinned selector state when switching conversations;
- added the embedded status LCD and repository lifecycle annunciators;
- aligned AI-model and repository selectors to the same compact disclosure triangle and neutral tab-surface palette.

### Validation and governance

- added real-repository R4 citation-traceability CI across EWD, CBD and RMD;
- added versioned R5 benchmark/run schemas, a reviewed frozen corpus and deterministic real-corpus runner;
- the current frozen R5 baseline passes the provisional gates without changing qrels or thresholds: exact-ID Success@1 1.00, MRR 1.00, nDCG@5 0.9069, required Recall@5 0.9833, wrong-repository contamination@5 0.0207, RO–EN nDCG gap 0.0333, evidence traceability 1.00 and clarification-outcome accuracy 1.00;
- added atomic release-metadata and README capability/design contracts;
- added PR-only workflow concurrency, contribution/support guidance and structured issue forms.

### Scope boundary

v0.3.0 does not persist conversations across restarts, manage AI-model files or provider installation/settings, support arbitrary repositories outside EWD/CBD/RMD, execute scientific models or autonomously modify scientific repositories.

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
