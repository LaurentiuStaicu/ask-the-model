<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="180">
</p>

<h1 align="center">Ask the Model (AtM)</h1>

<p align="center">
  <img alt="Version: 0.1.1" src="https://img.shields.io/badge/version-0.1.1-blue?style=flat-square">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square">
</p>

Ask the Model (AtM) is a local AI chat interface designed to query, explore and discuss repositories of scientific dynamical models through natural language. It provides a conversational access layer over model projects such as Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD), using locally hosted AI while keeping user data on-device.

AtM is an application interface. It does not contain, redefine or replace the scientific models themselves; each model remains canonical in its own repository.

**Current status:** application baseline, current development version 0.1.1. The development branch now includes the native GTK 4 / Granite shell, local message composition, automatic elementary OS color-scheme following, and a minimal local Ollama discovery probe. The probe checks the loopback API for available models but does not yet send user prompts to a model. Repository access, AI response generation, conversation management and settings are not yet implemented. See [STATUS.md](STATUS.md) for the maintained application boundary and [CITATION.cff](CITATION.cff) for citation metadata.

## Build and package

### Native development build

```bash
meson setup build --prefix=/usr
meson compile -C build
./build/io.github.laurentiustaicu.ask_the_model
```

### Flatpak build for elementary OS

AtM includes an elementary OS 8 Flatpak manifest using `io.elementary.Platform//8` and `io.elementary.Sdk//8`.

On elementary OS, install Flatpak Builder if needed and build the application with:

```bash
sudo apt install flatpak-builder
flatpak install -y appcenter io.elementary.Platform//8 io.elementary.Sdk//8
flatpak-builder flatpak-build io.github.laurentiustaicu.ask_the_model.yml --user --install --force-clean
flatpak run io.github.laurentiustaicu.ask_the_model
```

The development Flatpak now shares the network subsystem because local Ollama discovery uses its HTTP API. The current AtM code only probes loopback addresses (`127.0.0.1`) on ports `11434` and `11435`; no prompt content is transmitted during discovery.
