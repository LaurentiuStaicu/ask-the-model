# Ask the Model interface guide

This guide explains the user-facing controls in Ask the Model v0.3.0 and the state transitions that matter when using repository-grounded chat.

The interface is documented as a technical wireframe rather than a screenshot or generated illustration. That keeps the documentation editable, reviewable in Git diffs, and independent of a particular desktop background or window size.

## Technical wireframe

```text
 MAIN WINDOW

+--------------------------------------------------------------------------------------+
| [1] AI model selector  [2] model refresh   [3] repository selector  [4] repo refresh |
|                                                        [5] contextual Download/Update |
|                                   Ask the Model                                      |
+--------------------------------------------------------------------------------------+
| [6] repository/status LCD:     EWD   CBD   RMD      ...      READY                   |
+--------------------------------------------------------------------------------------+
| [7] +   [ conversation tab ..................................................... x ] |
+--------------------------------------------------------------------------------------+
|                                                                                      |
| [8] You: <question>                                                                  |
|                                                                                      |
|     Assistant: <ordinary or repository-grounded answer>                              |
|                                                                                      |
|     Sources: [9][1] [9][2] [9][3] ...                                                |
|                                                                                      |
|                                                                                      |
|                                                                                      |
+--------------------------------------------------------------------------------------+
| [10] Ask something.....................................................  [11] Send    |
+--------------------------------------------------------------------------------------+


 SOURCE DETAIL WINDOW

                         +------------------------------------------------------------+
                         | [12] Source [1]                                             |
                         +------------------------------------------------------------+
                         | Repository + repository-declared version                   |
                         | Source file + physical locator                             |
                         | Exact immutable snapshot SHA                               |
                         | Logical Source ID                                          |
                         |                                                            |
                         | Readable evidence excerpt                                  |
                         |                                                            |
                         | Open immutable source                                      |
                         +------------------------------------------------------------+
```

The wireframe describes topology and responsibilities, not pixel-perfect widget dimensions. The actual GTK theme, font metrics and window decoration are supplied by the desktop environment.

## Numbered control legend

| # | Surface | Purpose |
| ---: | --- | --- |
| 1 | AI model selector | Chooses a completion-capable model reported by the local provider before the first Send. |
| 2 | Model refresh | Repeats provider/model discovery without restarting AtM. |
| 3 | Repository selector | Chooses zero or more repositories from the fixed EWD/CBD/RMD catalog. |
| 4 | Repository refresh | Resolves the selected repositories to their current exact remote Git SHA/version without modifying local snapshots. |
| 5 | Download / Update | Appears contextually when a selected repository is missing or a newer exact SHA is available. |
| 6 | Status LCD | Shows compact repository identity/lifecycle state such as READY, refresh/update activity, offline or error. |
| 7 | Chat tabs | Creates, switches and closes independent in-memory conversations. |
| 8 | Transcript | Shows user/assistant turns and grounded source references. |
| 9 | Numbered source reference | Opens exact provenance for evidence used in the grounded answer. |
| 10 | Prompt composer | Accepts the next user message. |
| 11 | Send | Starts the turn; on the first Send it also freezes AI-model and repository identity for that chat. |
| 12 | Source detail window | Shows repository/version, source locator, exact snapshot SHA, Source ID, readable excerpt and immutable source link. |

## Header controls

### AI model selector

The first selector lists completion-capable models reported by the configured Ollama-compatible provider.

The adjacent **Refresh models** button repeats provider discovery. AtM does not scan arbitrary disks for GGUF files; the provider must know about the model.

### Repository selector

The repository selector controls the scientific source scope for the next conversation. The fixed v0.3.0 catalog is:

- **EWD** — Empirical World3 Dynamics;
- **CBD** — Cognitive Belief Dynamics;
- **RMD** — Romanian Monetary Dynamics.

Before the first Send, any combination may be selected, including no repository at all.

### Repository Refresh

**Refresh repositories** resolves the tracked branch of every selected repository to an exact remote Git SHA and reads the repository-declared version from that same revision.

Refresh does not silently replace local snapshots.

### Download / Update

The repository action button downloads a repository that is not installed or updates one whose exact remote SHA differs from the current local snapshot.

The update path is fail-closed:

1. resolve exact SHA;
2. download into staging;
3. safely extract;
4. validate manifest, paths and metadata;
5. build and validate the retrieval index;
6. atomically promote the new immutable snapshot.

A failed update leaves the last valid snapshot available.

## Status LCD

The narrow status strip reports compact model/repository lifecycle states such as repository identity, refresh/update activity, ready, offline and error conditions.

It is intentionally subdued rather than a bright LED-style display.

## Chat tabs

Each tab owns its own:

- provider conversation history;
- repository/model identity;
- retrieval state;
- grounded citation objects.

The **+** control creates a new chat. The tab close control removes only that in-memory conversation; it does not delete downloaded repositories or indexes.

## First-Send freeze

The first Send freezes the selected AI model and repository snapshot set for that chat.

The selectors remain visible but become insensitive. Updates discovered later affect future chats, not the active conversation.

## Transcript and grounded sources

Repository-grounded answers show compact numbered source references such as `[1]`, `[2]`, and `[3]`.

Clicking a source reference opens a transient detail window containing:

- repository and repository-declared version;
- exact immutable snapshot SHA;
- source file and locator;
- logical source ID;
- a readable evidence excerpt;
- an immutable GitHub permalink when one can be built safely.

Markdown excerpts are converted to readable plain text for display. AtM does not execute repository Markdown/HTML as GTK markup.

## Prompt composer

The lower composer accepts the next user message. The **Send** button starts the turn.

For grounded turns, repository evidence is retrieved only for the current turn and is not accumulated into ordinary provider conversation history.

## Local storage

Validated snapshots are visible under:

```text
~/Ask the Model/Repositories/
```

Per-snapshot retrieval indexes are regenerable cache data inside the Flatpak private cache.

The application receives only the dedicated `~/Ask the Model:create` filesystem permission; it does not request broad Home or host filesystem access.

## What the interface does not imply

A READY repository means the local snapshot/index passed AtM compatibility and integrity checks. It does **not** mean AtM has certified the scientific validity of the underlying model.

AtM is an interface and retrieval layer. EWD, CBD and RMD remain authoritative for their own model definitions, data, assumptions, provenance and validation.
