<p align="center">
  <img src="assets/icon.png" alt="Ask the Model icon" width="180">
</p>

<h1 align="center">Ask the Model (AtM)</h1>

<p align="center">
  <img alt="Version: 0.2.1" src="https://img.shields.io/badge/version-0.2.1-333333?style=flat-square">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-b0b0b0?style=flat-square">
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/actions/workflows/flatpak.yml"><img alt="Flatpak build" src="https://github.com/LaurentiuStaicu/ask-the-model/actions/workflows/flatpak.yml/badge.svg"></a>
</p>

<p align="center">
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak"><img alt="Download Flatpak" src="https://img.shields.io/badge/Download-Flatpak-222222?style=for-the-badge&logo=flatpak&logoColor=white"></a>
  <a href="#install"><img alt="Installation guide" src="https://img.shields.io/badge/Installation-Guide-666666?style=for-the-badge"></a>
  <a href="https://github.com/LaurentiuStaicu/ask-the-model/releases/latest"><img alt="Latest release" src="https://img.shields.io/badge/Latest-Release-b0b0b0?style=for-the-badge"></a>
</p>

Ask the Model (AtM) is a local AI chat application for working with locally hosted language models and, progressively, for querying, exploring and comparing repositories of scientific dynamical models through natural language. It is maintained as an application interface rather than a scientific model core; repositories such as Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD) remain independent and authoritative for their own code, data, assumptions, provenance and validation.

**Current status:** local-chat baseline with local provider discovery, chat-capable AI-model selection, refreshable model detection, streamed responses and in-session conversation context. Repository-aware retrieval, source provenance and scientific-model execution are not yet implemented. See [STATUS.md](STATUS.md) for the current application boundary and [CITATION.cff](CITATION.cff) for citation metadata.

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

## Install

The recommended order below is for people who want to **use** AtM. You do not need Meson, Vala, GTK development packages or the elementary SDK for a normal Flatpak installation.

### Installation order

| Step | Component | Required for normal use? | Notes |
| --- | --- | --- | --- |
| 1 | Flatpak | Yes | Must already be available on the Linux host. |
| 2 | Local Ollama-compatible provider | Yes | Must be installed and running outside AtM. |
| 3 | At least one completion-capable AI model | Yes | Installed through the provider, not through AtM. |
| 4 | Ask the Model Flatpak | Yes | Download the release bundle and install it with Flatpak. |
| 5 | Meson, Vala, GTK/Granite development packages | No | Developer-only dependencies for native source builds. |

### 1. Check Flatpak

```bash
flatpak --version
```

If this command is unavailable, install Flatpak using the instructions for your Linux distribution before continuing.

### 2. Prepare the local AI provider

AtM v0.2.1 expects an already-running Ollama-compatible HTTP provider and probes these loopback endpoints in order:

1. `http://127.0.0.1:11434` — preferred endpoint;
2. `http://127.0.0.1:11435` — compatibility fallback.

AtM does **not** install, start, stop or update the provider.

If both endpoints are active, AtM uses `11434` first. Do not install a second provider only for AtM unless you intentionally want to maintain two local-provider setups.

### 3. Install a compatible chat model

Install at least one model through your chosen provider. The model must advertise the `completion` capability through the provider API.

Embedding-only models are intentionally excluded from the AtM model selector.

### 4. Download and install AtM

Download the latest release bundle:

**[AskTheModel.flatpak](https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak)**

Then install it:

```bash
flatpak install --user ./AskTheModel.flatpak
```

The release bundle is built against `io.elementary.Platform//8`. Flatpak handles the application runtime separately; normal users do not need to install GTK, Granite, libsoup or json-glib development packages manually.

### 5. Run AtM

```bash
flatpak run io.github.laurentiustaicu.ask_the_model
```

AtM should discover the running provider and list compatible locally installed chat models.

### Important compatibility conditions

- The current release supports local loopback providers only; remote and cloud-provider endpoints are not implemented.
- Provider authentication and TLS configuration are not implemented.
- A provider that does not implement the required `/api/tags`, `/api/show` and `/api/chat` behavior is not compatible.
- AtM does not download or delete AI models.
- GPU/CPU selection and hardware acceleration belong to the external provider, not to AtM.
- The GitHub release bundle is a convenient direct-download format. The automatically published `AtM Development` repository is a development update source and is not presented as the primary end-user installation channel.

See [Dependencies and compatibility](docs/DEPENDENCIES_AND_COMPATIBILITY.md) for the full compatibility contract.

## Scientific-model boundary

AtM is intended to work with scientific dynamical-model repositories without replacing them.

When repository-aware functionality is available, the source repository remains canonical. AI-generated text must remain distinguishable from repository content, model outputs and validated scientific results.

The initial repository suite is intended to include:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

The architecture is intended to support additional compatible repositories later.

## Development build

The following dependencies are for contributors or developers building AtM from source. They are **not** prerequisites for installing the release Flatpak.

### Native build dependencies

AtM requires:

- Meson >= 1.0.0;
- Vala;
- GTK 4 development files;
- Granite 7 development files;
- libsoup 3.0 development files;
- json-glib development files.

On elementary OS 8 / Ubuntu-compatible systems, the corresponding packages are:

```bash
sudo apt install meson valac libgtk-4-dev libgranite-7-dev libsoup-3.0-dev libjson-glib-dev
```

Then build:

```bash
meson setup build --prefix=/usr
meson compile -C build
./build/io.github.laurentiustaicu.ask_the_model
```

### Flatpak development build

For a local Flatpak build from source:

```bash
sudo apt install flatpak-builder
flatpak install -y appcenter io.elementary.Platform//8 io.elementary.Sdk//8
flatpak-builder flatpak-build io.github.laurentiustaicu.ask_the_model.yml --user --install --force-clean
flatpak run io.github.laurentiustaicu.ask_the_model
```

The elementary SDK is needed to **build** the Flatpak; it is not required as a manually installed end-user dependency when installing the release bundle.

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
