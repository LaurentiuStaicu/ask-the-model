# Ask the Model interface guide

This guide explains the user-facing controls in Ask the Model v0.4.0 and the state transitions that matter when using repository-grounded chat.

The guide is intentionally text-first. A dedicated visual application guide is deferred until a representation can be produced and reviewed against the real GTK interface without introducing invented controls, proportions, decoration, or layout.

## Header controls

### AI model selector

The first selector lists completion-capable models reported by the configured Ollama-compatible provider.

The adjacent **Refresh models** button repeats provider discovery. AtM does not scan arbitrary disks for GGUF files; the provider must know about the model.

### Repository selector

The repository selector controls the scientific source scope for the next conversation. The fixed v0.4.0 catalog is:

- **EWD** — Empirical World3 Dynamics;
- **CBD** — Cognitive Belief Dynamics;
- **RMD** — Romanian Monetary Dynamics.

Before the first Send, any combination may be selected, including no repository at all.

### Repository Refresh

**Refresh repositories** resolves the tracked branch of every selected repository to an exact remote Git SHA and reads the repository-declared version from that same revision.

Refresh does not silently replace local snapshots.

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

For a durable chat, the tab **Close** control closes the current tab and records that it should not reopen automatically at the next startup. It does not archive or delete the conversation.

The per-tab conversation menu keeps lifecycle actions separate:

- **Archive** preserves transcript, repository pins and citation provenance but removes the conversation from normal startup restore;
- **Delete permanently** requires confirmation and removes the durable conversation and its dependent local history.

The **History** control at the opposite end of the tab strip lists durable conversations that are not currently open. A closed conversation can be **Open**ed again; an archived conversation can be **Unarchive & Open**ed. History-opened conversations first return as restored read-only tabs and become continuable only after AtM requalifies the exact saved AI-model identity and repository generation/version/SHA context.

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
