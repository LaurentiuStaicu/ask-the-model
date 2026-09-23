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
