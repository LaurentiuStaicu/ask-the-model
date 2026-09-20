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
3. Select one or more scientific dynamical-model repository contexts.
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
- **Repository context:** zero to three selected repository contexts from the v1 catalog: EWD, CBD and RMD.
- **AI model:** the locally hosted LLM selected from the configured provider.
- **Conversation:** the current question/answer history.

Repository context and AI model must never be represented as the same selector.

## Required interface surfaces

The visual design process must explicitly consider and approve each of these surfaces:

1. Main window structure.
2. Header bar and global actions.
3. Multi-select dynamical-model context control (one or more repositories).
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

## Multi-repository discussion requirement

AtM must allow the user to select **zero, one or more** scientific dynamical-model repositories for a conversation. Zero repositories preserves ordinary local AI chat; one or more repositories enables repository-grounded chat.

The initial selectable set is:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

The selector must support both single-model discussion and cross-model discussion without introducing a permanent navigation pane solely for this purpose.

The preferred GTK interaction pattern for the first visual prototype is a compact `Gtk.MenuButton` in the upper application area that opens a `Gtk.Popover` containing independent check controls for each available repository. This supports genuine multi-selection and keeps the main conversation area uncluttered.

The closed control must summarize the active scope clearly, for example:

- `Repositories` when none is selected
- `EWD`
- `EWD + RMD`
- `EWD + CBD + RMD`

At least one repository must be selected before a repository-grounded question can be sent. Ordinary local AI chat remains valid with zero selected repositories.

The scientific dynamical-model selector and the local AI-model selector are separate concepts and must never be merged into one control.

## Language and terminology

AtM must support English and Romanian, with English as the default interface language.

Terminology is normative and defined in `docs/TERMINOLOGY.md`. In the UI:

- **Repository / Repositories** identifies EWD, CBD and RMD in the v1 repository-aware scope. Arbitrary repositories are future scope.
- **AI model** identifies the local language model that generates the response.
- The bare word **model** must be avoided wherever it could be ambiguous.

The repository selector supports multiple selections; the AI-model selector represents the active local inference model.

## Minimal chat-window rule

The main chat window must remain visually minimal.

The persistent layout should contain only elements needed during ordinary conversation. Secondary functions such as history, provenance detail and preferences should stay out of the primary reading/composing path unless the user opens them.

The first visual prototype should therefore focus on:

1. a standard-height elementary-style header;
2. compact Repository and AI-model controls;
3. the conversation area;
4. the prompt composer.

No permanent sidebar or secondary inspector is part of the first prototype.

## Typography baseline

The conversation transcript and prompt composer should follow the typography used by elementary Code's editor: the system monospace font and its configured size.

On the current elementary OS defaults this is `Roboto Mono 10`, but AtM should read the system monospace setting rather than hard-code a font family or point size. This preserves the Code-like appearance while respecting the user's system configuration.

General interface chrome—header labels, buttons, menus and settings—should continue to use the normal elementary system UI font instead of forcing monospace everywhere.

## Header and identity mark

The header should use the normal theme-driven `Gtk.HeaderBar` height; AtM should not enlarge the real header solely to accommodate branding.

The AtM owl mark may appear inside a circular identity disc near the upper-left area as an overlay. The disc may visually overlap the header/content boundary toward the right and bottom without contributing to the header's measured height.

The prototype must preserve native window controls and draggable titlebar behavior. If the active decoration layout places native controls in the same corner, the disc must be offset inward rather than cover those controls.

A custom oversized disc is an AtM identity feature, not a replacement for standard header behavior.

## Repository selector detail and conversation pinning

When the selector is expanded, each repository is shown in long form with the repository-declared version, for example:

- `EWD (Empirical World3 Dynamics) v0.1.0`;
- `CBD (Cognitive Belief Dynamics) v0.1.0`;
- `RMD (Romanian Monetary Dynamics) v0.1.0`.

After selection, only active acronyms are shown in the compact control. SHA values, branch names and commit counts are not part of the normal selector UI.

Both the local-AI dropdown and repository selector use the same compact clock-style disclosure indicator: a small filled geometric downward triangle. Native theme arrows/chevrons are hidden so the indicator shape remains consistent across icon themes. The triangle is monochrome and follows the light/dark interface palette; gold remains reserved for active-operation and LCD status feedback.

