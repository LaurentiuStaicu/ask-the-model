# Dependencies and compatibility

This document defines the compatibility contract for Ask the Model (AtM) v0.2.0.

## Supported application baseline

The validated desktop baseline is elementary OS 8 using the Flatpak package built against:

- `io.elementary.Platform//8`;
- `io.elementary.Sdk//8`.

The native application is written in Vala and uses GTK 4 and Granite 7.

Other Linux environments may work if the required libraries and display stack are available, but they are not part of the validated v0.2.0 baseline.

## Local AI provider requirement

AtM v0.2.0 requires an already-running local Ollama-compatible HTTP provider.

Endpoint order is fixed in this release:

1. `http://127.0.0.1:11434` — preferred standalone Ollama endpoint;
2. `http://127.0.0.1:11435` — compatibility fallback for managed local-provider setups.

AtM does not install, launch, stop, update or supervise the provider.

Alpaca is optional and is not a dependency.

### Validated provider

- Ollama 0.34.2.

This is a validated reference version, not an intentional exact-version pin. A different provider/version is compatible only if it implements the API behavior required below.

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

Providers that do not accept these fields or response semantics are not compatible with v0.2.0.

## Model requirements

A chat model must advertise the `completion` capability.

Capabilities such as `vision`, `tools`, `thinking` or `embedding` may be present, but v0.2.0 does not expose those capabilities as separate application features.

The default chat path requests `think: false` to prioritize interactive latency. This does not imply that a selected model lacks reasoning capability.

## GPU and compute compatibility

AtM contains no inference engine and no vendor-specific GPU compute code. GPU/CPU selection belongs entirely to the external provider.

No particular GPU model, vendor or acceleration backend is required by AtM. Hardware acceleration may be used when supported and configured by the local provider. CPU inference may also work, although performance depends on the selected model and provider configuration.

## Model storage

AtM does not own or manage the provider's model store.

The provider may use its default model directory or an externally configured `OLLAMA_MODELS` location. Moving, downloading, deleting and deduplicating model files are outside the v0.2.0 application boundary.

## Flatpak sandbox requirements

The v0.2.0 Flatpak requests:

- `--share=ipc`;
- `--share=network`;
- `--socket=wayland`;
- `--socket=fallback-x11`;
- `--device=dri`.

Network sharing is required so the sandbox can reach the host-local HTTP provider. This permission is broader than loopback at the Flatpak sandbox layer; the current AtM provider implementation itself only addresses `127.0.0.1`.

## Display compatibility

### X11

AtM v0.2.0 selects `GSK_RENDERER=cairo` automatically only when:

- `XDG_SESSION_TYPE=x11`;
- no Wayland display is present;
- the user has not explicitly set `GSK_RENDERER`.

This is an application compatibility fallback for the current GTK/GSK path, not a hardware-specific requirement.

### Wayland

AtM does not force the Cairo renderer on Wayland. The normal GTK renderer selection remains active.

Wayland behavior is supported by the Flatpak manifest. The release does not claim exhaustive validation across all Linux graphics-driver combinations.

## Native build dependencies

Required build/runtime libraries:

- GTK 4;
- Granite 7;
- libsoup 3.0;
- json-glib 1.0;
- Vala;
- Meson >= 1.0.0.

The project is licensed under MIT.

## Privacy and network boundary

v0.2.0 has no cloud-provider integration.

The implemented provider addresses loopback only. Prompt text is sent when the user activates **Send**.

Conversation history is stored only in memory for the current application process and is not persisted by AtM across restarts.

## Known compatibility limitations

v0.2.0 does not provide:

- configurable provider host/port;
- provider authentication;
- TLS configuration;
- non-loopback or remote providers;
- cloud AI providers;
- model download/deletion;
- automatic Ollama installation or service management;
- repository ingestion or retrieval;
- persistent conversations.

Any of these additions changes the compatibility/security boundary and must be documented in the release that introduces it.
