# Application status

## Release status

Ask the Model (AtM) v0.1.0 established the initial public application baseline. The current 0.1.1 development state extends that baseline with local message composition, theme following and a minimal local Ollama discovery probe.

Version numbers identify frozen software snapshots. They do not imply that planned AI chat, repository-access or full model-integration functionality is already implemented.

## Canonical application role

**Ask the Model (AtM) is a local conversational interface designed for querying, exploring and discussing repositories of scientific dynamical models through natural language. It is not itself a scientific model and does not embed, redefine or replace the canonical models maintained in those repositories.**

The intended dynamical-model repository suite currently includes Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD), with scope for future compatible repositories.

Each scientific model remains authoritative in its own repository. AtM is an access and interaction layer over those external project sources.

This role is canonical for the project. Future development must not silently turn AtM into a replacement model implementation or make application-level AI output authoritative over the scientific status, structure, provenance, assumptions or validation boundaries maintained by the source repositories.

## Current functional boundary

The current development state establishes:

- the GTK 4 and Granite application shell;
- the stable application ID;
- the Meson build and installation configuration;
- desktop launcher and AppStream metadata;
- elementary-compatible application icons;
- native desktop integration and automatic system color-scheme following;
- local prompt composition with a functional Send action;
- minimal discovery of a local Ollama-compatible API through `GET /api/tags`;
- Flatpak packaging for the elementary OS 8 runtime;
- continuous Flatpak build verification through GitHub Actions;
- standardized project identity, application-boundary and citation metadata.

The Flatpak now requests network sharing because a sandboxed application must use the network subsystem to communicate with a local HTTP provider. This permission is broader than loopback at the sandbox layer. The current AtM implementation itself only probes `127.0.0.1:11434` and `127.0.0.1:11435`, and the discovery request does not include user prompt content.

The current development state does not yet implement:

- AI response generation;
- sending user prompts to a local AI provider;
- repository ingestion or retrieval;
- model-aware context selection;
- conversation persistence;
- application settings;
- execution or simulation of scientific models.

## Data and model boundary

The application is intended to use locally hosted AI and to keep user interaction data on-device wherever the selected local workflow permits.

Scientific model repositories remain external sources. Their own documentation, code, data, provenance, validation status and release boundaries remain authoritative.

AtM must not present an AI-generated explanation as if it were a canonical model result unless that result is explicitly supported by the corresponding source repository or by an actual model execution whose provenance is identified.

## What the current baseline does not claim

- a completed AI chat product;
- embedded scientific models;
- validated scientific inference;
- model execution or simulation;
- authoritative replacement of repository documentation;
- a production-ready scientific decision system.

Future releases should update this file whenever the application's functional boundary, repository integration or model-access architecture changes.
