# Dependencies and compatibility

This document defines the compatibility contract for Ask the Model (AtM) v0.4.0.

## Supported application baseline

The packaged desktop baseline is elementary OS 8 using the Flatpak package built against:

- `io.elementary.Platform//8`;
- `io.elementary.Sdk//8` for development builds.

The native application is written in Vala and uses GTK 4 and Granite 7.

Other Linux environments may work if Flatpak and the required display stack are available, but they are not part of the documented packaged baseline.

## Normal-user dependency order

For a normal release installation, the required pieces are deliberately separated:

1. **Flatpak on the host** — required to install and run the release bundle.
2. **A local Ollama-compatible provider** — external to AtM and already running.
3. **At least one completion-capable AI model** — installed through that provider.
4. **The AtM Flatpak bundle** — installed from the GitHub release.

The elementary runtime is resolved by Flatpak. Meson, Vala, GTK/Granite development headers, libsoup development headers, json-glib development headers, SQLite/libarchive development headers and the elementary SDK are not normal-user prerequisites.

## Local AI provider requirement

AtM v0.4.0 requires an already-running local Ollama-compatible HTTP provider.

Endpoint order is fixed in this release:

1. `http://127.0.0.1:11434` — preferred standalone local-provider endpoint;
2. `http://127.0.0.1:11435` — compatibility fallback for managed local-provider setups.

AtM does not install, launch, stop, update or supervise the provider.

If both endpoints are active, `11434` is selected first. Running two providers is not prohibited, but users should do so intentionally because the fixed probe order determines which provider AtM uses.

Compatibility is API-based. A provider is compatible only if it implements the behavior required below.

## Required provider API behavior

### Model enumeration

`GET /api/tags`

AtM expects a JSON object containing a `models` array and model names.

### Capability inspection

`POST /api/show`

AtM submits a model name and expects a `capabilities` array.

Only models advertising `completion` are exposed in the AI-model selector. Embedding-only models are intentionally excluded.

### Chat

`POST /api/chat`

AtM submits:

- `model`;
- ordered `messages`;
- `think: false`;
- `stream: true`.

AtM expects newline-delimited JSON response objects containing assistant `message.content` chunks and a final `done` state.

Providers that do not accept these fields or response semantics are not compatible with v0.4.0.

## Model requirements

A chat model must advertise the `completion` capability.

Capabilities such as `vision`, `tools`, `thinking` or `embedding` may be present, but v0.4.0 does not expose those capabilities as separate application features.

The default chat path requests `think: false` to prioritize interactive latency. This does not imply that a selected model lacks reasoning capability.

## Release Flatpak installation

The supported direct-download bundle is published as:

`AskTheModel.flatpak`

The stable latest-release download path is:

`https://github.com/LaurentiuStaicu/ask-the-model/releases/latest/download/AskTheModel.flatpak`

Install a downloaded bundle with:

```bash
flatpak install --user ./AskTheModel.flatpak
```

A single-file bundle is provided for convenient direct installation. The separately generated `AtM Development` Flatpak repository remains a development update source rather than the primary public installation channel.

## GPU and compute compatibility

AtM contains no inference engine and no vendor-specific GPU compute code. GPU/CPU selection belongs entirely to the external provider.

No particular GPU model, vendor or acceleration backend is required by AtM. Hardware acceleration may be used when supported and configured by the provider. CPU inference may also work, although performance depends on the selected model and provider configuration.

## Model storage

AtM does not own or manage the provider's model store.

The provider may use its default model directory or another provider-configured model location. Moving, downloading, importing, deleting and deduplicating AI model files remain outside the v0.4.0 application boundary; repository snapshots are managed separately by AtM under its dedicated application directory.

For practical instructions covering model sources, Ollama storage paths, `OLLAMA_MODELS`, GGUF import, model removal and disk/memory management, see [`MODEL_GUIDE.md`](MODEL_GUIDE.md).

## Flatpak sandbox requirements

The v0.4.0 Flatpak requests:

