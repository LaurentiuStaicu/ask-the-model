# Application status

## Release status

**Current release: Ask the Model (AtM) v0.2.2 — Documentation and public-metadata consistency, released 2026-09-20.**

v0.2.2 preserves the v0.2.0 Local Chat Baseline and the v0.2.1 release-distribution correction while aligning first-time user documentation, model-management and troubleshooting guidance, citation metadata, AppStream metadata and desktop-launcher wording with the implemented capability boundary. It does not change local-chat behavior.

The project remains in the `0.x` initial-development series. Repository-aware retrieval and scientific provenance are not implemented in this release.

## Development main — unreleased repository-aware application

Development `main` has advanced beyond the public v0.2.2 UI boundary with repository/retrieval work and a repository-aware GTK conversation path that remain unreleased.

The current development implementation includes:

- SHA-pinned, validated and immutable local snapshots for the fixed EWD/CBD/RMD catalog;
- deterministic per-snapshot index schema v2 validated against real EWD, CBD and RMD snapshots, including the manifest-declared `status_source` as a distinct authority role;
- exact technical-ID and structured entity/relation retrieval;
- safe FTS5/BM25 lexical retrieval;
- deterministic tabular row-key retrieval;
- intent-aware source authority and deduplication;
- conversation-pinned repository scoping;
- conservative Romanian/English query normalization;
- evidence records carrying repository ID, repository version and snapshot SHA;
- a deterministic repository router combining those primitives without embeddings;
- immutable per-conversation repository pinning that validates repository/index identity, supports valid zero-repository local chat and rejects post-freeze scope mutation;
- current-turn grounded Ollama request construction that keeps repository evidence transient rather than persisting grounding blocks into ordinary chat history;
- current-turn citation-label resolution into persistent provenance objects without fabricating metadata for unknown labels;
- real-repository R4 traceability checks across EWD, CBD and RMD;
- deterministic multi-turn retrieval state that can inherit repository scope, intent and exact anchors across follow-ups and return an explicit clarification outcome where needed;
- versioned R5 benchmark/run schemas, deterministic metric evaluation, provisional gates and CI for the benchmark contract, including clarification-outcome semantics;
- a reviewed frozen R5 seed corpus pinned to exact EWD/CBD/RMD v0.1.0 snapshots, with all required topic classes, Romanian/English/mixed coverage, unsupported cases and deterministic multi-turn cases;
- a deterministic real-corpus R5 runner that checks out the pinned SHAs, rebuilds the real indexes, executes the R3/R4 conversation/grounding path and evaluates the versioned run contract;
- a passing frozen R5 retrieval benchmark under the provisional gates; the current verified baseline on 2026-09-20 records exact-ID Success@1 1.00, MRR 1.00, nDCG@5 0.9069, required Recall@5 0.9833, RO–EN nDCG gap 0.0333, wrong-repository contamination@5 0.0207, evidence traceability 1.00 and clarification-outcome accuracy 1.00.

The unreleased development GTK path now exposes the fixed EWD/CBD/RMD repository selector and lifecycle controls, real multi-chat tabs with independent in-memory provider/session state, first-Send repository/model pinning, end-to-end grounded message generation and compact user-visible source references. Grounded turns validate temporary source labels before commit and preserve exact repository/version/SHA/logical-source/locator provenance for on-demand inspection. Ordinary zero-repository chat remains available and continues to stream normally.

The frozen R5 deterministic retrieval benchmark passes its provisional engineering gates on the pinned EWD/CBD/RMD corpus, and the real-repository R4 integration gate continues to exercise citation traceability independently of the GTK UI. This validates the unreleased development implementation against the current engineering contracts; it does not establish scientific validity of the underlying models. Therefore **v0.2.2 remains the current public release** and its user-facing capability boundary is unchanged.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface designed for querying, exploring and discussing repositories of scientific dynamical models through natural language. It is not itself a scientific model and does not embed, redefine or replace the canonical models maintained in those repositories.**

The intended dynamical-model repository suite currently includes Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD), with scope for future compatible repositories.

Each scientific model remains authoritative in its own repository. AtM is an access and interaction layer over those external project sources.

## Current functional boundary

v0.2.2 preserves the v0.2.0 functional baseline:

- the GTK 4 and Granite application shell;
- stable application ID and Meson build;
- desktop launcher, AppStream metadata and elementary-compatible icons;
- elementary OS 8 Flatpak packaging and GitHub Actions build verification;
- native desktop integration and automatic system color-scheme following;
- an X11-only Cairo renderer fallback when no explicit `GSK_RENDERER` override is present and no Wayland display is available;
- a functional prompt composer and Send action;
- discovery of a local Ollama-compatible API through `GET /api/tags`;
- preference for standalone Ollama on `127.0.0.1:11434`, with `127.0.0.1:11435` as compatibility fallback;
- capability inspection through `POST /api/show`;
- automatic exclusion of embedding-only models;
- a refreshable AI-model selector populated with completion-capable models;
- real model-scan progress and a short-lived scan-result status;
- streamed conversational requests through `POST /api/chat`;
- progressive assistant text display;
- explicit `think: false` on the default chat path;
- in-memory user/assistant history for the current application session.

## Provider and dependency boundary

AtM does not bundle, install, start, stop or update the local provider. The provider is an external dependency.

Compatibility is defined by the API behavior documented in `docs/DEPENDENCIES_AND_COMPATIBILITY.md`, not by a particular locally tested software version.

GPU acceleration is provider-owned. AtM itself does not contain vendor-specific GPU inference code and does not require a particular GPU model, vendor or acceleration backend.

## Data and model boundary

User prompt content is transmitted only when **Send** is activated. The current provider implementation addresses loopback only.

Conversation history is stored in application memory for the current session and is not persisted across restarts.

Scientific model repositories remain external sources. Their documentation, code, data, provenance, validation status and release boundaries remain authoritative.

AtM must not present an AI-generated explanation as if it were a canonical model result unless that result is explicitly supported by a future repository/provenance layer or by an actual model execution whose provenance is identified.

## Not implemented in v0.2.2

- repository ingestion or retrieval;
- repository-aware context selection for EWD, CBD or RMD;
- source/provenance presentation;
- conversation persistence across application restarts;
- application/provider settings;
- model installation, download or deletion;
- cloud AI providers;
- execution or simulation of scientific models;
- autonomous changes to scientific repositories.

## What v0.2.2 does not claim

- repository-grounded scientific answers;
- embedded scientific models;
- validated scientific inference;
- model execution or simulation;
- authoritative replacement of repository documentation;
- a production-ready scientific decision system.

See `releases/v0.2.2.md` for the current patch-release description, `releases/v0.2.1.md` for the release-distribution correction, `releases/v0.2.0.md` for the Local Chat Baseline, and `docs/DEPENDENCIES_AND_COMPATIBILITY.md` for compatibility requirements.
