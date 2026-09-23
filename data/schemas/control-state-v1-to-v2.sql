CREATE TABLE installation_v2 (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL CHECK (schema_id = 'atm-control-state/2')
) STRICT;

INSERT INTO installation_v2(singleton_id, schema_id)
VALUES(1, 'atm-control-state/2');

DROP TABLE installation;
ALTER TABLE installation_v2 RENAME TO installation;

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

INSERT INTO migration_ledger(
    migration_id,
    schema_version,
    applied_origin
) VALUES(
    'state-schema-v1-to-v2',
    2,
    'automatic-open-migration'
);
