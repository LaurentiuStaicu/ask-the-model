# Ask the Model interface guide

This guide explains the user-facing controls in Ask the Model v0.5.0 and the state transitions that matter when using repository-grounded chat.

The guide is intentionally text-first. A dedicated visual application guide is deferred until a representation can be produced and reviewed against the real GTK interface without introducing invented controls, proportions, decoration, or layout.

## Header controls

### AI model selector

The first selector lists completion-capable models reported by the configured Ollama-compatible provider.

The adjacent **Refresh models** button repeats provider discovery. AtM does not scan arbitrary disks for GGUF files; the provider must know about the model.

### Repository selector

The repository selector controls the scientific source scope for the next conversation. The fixed v0.5.0 catalog is:

- **EWD** — Empirical World3 Dynamics;
- **CBD** — Cognitive Belief Dynamics;
- **RMD** — Romanian Monetary Dynamics.

Before the first Send, any combination may be selected, including no repository at all.

### Repository Refresh

**Refresh repositories** resolves the tracked branch of every selected repository to an exact remote Git SHA and reads the repository-declared version from that same revision.

Refresh does not silently replace local snapshots.

### Optimization mode

A compact global instrument-style control sits at the end of the header:

`Optimizations  OFF  [switch]  ON`

It controls the post-v0.5.0 runtime optimization program as one application-wide mode:

- every AtM process starts with **OFF** selected;
- the state is session-only and is not remembered after restart;
- **OFF** preserves the established baseline runtime path;
- **ON** allows runtime optimizations that have passed their individual qualification gates;
- measurement and qualification tools do not depend on this UI switch.

The control deliberately resembles a small panel switch rather than a flat preference toggle. Its rounded case uses the same neutral ridge-border language as the AtM application frame. The case interior is warm muted red when OFF and warm muted green when ON, while the moving slider remains neutral metallic gray. Small fixed OFF and ON labels sit on the left and right, so color is never the only state cue.

The status LCD mirrors the same state with a compact `OPT OFF` / `OPT ON` annunciator in the NASA-style status strip.

Changing the switch affects subsequent operations. An operation already in progress keeps the optimization mode captured when it started rather than changing behavior halfway through.

### Download / Update

The repository action button downloads a repository that is not installed, updates one whose exact remote SHA differs from the current local snapshot, or repairs a locally integrity-invalid snapshot. A same-SHA integrity repair still requires a fresh exact-SHA download before the invalid local directory is quarantined and replaced.

The update path is fail-closed:

1. resolve exact SHA;
2. download into staging;
3. safely extract;
4. validate manifest, paths and metadata;
5. build and validate the retrieval index;
6. atomically promote the new immutable snapshot.

A failed update leaves the last valid snapshot available. When repairing a locally invalid same-SHA snapshot, AtM preserves the invalid directory under a diagnostic quarantine name rather than silently rewriting it in place.

When **Optimizations ON** is active, Download/Update also uses a nonblocking cross-process repository mutation lease. If another AtM process is already changing repository authority, the operation stops before repository staging is touched and reports that another Ask the Model instance is currently updating repository state. Ordinary repository reads and grounding do not wait for this global mutation lease.

With **Optimizations OFF**, this additional coordination layer is not used and the established v0.5.0 runtime path remains in effect.

## Status LCD

The narrow status strip reports compact model/repository lifecycle states such as repository identity, refresh/update activity, ready, offline and error conditions.

It is intentionally subdued rather than a bright LED-style display.

## Chat tabs and conversation history

Each tab owns its own:

- provider conversation history;
- repository/model identity;
- retrieval state;
- grounded citation objects.

The **+** control creates a new chat.

The tab controls deliberately separate **discard** from **save**:

- **Close (X)** closes the chat without saving it. If the chat already has temporary durable turn records, they are removed rather than added to History.
- the adjacent **Save to History** control uses the standard symbolic save icon. It is the explicit archive/save action: it preserves the transcript, repository pins and citation provenance and places the conversation in History.

Only explicitly archived conversations are saved across normal application exit. If the process was interrupted, the next startup removes any leftover unarchived working records.

The **History** control at the opposite end of the tab strip lists archived conversations only. Each row provides **Open** and **Delete permanently**. Open reconstructs the archived transcript and then rechecks the exact saved AI-model identity and repository generation/version/SHA context before continuation; opening does not unarchive the saved conversation. Delete permanently requires confirmation and removes both the archived local conversation and its managed automatic JSON export.

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

Durable conversations are mirrored automatically as JSON files under:

```text
~/Ask the Model/Conversation Exports/
```

The export folder is created automatically and does not require a location chooser or export button. Only explicitly archived conversations receive managed JSON files; unarchived working chats are not exported. Archived files are refreshed automatically when the saved conversation changes.

Per-snapshot retrieval indexes are regenerable cache data inside the Flatpak private cache.

The application receives only the dedicated `~/Ask the Model:create` filesystem permission; it does not request broad Home or host filesystem access.

## What the interface does not imply

A READY repository means the local snapshot/index passed AtM compatibility and integrity checks. It does **not** mean AtM has certified the scientific validity of the underlying model.

AtM is an interface and retrieval layer. EWD, CBD and RMD remain authoritative for their own model definitions, data, assumptions, provenance and validation.
