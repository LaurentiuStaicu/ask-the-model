PRAGMA encoding = 'UTF-8';

CREATE TABLE installation (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL
        CHECK (schema_id = 'atm-conversation-store/3')
) STRICT;

INSERT INTO installation(singleton_id, schema_id)
VALUES(1, 'atm-conversation-store/3');

CREATE TABLE conversations (
    conversation_id TEXT PRIMARY KEY
        CHECK (length(conversation_id) > 0),
    title TEXT NOT NULL
        CHECK (length(title) > 0),
    created_at_us INTEGER NOT NULL
        CHECK (created_at_us >= 0),
    updated_at_us INTEGER NOT NULL
        CHECK (updated_at_us >= created_at_us),
    model_name TEXT NOT NULL
        CHECK (length(model_name) > 0),
    model_digest TEXT
        CHECK (model_digest IS NULL OR length(model_digest) > 0),
    repository_generation_id INTEGER NOT NULL
        CHECK (repository_generation_id >= 0),
    archived INTEGER NOT NULL DEFAULT 0
        CHECK (archived IN (0, 1)),
    open_on_startup INTEGER NOT NULL DEFAULT 1
        CHECK (open_on_startup IN (0, 1))
) STRICT;

CREATE TABLE conversation_repositories (
    conversation_id TEXT NOT NULL
        REFERENCES conversations(conversation_id)
        ON DELETE CASCADE,
    repository_id TEXT NOT NULL
        CHECK (repository_id IN ('ewd', 'cbd', 'rmd')),
    repository_version TEXT NOT NULL
        CHECK (length(repository_version) > 0),
    snapshot_sha TEXT NOT NULL
        CHECK (
            length(snapshot_sha) = 40
            AND snapshot_sha NOT GLOB '*[^0-9a-f]*'
        ),
    PRIMARY KEY (conversation_id, repository_id)
) STRICT;

CREATE TABLE messages (
    message_id TEXT PRIMARY KEY
        CHECK (length(message_id) > 0),
    conversation_id TEXT NOT NULL
        REFERENCES conversations(conversation_id)
        ON DELETE CASCADE,
    sequence_no INTEGER NOT NULL
        CHECK (sequence_no >= 0),
    turn_no INTEGER NOT NULL
        CHECK (turn_no >= 0),
    role TEXT NOT NULL
        CHECK (role IN ('user', 'assistant')),
    provider_content TEXT NOT NULL,
    display_content TEXT NOT NULL,
    grounded INTEGER NOT NULL
        CHECK (grounded IN (0, 1)),
    created_at_us INTEGER NOT NULL
        CHECK (created_at_us >= 0),
    UNIQUE (conversation_id, sequence_no),
    UNIQUE (conversation_id, turn_no, role)
) STRICT;

CREATE TABLE citations (
    message_id TEXT NOT NULL
        REFERENCES messages(message_id)
        ON DELETE CASCADE,
    ordinal INTEGER NOT NULL
        CHECK (ordinal >= 1),
    label TEXT NOT NULL
        CHECK (length(label) > 0),
    repository_id TEXT NOT NULL
        CHECK (repository_id IN ('ewd', 'cbd', 'rmd')),
    repository_version TEXT NOT NULL
        CHECK (length(repository_version) > 0),
    snapshot_sha TEXT NOT NULL
        CHECK (
            length(snapshot_sha) = 40
            AND snapshot_sha NOT GLOB '*[^0-9a-f]*'
        ),
    logical_source_id TEXT NOT NULL
        CHECK (length(logical_source_id) > 0),
    source_path TEXT NOT NULL
        CHECK (length(source_path) > 0),
    locator TEXT NOT NULL
        CHECK (length(locator) > 0),
    title TEXT,
    excerpt TEXT,
    immutable_permalink TEXT,
    PRIMARY KEY (message_id, ordinal)
) STRICT;

CREATE TABLE message_charts (
    message_id TEXT NOT NULL
        REFERENCES messages(message_id)
        ON DELETE CASCADE,
    ordinal INTEGER NOT NULL
        CHECK (ordinal BETWEEN 1 AND 4),
    chart_schema TEXT NOT NULL
        CHECK (chart_schema = 'atm-chart-spec/1'),
    chart_spec_id TEXT NOT NULL
        CHECK (
            length(chart_spec_id) = 64
            AND chart_spec_id NOT GLOB '*[^0-9a-f]*'
        ),
    chart_kind TEXT NOT NULL
        CHECK (chart_kind IN ('line', 'scatter')),
    reconstruction_profile TEXT NOT NULL
        CHECK (
            reconstruction_profile =
            'atm-chart-reconstruct/gistemp-complete-annual/1'
        ),
    PRIMARY KEY (message_id, ordinal)
) STRICT;

CREATE TABLE chart_series (
    message_id TEXT NOT NULL,
    chart_ordinal INTEGER NOT NULL
        CHECK (chart_ordinal BETWEEN 1 AND 4),
    series_ordinal INTEGER NOT NULL
        CHECK (series_ordinal BETWEEN 0 AND 3),
    series_profile TEXT NOT NULL
        CHECK (
            series_profile =
            'atm-series/gistemp-complete-annual/1'
        ),
    admission_profile TEXT NOT NULL
        CHECK (
            admission_profile =
            'atm-gistemp-pinned-admission/1'
        ),
    scientific_id TEXT NOT NULL
        CHECK (
            length(scientific_id) = 64
            AND scientific_id NOT GLOB '*[^0-9a-f]*'
        ),
    qualified_id TEXT NOT NULL
        CHECK (
            length(qualified_id) = 64
            AND qualified_id NOT GLOB '*[^0-9a-f]*'
        ),
    repository_id TEXT NOT NULL
        CHECK (repository_id = 'ewd'),
    repository_version TEXT NOT NULL
        CHECK (length(repository_version) > 0),
    snapshot_sha TEXT NOT NULL
        CHECK (
            length(snapshot_sha) = 40
            AND snapshot_sha NOT GLOB '*[^0-9a-f]*'
        ),
    source_path TEXT NOT NULL
        CHECK (length(source_path) > 0),
    PRIMARY KEY (message_id, chart_ordinal, series_ordinal),
    FOREIGN KEY (message_id, chart_ordinal)
        REFERENCES message_charts(message_id, ordinal)
        ON DELETE CASCADE
) STRICT;

CREATE INDEX idx_messages_conversation_sequence
ON messages(conversation_id, sequence_no);

CREATE INDEX idx_citations_repository_snapshot
ON citations(repository_id, snapshot_sha);

CREATE INDEX idx_chart_series_repository_snapshot
ON chart_series(repository_id, snapshot_sha);
