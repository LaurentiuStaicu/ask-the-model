<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="180">
</p>

<h1 align="center">Ask the Model (AtM)</h1>

<p align="center">
  <img alt="Version: 0.2.0" src="https://img.shields.io/badge/version-0.2.0-blue?style=flat-square">
  <img alt="Platform: elementary OS 8" src="https://img.shields.io/badge/platform-elementary%20OS%208-lightgrey?style=flat-square">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square">
</p>

Ask the Model (AtM) is a local AI chat interface designed to query, explore and discuss repositories of scientific dynamical models through natural language. Its intended repository suite includes Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD).

AtM is the conversational application layer. It does not contain, redefine or replace those scientific models; each scientific repository remains canonical for its own code, data, assumptions, provenance and validation state.

> **Current release: v0.2.0 — Local Chat Baseline.**  
> Local AI chat is functional. Repository ingestion, repository-grounded retrieval and provenance-aware answers are not implemented yet, so v0.2.0 should not be described as repository-aware scientific chat.

## What v0.2.0 provides

- native GTK 4 / Granite desktop application for elementary OS;
- automatic light/dark appearance following the desktop;
- local Ollama-compatible provider discovery on loopback;
- preferred standalone Ollama endpoint at `127.0.0.1:11434`, with `11435` retained as a compatibility fallback;
- automatic capability inspection and exclusion of embedding-only models;
- a refreshable AI-model selector in the header bar;
- real model-scan progress and a short-lived scan result;
- streamed assistant responses through `/api/chat`;
- in-memory multi-turn conversation history for the current application session;
- `think: false` on the default chat path to avoid long hidden reasoning latency on supported reasoning models;
- an X11-only Cairo renderer fallback for the tested GTK/GSK compatibility case;
- elementary OS 8 Flatpak packaging and GitHub Actions build verification.

## Requirements and compatibility

The recommended runtime is the elementary OS 8 Flatpak build. AtM does **not** bundle or manage an inference engine. A local Ollama-compatible service must already be running.

For v0.2.0:

- standalone Ollama on `127.0.0.1:11434` is the preferred provider;
- `127.0.0.1:11435` is a compatibility fallback for managed local frontends;
- Alpaca is optional and is **not** an AtM dependency;
- AtM requires the provider operations used by `GET /api/tags`, `POST /api/show` and `POST /api/chat`;
- only models advertising the `completion` capability are presented as chat models;
- Ollama 0.34.2 is the validated reference provider for this release, but the application is not intentionally pinned to that exact version;
- GPU acceleration is owned by the provider, not by AtM. The validated reference configuration includes an AMD Radeon RX 6700 using Ollama's Vulkan backend. CPU inference remains a provider concern and may be substantially slower.

See [Dependencies and compatibility](docs/DEPENDENCIES_AND_COMPATIBILITY.md) for the complete runtime, API, sandbox and build requirements.

## Data and architecture boundary

AtM v0.2.0 sends prompts only to the selected loopback provider implemented by the application. Conversations are kept in memory and are not persisted across application restarts.

The Flatpak requires network sharing so it can reach the host-local HTTP provider. This Flatpak permission is broader than loopback at the sandbox level, while the current AtM provider implementation itself only addresses `127.0.0.1`.

Scientific repositories remain external and authoritative. AtM must not present an AI-generated explanation as a canonical scientific-model result unless a future repository/provenance layer can support that claim.

## Not implemented in v0.2.0

- repository ingestion or retrieval;
- EWD/CBD/RMD context selection;
- source citation or provenance tracing;
- conversation persistence;
- provider/settings UI;
- model download or deletion;
- cloud-provider integration;
- execution or simulation of scientific models;
- autonomous modification of scientific repositories.

## Build and package

### Native development build

Build dependencies are GTK 4, Granite 7, libsoup 3, json-glib 1.0, Vala and Meson 1.0 or newer.

```bash
meson setup build --prefix=/usr
meson compile -C build
./build/io.github.laurentiustaicu.ask_the_model
```

### Flatpak build for elementary OS

AtM uses `io.elementary.Platform//8` and `io.elementary.Sdk//8`.

```bash
sudo apt install flatpak-builder
flatpak install -y appcenter io.elementary.Platform//8 io.elementary.Sdk//8
flatpak-builder flatpak-build io.github.laurentiustaicu.ask_the_model.yml --user --install --force-clean
flatpak run io.github.laurentiustaicu.ask_the_model
```

## Project documentation

- [Application status](STATUS.md)
- [Architecture boundary](docs/ARCHITECTURE.md)
- [Dependencies and compatibility](docs/DEPENDENCIES_AND_COMPATIBILITY.md)
- [Interface design requirements](docs/INTERFACE_DESIGN_REQUIREMENTS.md)
- [Terminology](docs/TERMINOLOGY.md)
- [Changelog](CHANGELOG.md)
- [v0.2.0 release notes](releases/v0.2.0.md)
- [Citation metadata](CITATION.cff)

## License

MIT License.
