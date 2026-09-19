# Application architecture boundary

## Current state

AtM currently provides a GTK 4 / Granite application shell, desktop integration, AppStream metadata and elementary OS 8 Flatpak packaging.

The conversational and repository-aware layers are not yet implemented.

## Planned logical components

### Repository context service

Responsible for representing a configured scientific-model repository and exposing repository documents to the future retrieval/context layer.

The repository itself remains canonical. AtM must not silently rewrite repository state or convert generated text into canonical project data.

### Local AI provider service

Responsible for connecting to a locally hosted AI provider, discovering available models and sending chat requests.

The first provider target is an Ollama-compatible local API. Provider implementation must remain separated from the user-interface layer so that the endpoint and provider can evolve without redesigning the entire application.

### Conversation service

Responsible for conversation state, message ordering and future local persistence.

A conversation records which repository context and AI model were active, so answers remain interpretable later.

### Context/provenance service

Responsible for recording which repository sources were supplied to the AI for a response.

This layer must distinguish retrieved source material from AI-generated interpretation.

### Application settings

Responsible for local provider endpoint, preferred AI model, repository locations and application-level preferences.

## Explicitly outside the current boundary

The following are not implied by this architecture:

- executing EWD, CBD or RMD simulations;
- modifying scientific-model repository files;
- treating AI responses as canonical scientific results;
- cloud AI services by default;
- autonomous changes to scientific models.

Any future model-execution or write-back capability requires a separate architecture and safety review before implementation.

### Multi-model context selection

A conversation may be grounded in one or more scientific dynamical-model repositories at the same time.

The selected repository set is conversation context, not AI-provider configuration. A conversation should record the selected repository identifiers so that later answers remain interpretable.

For multi-model discussions, the future context/provenance layer must preserve source attribution per repository rather than flattening all retrieved material into an unidentified combined context.

The local AI model remains a separate single active inference model for a request unless a future provider architecture explicitly supports another mode.

### Localization service

All user-visible interface strings are localizable. English is the default application language and Romanian is the second supported language.

Implementation should use the standard gettext/PO workflow supported by Vala/Meson so UI copy is not duplicated in application logic.

Canonical terminology is defined in `docs/TERMINOLOGY.md`.

### Typography integration

For the conversation transcript and composer, AtM should read the desktop's `org.gnome.desktop.interface` `monospace-font-name` setting, matching the system monospace behavior used by elementary Code.

UI chrome continues to inherit the elementary GTK theme font.

### Header identity overlay

The normal header geometry should remain theme-driven. If the approved visual design uses the larger owl identity disc, implement it as an overlay/custom titlebar composition whose overlay child does not determine the header's requested height.

The overlay must not obscure native window controls or interfere with titlebar dragging.
