# Changelog

All notable public releases of Ask the Model are recorded here.

## Unreleased

No unreleased changes recorded yet.

## 0.2.0 - 2026-09-19

Local Chat Baseline.

### Added

- functional local chat through an Ollama-compatible `/api/chat` provider;
- provider discovery on `127.0.0.1:11434` with `11435` compatibility fallback;
- capability inspection through `/api/show`;
- automatic exclusion of embedding-only models;
- refreshable AI-model selector;
- real model-scan progress and temporary scan-result feedback;
- streamed assistant responses;
- in-memory multi-turn conversation history;
- elementary OS color-scheme following;
- X11-specific Cairo renderer fallback for the validated GTK/GSK compatibility case;
- elementary OS 8 Flatpak network access required for host-local provider communication;
- documented provider, dependency and compatibility requirements.

### Changed

- standalone Ollama is now the preferred provider architecture;
- Alpaca is optional rather than an application dependency;
- the default chat request sets `think: false` to reduce hidden-reasoning latency on supported models;
- README, STATUS, architecture, AppStream and citation metadata now describe the functional local-chat baseline.

### Compatibility

- validated with Ollama 0.34.2;
- validated with hardware-accelerated local Ollama inference;
- provider compatibility is based on the required Ollama-compatible API behavior rather than an exact version pin.

### Still not implemented

- scientific-repository ingestion/retrieval;
- repository context selection and provenance;
- conversation persistence;
- application/provider settings;
- model management;
- scientific model execution.

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

AI chat functionality, local model-provider integration, conversation management, settings, and other end-user features are not yet implemented.

### Scope boundary

Ask the Model is an application interface. It does not contain or constitute an AI model itself.
