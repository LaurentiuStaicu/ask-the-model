# Conversation Persistence Architecture

## Status and scope

This document defines the planned durable local conversation-storage boundary for Ask the Model after the completed Control State hardening work.

It is an architecture contract only. CONV-00 does not add runtime persistence, change the public release version, or make conversations survive application restart.

Conversation persistence is intentionally **not** part of the repository Control DB. The Control DB is authoritative repository state with its own S0 invariants and generation lifecycle. Conversation history is user data with independent retention, deletion, navigation and recovery semantics.

## Goals

The persistence layer must eventually provide:

- durable local conversations across application restarts;
- stable conversation identity, title and ordering metadata;
- exact pinned AI-model name and provider digest when available;
- exact repository-generation identity for repository-backed conversations;
- enough repository snapshot identity to render and validate historical provenance independently of current repository authority;
- committed user/assistant provider history;
- durable citation provenance for grounded assistant turns;
- safe restoration into either a continuable conversation or an explicitly read-only historical conversation.

The persistence layer must not:

- become repository READY authority;
- modify or repin scientific repositories;
- persist transient grounding evidence blocks as provider history;
- treat temporary UI/system error messages as committed provider conversation history;
- silently substitute a different AI model, model digest, repository generation or repository snapshot when restoring a conversation;
- make Control DB validity depend on conversation-history validity.

## Storage boundary

Conversation data uses a separate application-owned SQLite database:

`conversations.sqlite3`

The file lives under the already qualified XDG state root, alongside but independent from `control-state.sqlite3`.

The conversation store must not be opened through SQLite `ATTACH` from the Control DB connection. Repository authority and conversation user data therefore retain independent failure, migration and transaction boundaries.

The conversation DB should reuse the qualified SQLite baseline already established by Control State where applicable:

- SQLite >= 3.37.0;
- `SQLITE_OPEN_NOFOLLOW`;
- genuinely read/write `main` database;
- `SQLITE_DBCONFIG_DEFENSIVE = 1`;
- `SQLITE_DBCONFIG_TRUSTED_SCHEMA = 0`;
- legacy DQS parsing disabled;
- triggers explicitly enabled;
- foreign keys enabled;
- WAL journal mode;
- FULL synchronous durability;
- bounded busy timeout;
- schema identity, `application_id`, `user_version`, integrity and foreign-key validation.

Conversation-store failure is isolated from repository authority. A corrupt or unavailable conversation DB must not make a valid Control DB or repository snapshot invalid. The application may continue to support new in-memory chats while clearly indicating that durable conversation storage is unavailable; it must not claim those chats are saved.

## Schema v1 design

The first durable schema should use STRICT tables and explicit foreign keys.

### `installation`

Singleton schema identity for the conversation store.

It exists to distinguish the conversation database from Control State or an unrelated SQLite file and to support later schema migration.

### `conversations`

One row per durable chat.

Required identity/state:

- stable locally generated `conversation_id`;
- title;
- creation and last-update timestamps;
- pinned model name;
- optional pinned provider model digest;
- pinned repository generation ID, where `0` means no repository-backed scope;
- lifecycle state sufficient for normal/open/archived or non-continuable historical presentation.

A repository-backed conversation must not be persisted as continuable unless its generation ID is positive.

### `conversation_repositories`

One row per repository pinned to a conversation.

Persist:

- `conversation_id`;
- repository ID;
- repository version;
- exact snapshot SHA.

This is historical provenance, not an alternate repository authority. It records what the conversation used even if the Control DB later advances.

### `messages`

Ordered committed provider-history messages.

Persist:

- stable message identity;
- conversation ID;
- monotonic sequence number;
- role restricted to committed provider roles needed for reconstruction;
- provider content;
- display content;
- grounded flag;
- timestamp.

For ordinary user and assistant messages, provider/display content may be equal.

For grounded assistant messages, `provider_content` retains the raw assistant answer including temporary source labels because that is what subsequent provider turns historically saw. `display_content` retains the user-visible answer after temporary repository source labels are removed.

Transient grounding system instructions, evidence blocks and post-evidence reminders are not persisted as conversation messages. They are turn-local retrieval material, not provider history.

Transient `System:` errors currently rendered by the UI are not committed provider history and are not part of the initial durable message contract.

### `citations`

Zero or more citations belonging to one grounded assistant message.

Persist the provenance currently exposed by `CitationReference`:

- citation ordinal;
- original temporary label;
- repository ID;
- repository version;
- exact snapshot SHA;
- logical source ID;
- source path;
- locator;
- optional title;
- optional excerpt.

Where an immutable source permalink can be derived or is already available, the durable representation may also store it as a convenience value, but repository/SHA/source identity remains the canonical provenance basis.

## Transaction boundary

SQLite gives atomic commit within one database transaction. Each durable conversation turn must therefore be written to `conversations.sqlite3` as one transaction rather than as independent message/citation writes.

