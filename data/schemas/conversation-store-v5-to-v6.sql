-- Called inside BEGIN IMMEDIATE by the native migration coordinator.
-- Preserve source ChartSpec v1/v2/v3 rows/IDs; add only the presentation marker
-- and sidecar, and advance the store schema in the native transaction wrapper.

CREATE TABLE message_charts_v6 (
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
    presentation_schema TEXT
        CHECK (presentation_schema IS NULL OR presentation_schema = 'atm-chart-spec/4'),
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

CREATE TABLE chart_series_v6 (
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
        REFERENCES message_charts_v6(message_id, ordinal)
        ON DELETE CASCADE
) STRICT;

INSERT INTO message_charts_v6(
    message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile,presentation_schema
)
SELECT message_id,ordinal,chart_schema,chart_spec_id,chart_kind,reconstruction_profile,NULL
FROM message_charts;

INSERT INTO chart_series_v6(
    message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,
    scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path
)
SELECT message_id,chart_ordinal,series_ordinal,series_profile,admission_profile,
       scientific_id,qualified_id,repository_id,repository_version,snapshot_sha,source_path
FROM chart_series;

DROP TABLE chart_series;
DROP TABLE message_charts;
ALTER TABLE message_charts_v6 RENAME TO message_charts;
ALTER TABLE chart_series_v6 RENAME TO chart_series;

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

-- Optional v4 presentation recipe. Source identity and source pin remain in
-- message_charts/chart_series; this sidecar contains only claims to revalidate.
CREATE TABLE chart_presentations_v4 (
    message_id TEXT NOT NULL,
    chart_ordinal INTEGER NOT NULL CHECK (chart_ordinal BETWEEN 1 AND 4),
    presentation_schema TEXT NOT NULL CHECK (presentation_schema = 'atm-chart-spec/4'),
    presentation_spec_id TEXT NOT NULL CHECK (
        length(presentation_spec_id) = 64 AND
        presentation_spec_id NOT GLOB '*[^0-9a-f]*'
    ),
    series_id TEXT NOT NULL CHECK (series_id IN (
        'climate.gistemp.global-temperature-anomaly',
        'energy.energy-institute.global-total-energy-supply',
        'banking.bnr.bls-c01.credit-standards-net-percentage'
    )),
    catalog_schema TEXT NOT NULL CHECK (catalog_schema = 'atm-chart-catalog/1'),
    adapter_id TEXT NOT NULL CHECK (adapter_id IN (
        'gistemp-pinned-annual/1',
        'energy-institute-tes-pinned-annual/1',
        'bnr-bls-c01-pinned-quarterly/1'
    )),
    series_profile_id TEXT NOT NULL CHECK (series_profile_id IN (
        'atm-series/gistemp-complete-annual/1',
        'atm-series/energy-institute-tes-complete-annual/1',
        'atm-series/bnr-bls-c01-calendar-quarter/1'
    )),
    series_profile_version TEXT NOT NULL CHECK (series_profile_version = '1'),
    admission_profile_id TEXT NOT NULL CHECK (admission_profile_id IN (
        'atm-gistemp-pinned-admission/1',
        'atm-energy-institute-pinned-admission/3',
        'atm-bnr-bls-canonical-panel-admission/1'
    )),
    admission_profile_version TEXT NOT NULL CHECK (admission_profile_version IN ('1', '3')),
    reconstruction_profile_id TEXT NOT NULL CHECK (reconstruction_profile_id IN (
        'atm-chart-reconstruct/gistemp-complete-annual/1',
        'atm-chart-reconstruct/energy-institute-tes-complete-annual/1',
        'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1'
    )),
    source_chart_schema TEXT NOT NULL CHECK (source_chart_schema IN (
        'atm-chart-spec/1', 'atm-chart-spec/2', 'atm-chart-spec/3'
    )),
    source_chart_spec_id TEXT NOT NULL CHECK (
        length(source_chart_spec_id) = 64 AND
        source_chart_spec_id NOT GLOB '*[^0-9a-f]*'
    ),
    x_coordinate_semantics TEXT NOT NULL CHECK (x_coordinate_semantics IN (
        'calendar_year', 'calendar_quarter'
    )),
    x_frequency TEXT NOT NULL CHECK (x_frequency IN ('annual', 'quarterly')),
    x_unit TEXT NOT NULL CHECK (x_unit IN ('calendar_year', 'calendar_quarter')),
    x_dimension TEXT NOT NULL CHECK (x_dimension = 'time'),
    subject TEXT NOT NULL CHECK (subject IN (
        'global_surface_temperature', 'global_total_energy_supply', 'non_financial_corporations'
    )),
    attribute TEXT NOT NULL CHECK (attribute IN (
        'annual_temperature_anomaly', 'total_energy_supply', 'nfc_credit_standards'
    )),
    y_unit TEXT NOT NULL CHECK (y_unit IN ('degree_Celsius_anomaly', 'exajoules/year', 'percent')),
    y_dimension TEXT NOT NULL CHECK (y_dimension IN (
        'temperature_difference', 'energy_flow_rate', 'survey_net_percentage'
    )),
    series_semantics TEXT NOT NULL CHECK (series_semantics IN (
        'empirical_annual_observations', 'empirical_quarterly_observations'
    )),
    missing_policy TEXT NOT NULL CHECK (missing_policy IN ('reject', 'bounded_profile')),
    missing_coordinate TEXT,
    missing_reason TEXT,
    display_profile_id TEXT NOT NULL CHECK (display_profile_id IN (
        'atm-display/gistemp-linear',
        'atm-display/energy-institute-tes-linear',
        'atm-display/bnr-bls-c01-quarterly-gap'
    )),
    display_profile_version TEXT NOT NULL CHECK (display_profile_version = '1'),
    render_contract TEXT NOT NULL CHECK (render_contract = 'atm-retro-mono/1'),
    CHECK (
        (series_id = 'climate.gistemp.global-temperature-anomaly' AND
         adapter_id = 'gistemp-pinned-annual/1' AND
         series_profile_id = 'atm-series/gistemp-complete-annual/1' AND
         admission_profile_id = 'atm-gistemp-pinned-admission/1' AND
         admission_profile_version = '1' AND
         reconstruction_profile_id = 'atm-chart-reconstruct/gistemp-complete-annual/1' AND
         source_chart_schema = 'atm-chart-spec/1' AND
         x_coordinate_semantics = 'calendar_year' AND x_frequency = 'annual' AND
         x_unit = 'calendar_year' AND subject = 'global_surface_temperature' AND
         attribute = 'annual_temperature_anomaly' AND y_unit = 'degree_Celsius_anomaly' AND
         y_dimension = 'temperature_difference' AND series_semantics = 'empirical_annual_observations' AND
         missing_policy = 'reject' AND missing_coordinate IS NULL AND missing_reason IS NULL AND
         display_profile_id = 'atm-display/gistemp-linear') OR
        (series_id = 'energy.energy-institute.global-total-energy-supply' AND
         adapter_id = 'energy-institute-tes-pinned-annual/1' AND
         series_profile_id = 'atm-series/energy-institute-tes-complete-annual/1' AND
         admission_profile_id = 'atm-energy-institute-pinned-admission/3' AND
         admission_profile_version = '3' AND
         reconstruction_profile_id = 'atm-chart-reconstruct/energy-institute-tes-complete-annual/1' AND
         source_chart_schema = 'atm-chart-spec/2' AND
         x_coordinate_semantics = 'calendar_year' AND x_frequency = 'annual' AND
         x_unit = 'calendar_year' AND subject = 'global_total_energy_supply' AND
         attribute = 'total_energy_supply' AND y_unit = 'exajoules/year' AND
         y_dimension = 'energy_flow_rate' AND series_semantics = 'empirical_annual_observations' AND
         missing_policy = 'reject' AND missing_coordinate IS NULL AND missing_reason IS NULL AND
         display_profile_id = 'atm-display/energy-institute-tes-linear') OR
        (series_id = 'banking.bnr.bls-c01.credit-standards-net-percentage' AND
         adapter_id = 'bnr-bls-c01-pinned-quarterly/1' AND
         series_profile_id = 'atm-series/bnr-bls-c01-calendar-quarter/1' AND
         admission_profile_id = 'atm-bnr-bls-canonical-panel-admission/1' AND
         admission_profile_version = '1' AND
         reconstruction_profile_id = 'atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1' AND
         source_chart_schema = 'atm-chart-spec/3' AND
         x_coordinate_semantics = 'calendar_quarter' AND x_frequency = 'quarterly' AND
         x_unit = 'calendar_quarter' AND subject = 'non_financial_corporations' AND
         attribute = 'nfc_credit_standards' AND y_unit = 'percent' AND
         y_dimension = 'survey_net_percentage' AND series_semantics = 'empirical_quarterly_observations' AND
         missing_policy = 'bounded_profile' AND missing_coordinate = '2025-Q2' AND
         missing_reason = 'source_round_not_retained' AND
         display_profile_id = 'atm-display/bnr-bls-c01-quarterly-gap')
    ),
    PRIMARY KEY (message_id, chart_ordinal),
    FOREIGN KEY (message_id, chart_ordinal)
        REFERENCES message_charts(message_id, ordinal) ON DELETE CASCADE
) STRICT;

CREATE INDEX idx_chart_presentations_series_id ON chart_presentations_v4(series_id);

CREATE TRIGGER chart_presentations_v4_tuple_insert
BEFORE INSERT ON chart_presentations_v4
FOR EACH ROW
WHEN NOT EXISTS (
    SELECT 1
    FROM message_charts c
    JOIN chart_series s ON s.message_id=c.message_id AND s.chart_ordinal=c.ordinal
    WHERE c.message_id=NEW.message_id AND c.ordinal=NEW.chart_ordinal
      AND c.presentation_schema=NEW.presentation_schema
      AND c.chart_schema=NEW.source_chart_schema
      AND c.chart_spec_id=NEW.source_chart_spec_id
      AND c.reconstruction_profile=NEW.reconstruction_profile_id
      AND s.series_ordinal=0
      AND s.series_profile=NEW.series_profile_id
      AND s.admission_profile=NEW.admission_profile_id
)
BEGIN
    SELECT RAISE(ABORT, 'v4 presentation identity does not match its source chart recipe');
END;

CREATE TRIGGER chart_presentations_v4_tuple_update
BEFORE UPDATE ON chart_presentations_v4
FOR EACH ROW
WHEN NOT EXISTS (
    SELECT 1
    FROM message_charts c
    JOIN chart_series s ON s.message_id=c.message_id AND s.chart_ordinal=c.ordinal
    WHERE c.message_id=NEW.message_id AND c.ordinal=NEW.chart_ordinal
      AND c.presentation_schema=NEW.presentation_schema
      AND c.chart_schema=NEW.source_chart_schema
      AND c.chart_spec_id=NEW.source_chart_spec_id
      AND c.reconstruction_profile=NEW.reconstruction_profile_id
      AND s.series_ordinal=0
      AND s.series_profile=NEW.series_profile_id
      AND s.admission_profile=NEW.admission_profile_id
)
BEGIN
    SELECT RAISE(ABORT, 'v4 presentation identity does not match its source chart recipe');
END;

CREATE TRIGGER chart_presentations_v4_delete_guard
BEFORE DELETE ON chart_presentations_v4
FOR EACH ROW
WHEN EXISTS (
    SELECT 1 FROM message_charts c
    WHERE c.message_id=OLD.message_id
      AND c.ordinal=OLD.chart_ordinal
)
BEGIN
    SELECT RAISE(ABORT, 'cannot delete a v4 presentation while its source chart exists');
END;

CREATE TRIGGER message_charts_presentation_update
BEFORE UPDATE ON message_charts
FOR EACH ROW
WHEN EXISTS (
    SELECT 1 FROM chart_presentations_v4 p
    WHERE p.message_id=OLD.message_id AND p.chart_ordinal=OLD.ordinal
      AND (NEW.presentation_schema IS NOT p.presentation_schema OR
           NEW.chart_schema IS NOT p.source_chart_schema OR
           NEW.chart_spec_id IS NOT p.source_chart_spec_id OR
           NEW.reconstruction_profile IS NOT p.reconstruction_profile_id)
)
BEGIN
    SELECT RAISE(ABORT, 'source chart update would invalidate its v4 presentation recipe');
END;

CREATE TABLE installation_v6 (
    singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
    schema_id TEXT NOT NULL CHECK (schema_id = 'atm-conversation-store/6')
) STRICT;

INSERT INTO installation_v6(singleton_id, schema_id)
VALUES(1, 'atm-conversation-store/6');

DROP TABLE installation;
ALTER TABLE installation_v6 RENAME TO installation;
