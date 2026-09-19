# Application status

## Release status

**Current release: Ask the Model (AtM) v0.2.0 — Local Chat Baseline, released 2026-09-19.**

v0.2.0 is the first AtM release with functional local AI conversation. It adds provider discovery, completion-capable AI-model selection, streamed responses and in-session conversation history on top of the v0.1.x application/documentation baseline.

The project remains in the `0.x` initial-development series. Repository-aware retrieval and scientific provenance are not implemented in this release.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface designed for querying, exploring and discussing repositories of scientific dynamical models through natural language. It is not itself a scientific model and does not embed, redefine or replace the canonical models maintained in those repositories.**

The intended dynamical-model repository suite currently includes Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD), with scope for future compatible repositories.

Each scientific model remains authoritative in its own repository. AtM is an access and interaction layer over those external project sources.

## Current functional boundary

v0.2.0 establishes:

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

AtM does not bundle, install, start, stop or update Ollama. The local provider is an external dependency.

The validated reference provider for v0.2.0 is Ollama 0.34.2. Compatibility is defined by the API behavior documented in `docs/DEPENDENCIES_AND_COMPATIBILITY.md`, not by an intentional hard pin to that exact provider version.

Alpaca is optional and is not an application dependency.

GPU acceleration is also provider-owned. The validated reference configuration includes an AMD Radeon RX 6700 using Ollama's Vulkan backend; AtM itself does not contain GPU inference code.

## Data and model boundary

User prompt content is transmitted only when **Send** is activated. The current provider implementation addresses loopback only.

Conversation history is stored in application memory for the current session and is not persisted across restarts.

Scientific model repositories remain external sources. Their documentation, code, data, provenance, validation status and release boundaries remain authoritative.

AtM must not present an AI-generated explanation as if it were a canonical model result unless that result is explicitly supported by a future repository/provenance layer or by an actual model execution whose provenance is identified.

## Not implemented in v0.2.0

- repository ingestion or retrieval;
- repository-aware context selection for EWD, CBD or RMD;
- source/provenance presentation;
- conversation persistence across application restarts;
- application/provider settings;
- model installation, download or deletion;
- cloud AI providers;
- execution or simulation of scientific models;
- autonomous changes to scientific repositories.

## What v0.2.0 does not claim

- repository-grounded scientific answers;
- embedded scientific models;
- validated scientific inference;
- model execution or simulation;
- authoritative replacement of repository documentation;
- a production-ready scientific decision system.

See `releases/v0.2.0.md` for the frozen release description and `docs/DEPENDENCIES_AND_COMPATIBILITY.md` for compatibility requirements.