### Ungrounded turn

The provider call must support deferred provider-history commit.

Required order:

1. generate the complete assistant response without mutating `OllamaConversation`;
2. begin one conversation-store transaction;
3. write the committed user message and assistant message plus conversation metadata;
4. commit the SQLite transaction;
5. only after durable commit succeeds, call `OllamaConversation.commit_exchange()`;
6. publish the final committed turn to the transcript.

A persistence failure must not silently advance in-memory provider history as though the turn had been saved.

### Grounded turn

Grounded provider calls already use deferred provider-history mutation.

Required order:

1. prepare the grounded retrieval turn;
2. generate the complete raw assistant response;
3. resolve citations;
4. reject unknown or incomplete citation provenance;
5. compute display content;
6. commit the retrieval/session turn;
7. atomically persist user message, raw/display assistant message, citation rows and conversation metadata;
8. only after durable DB commit succeeds, call `OllamaConversation.commit_exchange()`;
9. publish the final committed transcript/citations.

If durable DB commit fails after the retrieval/session turn has already committed, the application must fail closed for continuation of that in-memory conversation. It may be reloaded from the last durable boundary, but it must not continue with provider/retrieval state that the durable record does not contain.

A later implementation may tighten this boundary further, but it must not weaken it by committing provider history before persistence.

### Titles

Automatic title generation is presentation metadata, not part of the turn's semantic commit. A title update may be persisted in its own small transaction after the turn commit. Failure to update a title must not invalidate a successfully committed conversation turn.

## Session pinning and restoration

A restored transcript must remain viewable even when its original provider model or repository context is no longer available.

Continuation is a separate qualification step.

### Model qualification

To continue a restored conversation:

- the exact pinned model name must be available;
- if a provider digest was pinned, the current digest must match exactly.

No silent fallback to another model or digest is allowed.

### Repository qualification

For a repository-backed restored conversation:

- the recorded repository generation must still exist as an immutable COMPLETE generation;
- each persisted repository identity/version/SHA must match that generation;
- the required local snapshots/indexes/seals must pass the normal repository validation path.

No silent repin to current `active_state` is allowed.

If exact context cannot be requalified, the conversation opens as historical/read-only. Its transcript and persisted provenance remain inspectable, but Send is disabled for that conversation.

### Reconstructing provider history

Only committed durable user/assistant messages are replayed into a fresh `OllamaConversation`, in sequence order.

Grounded assistant replay uses stored `provider_content`, not the stripped display text.

Persisted citation rows are used to rebuild provenance UI; retrieval evidence blocks are regenerated only for future turns after the pinned context has been requalified.

## Failure and corruption policy

Conversation persistence has its own qualification result.

A foreign, newer, corrupt or semantically invalid conversation database fails the persistence subsystem closed. It must not be auto-replaced, silently discarded or interpreted as an empty conversation history.

At the same time, that failure does not downgrade repository Control DB authority. The application may still provide explicitly unsaved in-memory chat when the provider is available.

A future repair/export workflow must be explicit. Automatic destructive recovery is outside the initial persistence scope.

## Privacy boundary

Conversation storage remains local application data.

The persistence layer must not:

- upload conversation history to GitHub or any cloud service;
- grant additional broad Home/host filesystem access;
- copy scientific repository contents into the conversation DB beyond the minimal provenance/excerpt data already associated with committed citations.

Provider requests continue to follow the existing local-provider boundary.

## Initial rollout gates

### CONV-00 — architecture contract

Documentation only.

Define storage ownership, schema concepts, transaction boundaries, restore semantics and failure isolation before runtime work.

### CONV-01 — database foundation

Add the separate conversation SQLite schema, hardened open/validation layer and tests.

The foundation uses `atm-conversation-store/1` with its own SQLite `application_id`, STRICT tables and the same hardened SQLite connection baseline as Control State. Structural and semantic validation are independent of repository Control DB validation.

CONV-01 deliberately exposes no live-chat write or restore path. GTK and `Application` do not open the conversation database yet, so public behavior remains in-memory-only until later gates.

Do not wire GTK or persist live chats yet.

### CONV-02 — durable write path

Add a conversation-domain store and atomic committed-turn writes.

CONV-02a implements and qualifies the native write boundary. Conversation creation plus repository pins are atomic, and each turn is one store-owned transaction containing the paired user/assistant messages, optional citations and metadata update. The store allocates local identities and turn/sequence numbers and rolls back the complete turn on any failure.

CONV-02b wires that qualified boundary into the Vala conversation domain and development GTK send path. Persistence uses the exact repository ID/version/SHA pins retained by the frozen conversation grounding/session rather than re-reading a later Control DB active generation. Both grounded and ungrounded provider calls defer `OllamaConversation.commit_exchange()`; the shared turn committer advances provider history only after the durable SQLite turn succeeds.

