ALTER TABLE conversations
ADD COLUMN open_on_startup INTEGER NOT NULL DEFAULT 1
    CHECK (open_on_startup IN (0, 1));

UPDATE installation
SET schema_id = 'atm-conversation-store/2'
WHERE singleton_id = 1
  AND schema_id = 'atm-conversation-store/1';
