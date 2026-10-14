
-- Called inside BEGIN IMMEDIATE by the native migration coordinator.
-- Rebuild only the two version-constrained chart tables; the rest of v3 is
-- untouched. Stage all rows before removing old tables so rollback restores v3.
CREATE TABLE message_charts_v5 (
    message_id TEXT NOT NULL
        REFERENCES messages(message_id)
        ON DELETE CASCADE,
    ordinal INTEGER NOT NULL
        CHECK (ordinal BETWEEN 1 AND 4),
    chart_schema TEXT NOT NULL
        CHECK (chart_schema IN ('atm-chart-spec/1', 'atm-chart-spec/2', 'atm-chart-spec/3')),
    chart_spec_id TEXT NOT NULL
        CHECK (
            length(chart_spec_id) = 64
            AND chart_spec_id NOT GLOB '*[^0-9a-f]*'
        ),
    chart_kind TEXT NOT NULL
        CHECK (chart_kind IN ('line', 'scatter')),
    reconstruction_profile TEXT NOT NULL
        CHECK (reconstruction_profile IN (
            'atm-chart-reconstruct/gistemp-complete-annual/1',
            'atm-chart-reconstruct/energy-institute-tes-complete-annual/1',
            'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1'
        )),
    CHECK (
        (chart_schema = 'atm-chart-spec/1' AND
         reconstruction_profile = 'atm-chart-reconstruct/gistemp-complete-annual/1') OR
        (chart_schema = 'atm-chart-spec/2' AND
         reconstruction_profile = 'atm-chart-reconstruct/energy-institute-tes-complete-annual/1') OR
        (chart_schema = 'atm-chart-spec/3' AND
         reconstruction_profile = 'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1')
    ),
    PRIMARY KEY (message_id, ordinal)
) STRICT;

CREATE TABLE chart_series_v5 (
    message_id TEXT NOT NULL,
    chart_ordinal INTEGER NOT NULL
        CHECK (chart_ordinal BETWEEN 1 AND 4),
    series_ordinal INTEGER NOT NULL
        CHECK (series_ordinal BETWEEN 0 AND 3),
    series_profile TEXT NOT NULL
        CHECK (series_profile IN (
            'atm-series/gistemp-complete-annual/1',
            'atm-series/energy-institute-tes-complete-annual/1',
            'atm-series/bnr-bls-c01-calendar-quarter/1'
        )),
    admission_profile TEXT NOT NULL
        CHECK (admission_profile IN (
            'atm-gistemp-pinned-admission/1',
            'atm-energy-institute-pinned-admission/3',
            'atm-bnr-bls-canonical-panel-admission/1'
        )),
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
        CHECK (repository_id IN ('ewd', 'rmd')),
    repository_version TEXT NOT NULL
        CHECK (length(repository_version) > 0),
    snapshot_sha TEXT NOT NULL
        CHECK (
            length(snapshot_sha) = 40
            AND snapshot_sha NOT GLOB '*[^0-9a-f]*'
        ),
    source_path TEXT NOT NULL
        CHECK (source_path IN (
            'science/data/processed/nasa_gistemp_global_2026.csv',
            'science/data/processed/energy_institute_global_2026.csv',
            'data/processed/bnr_bls_realised_rounds.csv'
        )),
    CHECK (
        (series_profile = 'atm-series/gistemp-complete-annual/1' AND
         admission_profile = 'atm-gistemp-pinned-admission/1' AND
         repository_id = 'ewd' AND
         source_path = 'science/data/processed/nasa_gistemp_global_2026.csv') OR
        (series_profile = 'atm-series/energy-institute-tes-complete-annual/1' AND
         admission_profile = 'atm-energy-institute-pinned-admission/3' AND
         repository_id = 'ewd' AND
         source_path = 'science/data/processed/energy_institute_global_2026.csv' AND
         ((snapshot_sha = 'd9e249339663015f6d1c05752338a955bf64ad0b' AND
           repository_version = 'snapshot:d9e249339663015f6d1c05752338a955bf64ad0b') OR
          (snapshot_sha = 'dc710d8e604c0fff713a3acfdb18ab47a1a902bb' AND
           repository_version = 'snapshot:dc710d8e604c0fff713a3acfdb18ab47a1a902bb'))) OR
        (series_profile = 'atm-series/bnr-bls-c01-calendar-quarter/1' AND
         admission_profile = 'atm-bnr-bls-canonical-panel-admission/1' AND
         repository_id = 'rmd' AND
         source_path = 'data/processed/bnr_bls_realised_rounds.csv' AND
         snapshot_sha = 'e43eb4e899f123d3b1aa0f6e20b7c8ed35434170' AND
         repository_version = 'snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170')

    ),
    PRIMARY KEY (message_id, chart_ordinal, series_ordinal),
    FOREIGN KEY (message_id, chart_ordinal)
        REFERENCES message_charts_v5(message_id, ordinal)
        ON DELETE CASCADE
) STRICT;

INSERT INTO message_charts_v5(
    message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile
)
SELECT message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile
FROM message_charts;

