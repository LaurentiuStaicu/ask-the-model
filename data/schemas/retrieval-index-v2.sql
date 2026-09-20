PRAGMA foreign_keys = ON;
PRAGMA trusted_schema = OFF;
PRAGMA user_version = 2;

CREATE TABLE snapshot_metadata (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    repository_id TEXT NOT NULL,
    repository_version TEXT NOT NULL,
    snapshot_sha TEXT NOT NULL CHECK (
        length(snapshot_sha) = 40
        AND snapshot_sha NOT GLOB '*[^0-9a-f]*'
    ),
    manifest_schema_version INTEGER NOT NULL CHECK (
        manifest_schema_version >= 1
    ),
    manifest_sha256 TEXT NOT NULL CHECK (
        length(manifest_sha256) = 64
        AND manifest_sha256 NOT GLOB '*[^0-9a-f]*'
    ),
    created_at_utc TEXT NOT NULL
);

CREATE TABLE source_files (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    sha256 TEXT NOT NULL CHECK (
        length(sha256) = 64
        AND sha256 NOT GLOB '*[^0-9a-f]*'
    ),
    byte_size INTEGER NOT NULL CHECK (byte_size >= 0),
    media_type TEXT NOT NULL,
    logical_source_id TEXT NOT NULL UNIQUE
);

CREATE TABLE source_roles (
    source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK (
        role IN (
            'canonical',
            'status',
            'structural',
            'evidence',
            'tabular',
            'implementation'
        )
    ),
    PRIMARY KEY (source_id, role)
) WITHOUT ROWID;

CREATE INDEX source_roles_by_role
ON source_roles(role, source_id);

CREATE TABLE document_sections (
    id INTEGER PRIMARY KEY,
    source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL CHECK (ordinal >= 0),
    heading_path TEXT,
    locator TEXT NOT NULL,
    logical_source_id TEXT NOT NULL UNIQUE,
    title TEXT,
    body TEXT NOT NULL,
    UNIQUE (source_id, ordinal)
);

CREATE INDEX document_sections_by_source
ON document_sections(source_id, ordinal);

CREATE TABLE structured_entities (
    id INTEGER PRIMARY KEY,
    source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
    entity_type TEXT NOT NULL,
    native_id TEXT,
    logical_source_id TEXT NOT NULL UNIQUE,
    locator TEXT NOT NULL,
    label TEXT,
    payload_json TEXT NOT NULL
);

CREATE INDEX structured_entities_by_native_id
ON structured_entities(native_id);

CREATE INDEX structured_entities_by_type
ON structured_entities(entity_type);

CREATE TABLE structured_relations (
    id INTEGER PRIMARY KEY,
    source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
    relation_type TEXT NOT NULL,
    native_id TEXT,
    logical_source_id TEXT NOT NULL UNIQUE,
    locator TEXT NOT NULL,
    from_logical_source_id TEXT,
    to_logical_source_id TEXT,
    payload_json TEXT NOT NULL
);

CREATE INDEX structured_relations_by_native_id
ON structured_relations(native_id);

CREATE INDEX structured_relations_by_type
ON structured_relations(relation_type);

CREATE TABLE datasets (
    id INTEGER PRIMARY KEY,
    source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
    native_id TEXT,
    logical_source_id TEXT NOT NULL UNIQUE,
    locator TEXT NOT NULL,
    title TEXT,
    metadata_json TEXT NOT NULL
);

CREATE INDEX datasets_by_source
ON datasets(source_id);

CREATE TABLE dataset_rows (
    id INTEGER PRIMARY KEY,
    dataset_id INTEGER NOT NULL REFERENCES datasets(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL CHECK (ordinal >= 0),
    row_key TEXT,
    locator TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    search_text TEXT,
    UNIQUE (dataset_id, ordinal)
);

CREATE INDEX dataset_rows_by_dataset_key
ON dataset_rows(dataset_id, row_key);

CREATE VIRTUAL TABLE search_fts USING fts5(
    evidence_kind UNINDEXED,
    evidence_id UNINDEXED,
    logical_source_id UNINDEXED,
    title,
    body,
    tokenize = 'unicode61 remove_diacritics 2'
);
