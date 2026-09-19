<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="180">
</p>

<h1 align="center">Ask the Model (AtM)</h1>

<p align="center">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square">
</p>

Ask the Model (AtM) is a local desktop chat application for working with locally hosted AI models through a simple native interface.

The project is being developed as a natural-language access layer for repositories of scientific dynamical models, including Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD).

AtM is an application interface, not a scientific model. EWD, CBD, RMD and other compatible repositories remain independent and authoritative for their own code, data, assumptions, provenance and validation.

## What AtM does

AtM provides a local conversational interface that can:

- discover locally available AI models through an Ollama-compatible provider;
- present chat-capable models for selection;
- refresh the local model list while the application is running;
- stream assistant responses as they are generated;
- maintain multi-turn conversation context during the current application session;
- follow the desktop light/dark appearance;
- run as a native GTK 4 / Granite application packaged for elementary OS.

The long-term goal is to let users query and compare scientific-model repositories through natural language while preserving a clear distinction between repository evidence and AI-generated interpretation.

Repository-aware retrieval, source provenance and scientific-model execution are separate capabilities and are not implied by ordinary local chat.

## Local-first architecture

AtM is designed around locally hosted inference.

It connects to a local Ollama-compatible service. AtM does not install, start, update or manage the inference provider itself, and it does not download or delete AI models.

The application currently uses loopback-only provider connections, keeping the inference path on the local machine.

## Requirements

For normal use, AtM requires:

- a compatible Linux desktop environment;
- a running local Ollama-compatible service;
- at least one installed AI model with chat/completion capability.

The primary packaged target is elementary OS using GTK 4, Granite and Flatpak.

Detailed provider API requirements, sandbox permissions, display compatibility and build dependencies are documented in [Dependencies and compatibility](docs/DEPENDENCIES_AND_COMPATIBILITY.md).

## Scientific-model boundary

AtM is intended to work with scientific dynamical-model repositories without replacing them.

When repository-aware functionality is available, the source repository remains canonical. AI-generated text must remain distinguishable from repository content, model outputs and validated scientific results.

The initial repository suite is intended to include:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

The architecture is intended to support additional compatible repositories later.

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
- [Release notes](releases/)
- [Citation metadata](CITATION.cff)

## License

MIT License.
