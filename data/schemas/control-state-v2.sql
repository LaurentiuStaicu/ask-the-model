PRAGMA encoding = 'UTF-8';

CREATE TABLE installation (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL CHECK (schema_id = 'atm-control-state/2')
) STRICT;

INSERT INTO installation(singleton_id, schema_id)
VALUES(1, 'atm-control-state/2');

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

CREATE TRIGGER trg_installation_immutable_update
BEFORE UPDATE ON installation
BEGIN
    SELECT RAISE(ABORT, 'control-state installation identity is immutable');
END;

CREATE TRIGGER trg_installation_immutable_delete
BEFORE DELETE ON installation
BEGIN
    SELECT RAISE(ABORT, 'control-state installation identity is immutable');
END;

CREATE TRIGGER trg_repository_generations_no_complete_insert
BEFORE INSERT ON repository_generations
WHEN NEW.lifecycle = 'COMPLETE'
BEGIN
    SELECT RAISE(ABORT, 'repository generation must be completed from CANDIDATE');
END;

CREATE TRIGGER trg_repository_generations_complete_update
BEFORE UPDATE ON repository_generations
WHEN OLD.lifecycle = 'COMPLETE'
BEGIN
    SELECT RAISE(ABORT, 'complete repository generation is immutable');
END;

CREATE TRIGGER trg_repository_generations_complete_delete
BEFORE DELETE ON repository_generations
WHEN OLD.lifecycle = 'COMPLETE'
BEGIN
    SELECT RAISE(ABORT, 'complete repository generation is immutable');
END;

CREATE TRIGGER trg_generation_repositories_complete_insert
BEFORE INSERT ON generation_repositories
WHEN EXISTS (
    SELECT 1
    FROM repository_generations
    WHERE generation_id = NEW.generation_id
      AND lifecycle = 'COMPLETE'
)
BEGIN
    SELECT RAISE(ABORT, 'complete repository generation rows are immutable');
END;

CREATE TRIGGER trg_generation_repositories_complete_update
BEFORE UPDATE ON generation_repositories
WHEN EXISTS (
    SELECT 1
    FROM repository_generations
    WHERE generation_id = OLD.generation_id
      AND lifecycle = 'COMPLETE'
)
OR EXISTS (
    SELECT 1
    FROM repository_generations
    WHERE generation_id = NEW.generation_id
      AND lifecycle = 'COMPLETE'
)
BEGIN
    SELECT RAISE(ABORT, 'complete repository generation rows are immutable');
END;

CREATE TRIGGER trg_generation_repositories_complete_delete
BEFORE DELETE ON generation_repositories
WHEN EXISTS (
    SELECT 1
    FROM repository_generations
    WHERE generation_id = OLD.generation_id
      AND lifecycle = 'COMPLETE'
)
BEGIN
    SELECT RAISE(ABORT, 'complete repository generation rows are immutable');
END;

CREATE TRIGGER trg_active_state_guard_update
BEFORE UPDATE OF active_repository_generation ON active_state
WHEN (
    OLD.active_repository_generation IS NOT NULL
    AND NEW.active_repository_generation IS NULL
)
OR (
    NEW.active_repository_generation IS NOT NULL
    AND NOT EXISTS (
        SELECT 1
        FROM repository_generations
        WHERE generation_id = NEW.active_repository_generation
          AND lifecycle = 'COMPLETE'
    )
)
OR (
    OLD.active_repository_generation IS NOT NULL
    AND NEW.active_repository_generation IS NOT NULL
    AND NEW.active_repository_generation <= OLD.active_repository_generation
)
BEGIN
    SELECT RAISE(ABORT, 'active repository generation must advance to a newer COMPLETE generation');
END;

CREATE TRIGGER trg_active_state_no_delete
BEFORE DELETE ON active_state
BEGIN
    SELECT RAISE(ABORT, 'active_state singleton is immutable');
END;

CREATE TRIGGER trg_migration_ledger_append_only_update
BEFORE UPDATE ON migration_ledger
BEGIN
    SELECT RAISE(ABORT, 'migration ledger is append-only');
END;

CREATE TRIGGER trg_migration_ledger_append_only_delete
BEFORE DELETE ON migration_ledger
BEGIN
    SELECT RAISE(ABORT, 'migration ledger is append-only');
END;
