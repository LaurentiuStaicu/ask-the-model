PRAGMA encoding = 'UTF-8';

CREATE TABLE installation (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL CHECK (schema_id = 'atm-control-state/1')
) STRICT;

INSERT INTO installation(singleton_id, schema_id)
VALUES(1, 'atm-control-state/1');

CREATE TABLE repository_generations (
    generation_id INTEGER PRIMARY KEY,
    lifecycle TEXT NOT NULL
        CHECK (lifecycle IN ('CANDIDATE', 'COMPLETE')),
    origin TEXT NOT NULL CHECK (length(origin) > 0)
) STRICT;

CREATE TABLE generation_repositories (
    generation_id INTEGER NOT NULL
        REFERENCES repository_generations(generation_id)
        ON DELETE RESTRICT,
    repository_id TEXT NOT NULL
        CHECK (repository_id IN ('ewd', 'cbd', 'rmd')),
    snapshot_sha TEXT NOT NULL
        CHECK (
            length(snapshot_sha) = 40
            AND snapshot_sha NOT GLOB '*[^0-9a-f]*'
        ),
    repository_version TEXT NOT NULL
        CHECK (length(repository_version) > 0),
    snapshot_seal_sha256 TEXT
        CHECK (
            snapshot_seal_sha256 IS NULL
            OR (
                length(snapshot_seal_sha256) = 64
                AND snapshot_seal_sha256 NOT GLOB '*[^0-9a-f]*'
            )
        ),
    PRIMARY KEY (generation_id, repository_id)
) STRICT;

CREATE TABLE active_state (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    active_repository_generation INTEGER
        REFERENCES repository_generations(generation_id)
        ON DELETE RESTRICT
) STRICT;

INSERT INTO active_state(
    singleton_id,
    active_repository_generation
) VALUES(1, NULL);

CREATE TABLE migration_ledger (
    migration_id TEXT PRIMARY KEY
        CHECK (length(migration_id) > 0),
    schema_version INTEGER NOT NULL
        CHECK (schema_version >= 1),
    applied_origin TEXT NOT NULL
        CHECK (length(applied_origin) > 0)
) STRICT;
