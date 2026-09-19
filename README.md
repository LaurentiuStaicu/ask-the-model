<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="180">
</p>

<h1 align="center">Ask the Model (AtM)</h1>

<p align="center">
  <img alt="Version: 0.2.0" src="https://img.shields.io/badge/version-0.2.0-blue?style=flat-square">
  <img alt="Platform: elementary OS 8" src="https://img.shields.io/badge/platform-elementary%20OS%208-lightgrey?style=flat-square">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square">
</p>

Ask the Model (AtM) is a local desktop chat application for working with locally hosted AI models.

The project is being developed as a natural-language interface for scientific dynamical-model repositories such as Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD). Those repositories remain independent and authoritative for their own code, data, assumptions and validation.

**Current release: v0.2.0 — Local Chat Baseline.**  
This release provides functional local AI chat and local AI-model discovery/selection. Repository-aware retrieval, provenance and scientific-model context are planned but are not implemented yet.

## Current capabilities

- local Ollama-compatible chat;
- automatic discovery of installed chat-capable models;
- AI-model selection from the header bar;
- manual model refresh with scan progress and result feedback;
- streamed assistant responses;
- in-memory multi-turn conversation for the current session;
- automatic light/dark appearance following the desktop;
- elementary OS 8 Flatpak packaging;
- compatibility handling for the current X11/GTK renderer path.

AtM does not require Alpaca. Any compatible local Ollama service can provide inference.

## Requirements

For normal use, AtM requires:

- elementary OS 8 or a compatible Linux environment;
- a running local Ollama-compatible service;
- at least one installed model that advertises the `completion` capability.

The preferred local provider endpoint is `127.0.0.1:11434`. A secondary loopback endpoint is retained for compatibility with managed local-provider setups.

AtM does not install or manage Ollama and does not download or delete AI models.

See [Dependencies and compatibility](docs/DEPENDENCIES_AND_COMPATIBILITY.md) for the complete technical requirements.

## Project scope

AtM is the application interface, not a scientific model.

In v0.2.0, EWD, CBD and RMD are part of the intended future repository context, but AtM does not yet ingest or query their repository contents. AI responses in this release must therefore not be treated as repository-grounded or canonical scientific results.

## Build

### Native development build

```bash
meson setup build --prefix=/usr
meson compile -C build
./build/io.github.laurentiustaicu.ask_the_model
```

### Flatpak build

```bash
sudo apt install flatpak-builder
flatpak install -y appcenter io.elementary.Platform//8 io.elementary.Sdk//8
flatpak-builder flatpak-build io.github.laurentiustaicu.ask_the_model.yml --user --install --force-clean
flatpak run io.github.laurentiustaicu.ask_the_model
```

## Documentation

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
