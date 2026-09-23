ALTER TABLE conversations
ADD COLUMN open_on_startup INTEGER NOT NULL DEFAULT 1
    CHECK (open_on_startup IN (0, 1));

CREATE TABLE installation_v2 (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL
        CHECK (schema_id = 'atm-conversation-store/2')
) STRICT;

INSERT INTO installation_v2(singleton_id, schema_id)
VALUES(1, 'atm-conversation-store/2');

DROP TABLE installation;
ALTER TABLE installation_v2 RENAME TO installation;
