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

STATE-02 will add a deterministic legacy importer and semantic equivalence tests. STATE-03 is the only planned point where SQLite becomes authoritative after a verified one-time cutover.