- `--share=ipc`;
- `--share=network`;
- `--socket=wayland`;
- `--socket=fallback-x11`;
- `--device=dri`.

Network sharing is required so the sandbox can reach the host-local HTTP provider. This permission is broader than loopback at the Flatpak sandbox layer; the current AtM provider implementation itself only addresses `127.0.0.1`.

The v0.4.0 repository-aware release additionally requests only:

- `--filesystem=~/Ask the Model:create`.

This narrowly exposes the dedicated user-visible repository directory so AtM can create and maintain `~/Ask the Model/Repositories`. It does not grant access to the rest of Home. Repository retrieval indexes and application state remain in the Flatpak-private XDG cache/state directories.

## Display compatibility

### X11

AtM v0.4.0 selects `GSK_RENDERER=cairo` automatically only when:

- `XDG_SESSION_TYPE=x11`;
- no Wayland display is present;
- the user has not explicitly set `GSK_RENDERER`.

This is an application compatibility fallback, not a hardware-specific requirement.

### Wayland

AtM does not force the Cairo renderer on Wayland. The normal GTK renderer selection remains active.

Wayland behavior is supported by the Flatpak manifest. The release does not claim exhaustive validation across all Linux graphics-driver combinations.

## Native development build dependencies

These packages are required only when building the native application and its repository-platform capability tests from source:

- GTK 4 development files;
- Granite 7 development files;
- libsoup 3.0 development files;
- json-glib development files;
- GLib development files;
- SQLite >= 3.37.0 development files;
- libarchive development files;
- libyaml development files for `CITATION.cff` parsing;
- a C compiler for the non-installed platform probe and repository-validation helpers;
- Vala;
- Meson >= 1.0.0.

On elementary OS 8 / Ubuntu-compatible systems:

```bash
sudo apt install meson valac build-essential libgtk-4-dev libgranite-7-dev libsoup-3.0-dev libjson-glib-dev libglib2.0-dev libsqlite3-dev libarchive-dev libyaml-dev
```

For a Flatpak **development build**, `flatpak-builder` and `io.elementary.Sdk//8` are also required. CI builds against that same elementary SDK baseline and runs the Meson test suite inside the Flatpak build environment.

The SQLite/libarchive/libyaml platform probe verifies implementation prerequisites for the repository-aware layer and enforces that the only development filesystem grant is the dedicated `~/Ask the Model:create` path rather than Home/host access. The v0.4.0 release exposes the fixed EWD/CBD/RMD repository selector, lifecycle controls, deterministic retrieval and provenance UI. Test executables are not installed in the application bundle.

## Privacy and network boundary

v0.4.0 contains no direct cloud-provider integration.

The implemented AtM provider addresses loopback only (`127.0.0.1`). Prompt text is sent to that provider when the user activates **Send**.

A loopback AtM connection does not guarantee that the external provider will execute every selected model locally. Provider software can have capabilities outside AtM's control, including cloud-backed models or services. Users who require strict local-only inference must choose a locally installed model and configure the provider accordingly.

For example, current Ollama documentation describes a local-only mode using `OLLAMA_NO_CLOUD=1` or `disable_ollama_cloud` in its server configuration. See the [Ollama FAQ](https://docs.ollama.com/faq) and [`MODEL_GUIDE.md`](MODEL_GUIDE.md).

Conversation history is stored only in memory for the current application process and is not persisted by AtM across restarts.

## Known compatibility limitations

v0.4.0 does not provide:

- configurable provider host/port;
- provider authentication;
- TLS configuration;
- non-loopback or remote providers;
- cloud AI providers;
- model download/deletion;
- automatic provider installation or service management;
- arbitrary unreviewed repository origins beyond the fixed EWD/CBD/RMD catalog;
- full repository snapshot-history/removal management;
- persistent conversations.

Any of these additions changes the compatibility/security boundary and must be documented in the release that introduces it.

## User-facing setup guides

- [Choosing, installing and managing AI models](MODEL_GUIDE.md)
- [Troubleshooting](TROUBLESHOOTING.md)
