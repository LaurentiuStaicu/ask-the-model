# Interface design requirements

## Purpose

This document defines the functional requirements that the Ask the Model (AtM) interface must make understandable before any visual layout is approved.

AtM is a local AI chat interface for querying, exploring and discussing repositories of scientific dynamical models. The current suite includes Empirical World3 Dynamics (EWD), Cognitive Belief Dynamics (CBD) and Romanian Monetary Dynamics (RMD).

The interface must preserve a strict distinction between:

1. the **scientific-model repository** that supplies project context and remains canonical for its own model;
2. the **local AI model/provider** that generates conversational responses;
3. the **conversation** through which the user asks questions.

The application must not visually imply that the local language model is EWD, CBD or RMD, or that a generated answer is itself a canonical model result.

## MVP interaction model

The first functional application should support the following user flow:

1. Start AtM.
2. Confirm whether the local AI provider is available.
3. Select a scientific-model repository context.
4. Select an available local AI model.
5. Start or continue a conversation.
6. Ask a natural-language question.
7. Read the streamed answer.
8. Inspect which repository sources/context were used when repository-aware retrieval is enabled.
9. Start a new conversation or switch to another existing conversation.
10. Change application/provider settings when needed.

Execution or simulation of EWD, CBD or RMD is outside the current application boundary unless it is added later through an explicitly designed and documented feature.

## Information hierarchy

The interface must make these four levels visually distinguishable:

- **Application:** Ask the Model.
- **Repository context:** EWD, CBD, RMD or another compatible repository.
- **AI model:** the locally hosted LLM selected from the configured provider.
- **Conversation:** the current question/answer history.

Repository context and AI model must never be represented as the same selector.

## Required interface surfaces

The visual design process must explicitly consider and approve each of these surfaces:

1. Main window structure.
2. Header bar and global actions.
3. Repository-context selector/navigation.
4. Local AI-model selector and provider status.
5. Conversation history/navigation.
6. Conversation transcript.
7. User/assistant message presentation.
8. Prompt composer and send/stop controls.
9. Source/provenance presentation.
10. Connection, loading and generation status.
11. Empty/welcome state.
12. Error and recovery states.
13. Preferences/settings.
14. Narrow-window/adaptive behavior.
15. Keyboard-access and tooltip behavior.
16. Light/dark appearance and use of the AtM identity accent.

## Visual-design constraints

- Follow elementary OS conventions unless a documented AtM-specific need justifies a deviation.
- Prefer native GTK 4 / Granite widgets over custom-drawn controls.
- Keep global actions in the header area and settings toward the end of the header/menu.
- Use a sidebar only for high-level navigation, not for unrelated controls.
- Avoid permanently visible secondary panes unless their information is useful often enough to justify the space.
- Keep the main reading/composing area visually dominant.
- Preserve usable spacing and avoid excessive density.
- Do not rely on color alone to communicate repository, provider or connection state.
- Icon-only tool buttons require clear tooltips.
- Interface text should remain brief and unambiguous.
- The initial design must remain usable when the window is tiled or narrowed.

## Approval gate

No substantive visual layout or custom styling should be treated as final until it has been reviewed element by element.

For each proposed element, the design review should state:

- what the element does;
- why it is needed;
- why it is placed in that location;
- whether a standard elementary/GTK pattern exists;
- what alternatives were considered;
- what changes in narrow windows;
- whether the element is MVP-critical or optional.

The first visual decision is the overall window topology. Subsequent elements should be reviewed only after that topology is selected.
