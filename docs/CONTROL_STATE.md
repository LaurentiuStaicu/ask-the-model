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
