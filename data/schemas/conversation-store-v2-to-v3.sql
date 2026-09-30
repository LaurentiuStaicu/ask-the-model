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

CREATE INDEX idx_chart_series_repository_snapshot
ON chart_series(repository_id, snapshot_sha);

CREATE TABLE installation_v3 (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL
        CHECK (schema_id = 'atm-conversation-store/3')
) STRICT;

INSERT INTO installation_v3(singleton_id, schema_id)
VALUES(1, 'atm-conversation-store/3');

DROP TABLE installation;
ALTER TABLE installation_v3 RENAME TO installation;
