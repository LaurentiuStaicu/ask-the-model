# STATE-01 — AtM Control State foundation

STATE-01 introduces an SQLite control-plane database in **shadow mode**.

It does not replace `repository-state.json`, does not import legacy state and does not change repository authority.

## File identity

The database uses:

- schema ID: `atm-control-state/1`
- SQLite `application_id`: `0x41544D31` (`ATM1`)
- SQLite `user_version`: `1`

The application ID is checked before an existing database is accepted.

## SQLite safety profile

Every connection enables and verifies:

- `foreign_keys=ON`
- `journal_mode=WAL`
- `synchronous=FULL`
- a bounded busy timeout

The store also runs `PRAGMA integrity_check` and `PRAGMA foreign_key_check` during validation.

## Initial schema

The v1 schema establishes only the future control-state backbone:

- `installation`
- `repository_generations`
- `generation_repositories`
- `active_state`
- `migration_ledger`

STATE-01 exposes no API to activate a generation or mutate repository authority. Those transitions are intentionally deferred until later STATE PRs.

## Shadow-mode rule

Creating or validating `control-state.sqlite3` must not read, rewrite, repair or otherwise mutate `repository-state.json`.

## STATE-02 — deterministic legacy import

STATE-02 adds an explicit, test-only/runtime-dormant import API from the existing `repository-state.json` format into the Control DB.

The importer:

- accepts exactly the same legacy schema versions currently accepted by `RepositoryStateStore` (v1 and v2);
- preserves the same repository-id, SHA, version, duplicate-id and snapshot-seal validation rules;
- creates only generation `1` in an otherwise empty and inactive Control DB;
- inserts repository rows in fixed repository-id order so JSON array ordering cannot affect the imported state;
- performs the whole import under one `BEGIN IMMEDIATE` transaction;
- verifies semantic equivalence against the parsed legacy state before marking the generation `COMPLETE`;
- rolls back on parse, validation, SQL or equivalence failure;
- never writes, repairs, renames or deletes the legacy JSON file;
- leaves `active_state.active_repository_generation` as `NULL`.

A semantically equivalent JSON file may differ in whitespace, member ordering and repository-array ordering and still compare equal after import. Byte identity is deliberately not the equivalence criterion.

A second import into a non-empty generation store is refused. STATE-02 therefore cannot silently replace or reinterpret a previously imported candidate.

The application does not call this import path yet. `repository-state.json` remains authoritative throughout STATE-02.

STATE-03 remains the only planned point where SQLite can become authoritative, and only after a verified one-time cutover.


## STATE-03a — atomic cutover publication primitive

STATE-03a adds the **publication primitive only**. It is intentionally not called by application startup yet, so repository authority remains unchanged until the later STATE-03 wiring gate.

For a previously absent authoritative `control-state.sqlite3`, the primitive:

- validates legacy `repository-state.json` before creating a candidate database;
- builds the SQLite candidate in the same state directory under a private temporary name;
- when legacy state exists, reuses the STATE-02 deterministic import, verifies semantic equivalence, activates generation `1`, and records `state-03a-cutover-v1` in `migration_ledger`;
- when no legacy state exists, records an empty-bootstrap cutover with no active generation;
- validates the complete candidate and forces a WAL `TRUNCATE` checkpoint;
- closes the only SQLite connection and refuses publication if WAL/SHM sidecars remain;
- fsyncs the candidate database;
- publishes with Linux `renameat2(..., RENAME_NOREPLACE)`, with a no-clobber hard-link fallback only when the filesystem/kernel does not support that flag;
- fsyncs the containing directory after publication;
- never overwrites an existing authoritative database and never mutates the legacy JSON file.

A failure before publication removes the private candidate and sidecars. If publication has already happened but the final directory fsync reports an error, the final database is left intact rather than being destructively rolled back.

STATE-03a still does **not** make SQLite authoritative in the running application. STATE-03b/03c will separately qualify the DB-backed repository-state adapter and startup authority switch.


## STATE-03b — DB-backed repository-state adapter

STATE-03b adds a Vala repository-state adapter backed exclusively by the Control DB. It is compiled and tested independently but is still not wired into application startup or `RepositoryLifecycleService`.

The adapter deliberately has no JSON fallback:

- missing `control-state.sqlite3` is reported as `ABSENT`;
- malformed, foreign, incomplete, or authority-inconsistent SQLite is reported as `INVALID`;
- only an active `COMPLETE` repository generation can be read;
- an empty, already-published Control DB is a valid state with no ready repositories;
- normal mutations never create the authoritative database file.

For the current pre-STATE-04 semantics, `set_current` updates the active COMPLETE generation transactionally. If the already-published Control DB is an empty bootstrap, the first successful `set_current` creates and activates generation `1` with origin `runtime-state-v1`. `set_snapshot_seal` requires the exact active snapshot SHA.

The Vala adapter mirrors the current JSON store's validation and rollback behavior: invalid SHA/version/seal inputs are rejected before persistence, and an SQLite write failure restores the in-memory record.

STATE-03b does not implement immutable generations. That remains a separate STATE-04 qualification step.


## STATE-03c — one-time authority switch

STATE-03c wires the previously qualified pieces into production and completes the one-time repository-state authority cutover.

Startup authority resolution now follows a single rule:

1. if `control-state.sqlite3` exists, it is the only repository-state authority;
2. if it is valid, legacy `repository-state.json` is ignored even if it later changes or becomes invalid;
3. if it exists but is invalid, startup fails closed and does **not** fall back to JSON;
4. only when the Control DB is absent may STATE-03a publish a verified migration from legacy JSON or an empty bootstrap.

`StartupQualificationService` reconciles repository snapshots from `ControlRepositoryStateStore` and enrolls missing snapshot seals into the Control DB. Repository-state validity is now part of `installation_qualified`.

`RepositoryLifecycleService` is also Control-DB-backed. It may be constructed before startup qualification, but repository operations remain blocked. After startup qualification, the application explicitly reloads the Control DB and only then applies the qualification result. A failed reload keeps repository operations blocked.

The legacy JSON file is retained as migration/recovery evidence and is no longer written by normal production repository-state operations.

STATE-03 therefore ends dual authority: after a successful cutover, Control DB is authoritative. STATE-04 remains responsible for making repository generations immutable and pinning sessions to explicit generation IDs.