The header presents repository controls as a distinct group immediately after the local AI-model group. The intended left-to-right relationship is:

`[AI model] [Refresh AI]    [Repositories] [Refresh repositories] [Download/Update]`

Transient model and repository status text is not placed in this horizontal control row. A fixed, compact status LCD sits immediately below the header in the upper edge of the conversation area, so status changes never cause header controls to shift.

The repository selector remains a selection control only. Its popover contains the EWD/CBD/RMD multi-selection controls and does not become a repository-management dialog.

The repository Refresh tool button checks the configured GitHub origins for the currently selected repository set without modifying local repository files. The contextual repository action occupies a permanent fixed slot: it shows a Download icon when a selected repository is missing locally, remains fully transparent and non-interactive when there is no available repository action, and shows an Update icon only when a refresh has identified an available update. The widget must remain allocated while transparent so the selector and Refresh control do not shift horizontally as its state changes. Visible action states are icon-only and use descriptive tooltips.

The repository group must distinguish at least: checking, no update available, update available, downloading/updating, ready/offline-local, and failure. An actionable update-available status remains visible until acted on or refreshed; short scan-completion messages may expire.

While a header action is actively working, its circular button uses a thin activity sleeve: a short muted-gold highlight travels around the button perimeter with a soft low-opacity trail. The effect applies consistently to local-model Refresh, repository Refresh and repository Download/Update. It is an overlay only and must not alter button allocation, icon position or neighboring layout. When system animations are disabled, the sleeve remains static rather than rotating.

The fixed status LCD immediately below the header provides the textual counterpart to that activity cue. It is a passive smoky-gold translucent panel with no panel glow. The LCD itself never lights up. Its active normal-status text is intentionally styled as a dim warm incandescent-lamp amber rather than a saturated LED: approximately `#C79A52`, with only a very small soft bloom. The orbiting activity sleeve remains a clearer related gold because it communicates motion rather than steady-state status.

The LCD follows a compact clock/instrument-panel model rather than free-form status sentences. It uses one permanent horizontal row of predefined segments separated by centered dots:

`NO AI · NO REPOS · EWD · CBD · RMD · SCAN · CHECK · DL · UPD · VAL · READY · OFFLINE · ERR`

The active local AI model name and a normal `CONNECTED` state are deliberately omitted. Normal availability should be visually quiet. `NO AI` lights only when no usable local AI is available after scanning. `NO REPOS` lights when no local selected repository snapshot is ready. These two absence warnings, together with `ERR`, use a static warm lamp-red treatment rather than a saturated LED red; they never blink. All normal/action states use the warm lamp-amber treatment. Inactive segments remain only as extremely faint, non-glowing LCD ghosts so state changes alter luminance rather than geometry.

Repository acronyms light when selected. `SCAN` marks local-AI discovery. `CHECK` marks a repository remote check. `DL`, `UPD` and `VAL` are compact forms of Download, Update and Validate; their full meanings remain available through tooltips. `DL` may remain lit when a selected repository still needs a local snapshot, and `UPD` may remain lit when a usable local snapshot has a newer remote version. During active work, the corresponding button's orbiting activity sleeve distinguishes an operation in progress from a merely available action. `READY` means all selected local repository snapshots are usable and may remain lit together with `UPD` or `OFFLINE`. `ERR` marks a failed repository operation.

The LCD is a structural strip of the application shell, not an inset card. It sits flush immediately below the header and spans the full inner width of the window. Its upper boundary is the header bar's existing lower border; its left and right boundaries are the application's outer frame, so the LCD draws no independent top, left or right border and has no rounded corners. Only its lower edge draws a neutral 2 px ridge-style border matching the application's frame language. Color belongs to status text only. The conversation transcript begins below this lower divider with a small fixed separation. The panel remains permanently allocated, so neither header controls nor the transcript origin shift when status changes.

Repository files are stored visibly under `~/Ask the Model/Repositories`, while derived retrieval indexes remain application-private cache data. The UI should make this distinction understandable without exposing SHA values in the normal header.

The repository scope is editable before the first user message. Once the first message is sent, the scope is pinned for that conversation, including the valid zero-repository case. A later repository-scope change must start a new chat rather than silently changing the scientific basis of an existing conversation.

The preferred v1 behavior applies the same new-chat boundary when the active AI model changes after the first user message.