INSERT INTO chart_series_v5(
    message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,
    scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path
)
SELECT message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,
       scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path
FROM chart_series;

DROP TABLE chart_series;
DROP TABLE message_charts;

ALTER TABLE message_charts_v5 RENAME TO message_charts;
ALTER TABLE chart_series_v5 RENAME TO chart_series;

CREATE INDEX idx_chart_series_repository_snapshot
ON chart_series(repository_id, snapshot_sha);

CREATE TRIGGER chart_series_tuple_insert
BEFORE INSERT ON chart_series
FOR EACH ROW
WHEN NOT EXISTS (
    SELECT 1 FROM message_charts c
    WHERE c.message_id = NEW.message_id
      AND c.ordinal = NEW.chart_ordinal
      AND (
        (c.chart_schema = 'atm-chart-spec/1'
         AND c.reconstruction_profile = 'atm-chart-reconstruct/gistemp-complete-annual/1'
         AND NEW.series_profile = 'atm-series/gistemp-complete-annual/1'
         AND NEW.admission_profile = 'atm-gistemp-pinned-admission/1') OR
        (c.chart_schema = 'atm-chart-spec/2'
         AND c.reconstruction_profile = 'atm-chart-reconstruct/energy-institute-tes-complete-annual/1'
         AND NEW.series_profile = 'atm-series/energy-institute-tes-complete-annual/1'
         AND NEW.admission_profile = 'atm-energy-institute-pinned-admission/3') OR
        (c.chart_schema = 'atm-chart-spec/3'
         AND c.reconstruction_profile = 'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1'
         AND NEW.series_profile = 'atm-series/bnr-bls-c01-calendar-quarter/1'
         AND NEW.admission_profile = 'atm-bnr-bls-canonical-panel-admission/1')
      )
)
BEGIN
    SELECT RAISE(ABORT, 'chart series profile does not match chart schema tuple');
END;

CREATE TRIGGER chart_series_tuple_update
BEFORE UPDATE ON chart_series
FOR EACH ROW
WHEN NOT EXISTS (
    SELECT 1 FROM message_charts c
    WHERE c.message_id = NEW.message_id
      AND c.ordinal = NEW.chart_ordinal
      AND (
        (c.chart_schema = 'atm-chart-spec/1'
         AND c.reconstruction_profile = 'atm-chart-reconstruct/gistemp-complete-annual/1'
         AND NEW.series_profile = 'atm-series/gistemp-complete-annual/1'
         AND NEW.admission_profile = 'atm-gistemp-pinned-admission/1') OR
        (c.chart_schema = 'atm-chart-spec/2'
         AND c.reconstruction_profile = 'atm-chart-reconstruct/energy-institute-tes-complete-annual/1'
         AND NEW.series_profile = 'atm-series/energy-institute-tes-complete-annual/1'
         AND NEW.admission_profile = 'atm-energy-institute-pinned-admission/3') OR
        (c.chart_schema = 'atm-chart-spec/3'
         AND c.reconstruction_profile = 'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1'
         AND NEW.series_profile = 'atm-series/bnr-bls-c01-calendar-quarter/1'
         AND NEW.admission_profile = 'atm-bnr-bls-canonical-panel-admission/1')
      )
)
BEGIN
    SELECT RAISE(ABORT, 'chart series profile does not match chart schema tuple');
END;


CREATE TRIGGER message_charts_tuple_update
BEFORE UPDATE ON message_charts
FOR EACH ROW
WHEN EXISTS (
    SELECT 1 FROM chart_series s
    WHERE s.message_id = OLD.message_id
      AND s.chart_ordinal = OLD.ordinal
      AND NOT (
        (NEW.chart_schema = 'atm-chart-spec/1'
         AND NEW.reconstruction_profile = 'atm-chart-reconstruct/gistemp-complete-annual/1'
         AND s.series_profile = 'atm-series/gistemp-complete-annual/1'
         AND s.admission_profile = 'atm-gistemp-pinned-admission/1') OR
        (NEW.chart_schema = 'atm-chart-spec/2'
         AND NEW.reconstruction_profile = 'atm-chart-reconstruct/energy-institute-tes-complete-annual/1'
         AND s.series_profile = 'atm-series/energy-institute-tes-complete-annual/1'
         AND s.admission_profile = 'atm-energy-institute-pinned-admission/3') OR
        (NEW.chart_schema = 'atm-chart-spec/3'
         AND NEW.reconstruction_profile = 'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1'
         AND s.series_profile = 'atm-series/bnr-bls-c01-calendar-quarter/1'
         AND s.admission_profile = 'atm-bnr-bls-canonical-panel-admission/1')
      )
)
BEGIN
    SELECT RAISE(ABORT, 'chart tuple update would invalidate existing chart series');
END;

CREATE TABLE installation_v5 (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL
        CHECK (schema_id = 'atm-conversation-store/5')
) STRICT;

INSERT INTO installation_v5(singleton_id, schema_id)
VALUES(1, 'atm-conversation-store/5');

DROP TABLE installation;
ALTER TABLE installation_v5 RENAME TO installation;
