# Application status

## Release status

**Current release: Ask the Model (AtM) v0.2.2 — Documentation and public-metadata consistency, released 2026-09-20.**

v0.2.2 preserves the v0.2.0 Local Chat Baseline and the v0.2.1 release-distribution correction while aligning first-time user documentation, model-management and troubleshooting guidance, citation metadata, AppStream metadata and desktop-launcher wording with the implemented capability boundary. It does not change local-chat behavior.

The project remains in the `0.x` initial-development series. Repository-aware retrieval and scientific provenance are not implemented in this release.

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