If the separate conversation store is unavailable, AtM keeps ordinary live chat available only as an explicitly disclosed unsaved in-memory session. For a grounded turn, retrieval/session commit still precedes durable persistence as defined above; if persistence then fails, the tab is marked non-continuable instead of allowing provider/retrieval/durable state to diverge.

CONV-02 still does not restore persisted conversations after restart and does not add history navigation. Those remain CONV-03/CONV-04.

### CONV-03 — restore and continuation qualification

Load durable conversations, rebuild provider history/provenance and qualify exact model/repository context before enabling Send.

CONV-03a provides the read-only restore boundary. Conversation summaries and complete snapshots are materialized from one SQLite read transaction. A snapshot includes the pinned model/digest, repository generation and exact repository version/SHA set, ordered committed messages, raw provider content, display content and citation provenance. Provider history is reconstructed only from committed user/assistant `provider_content` pairs.

Snapshot loading alone never enables continuation.

CONV-03b provides that continuation-qualification boundary. Repository-backed restore can reconstruct frozen grounding from the exact persisted COMPLETE `repository_generation_id`, validating its local snapshot, version, persistent seal and retrieval index without replacing or mutating current `active_state`. The reconstructed repository ID/version/SHA set must then match the durable conversation pins exactly.

Model qualification uses the locally discovered provider inventory. The model name must match exactly and, when the conversation persisted a provider digest, that digest must also match exactly. A conversation persisted without a digest remains name-pinned only.

These primitives do not themselves enable Send or create restored GTK tabs. CONV-04 must invoke both exact repository and model qualification before making a restored conversation continuable. Historical conversations whose exact context is unavailable remain viewable but non-continuable.

Before entering CONV-04, the live Vala committed-turn bridge also preserves the canonical immutable permalink for grounded citations when it can be constructed from the pinned repository revision and locator. This closes the remaining provenance write asymmetry between native storage and live GTK-originated turns.

### CONV-04 — GTK lifecycle

Add startup loading, durable conversation navigation and explicit archive/delete behavior to the existing multi-chat notebook without changing repository authority semantics.

CONV-04a restores non-archived durable conversations into the existing scrollable `Gtk.Notebook`. Restored tabs reconstruct provider history from durable `provider_content`, render user-visible transcript from durable `display_content`, and rebuild grounded citation buttons from durable provenance including the persisted immutable permalink. These tabs are explicitly view-only: prompt and Send remain disabled regardless of local model/repository availability.

Startup restore is idempotent and runs only after both the conversation store and notebook exist. The initial `New` tab is removed only when at least one durable conversation was restored and that tab remains completely pristine; user input started during asynchronous startup is never discarded.

CONV-04b orchestrates the CONV-03 qualification primitives. A restored tab remains view-only while the durable snapshot is reloaded, its persisted model name/digest is matched against the discovered local Ollama inventory, and its exact historical repository generation is reconstructed and revalidated. The reconstructed repository ID/version/SHA set must match the durable pins exactly. Only after all checks pass is `ConversationSession.begin()` called on that historical grounding and the restored prompt/Send boundary enabled. Model and repository selectors remain locked, so continuation cannot silently repin the conversation.

Qualification is re-entrant and retryable. A tab that cannot currently satisfy its exact context remains readable with an explicit read-only reason; model discovery/refresh, repository refresh/update and completion of another active generation can request a new qualification attempt. Failures never replace the historical context with current authority.

CONV-04c adds explicit archive/delete lifecycle without overloading the tab close affordance. Close remains an in-memory UI operation and never mutates durable history. A separate per-tab actions menu exposes Archive and Delete only for conversations that already have durable identity.

Archive atomically sets the durable `archived` flag and advances `updated_at_us` before the tab is removed from the current notebook. Archived conversations are excluded from normal startup restore but remain present in the conversation store; the storage API also supports unarchive for future history-management UI.

Delete is a distinct destructive action with explicit confirmation. The store deletes exactly one conversation inside one write transaction; schema-v1 foreign-key cascades remove its repository pins, messages and citation rows. If archive or delete fails, the tab remains open and no successful lifecycle transition is presented.

Retention policy, export/import, archived-history browsing and bulk history management remain later work.

## Acceptance rules for later implementation

The implementation gates must demonstrate at minimum:

- conversation DB and Control DB are distinct files and failure domains;
- no committed turn can have citations without its committed assistant message;
- no assistant message can become durable without its paired user message in the same turn transaction;
- provider history is never advanced before the durable turn boundary;
- grounded raw provider content and user-visible display content remain distinguishable;
- exact model/digest and repository-generation identity survive restart;
- restoration never silently repins model or repository context;
- historical transcript/provenance remains inspectable even when continuation cannot be qualified;
- persistence failure never masquerades as successful saving;
- conversation-store corruption does not invalidate otherwise qualified repository authority.
