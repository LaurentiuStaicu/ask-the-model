# CHART-03 BNR BLS C01 quarterly renderer and Data view

Status: the gap-aware Cairo renderer and GTK Data view were integrated by PRs #474–#475 and are used by the bounded BNR C01 Application/History route on development `main`. This is unreleased development capability; the public v0.7.1 artifact is unchanged.

## Rendering policy

The renderer accepts BNR C01 only through ChartSpec v3 and its source-specific projection policy. It draws the twelve-quarter domain with eleven numeric observations. For a line chart it detects the non-consecutive projected source indices 9 and 11, strokes the first path, and starts a separate path at the latter point. It never joins a line across 2025-Q2 and never creates a marker for the missing quarter. Scatter mode draws only the eleven numeric observations.

The bounded chart labels the quantity as a net percentage for non-financial corporations, shows the period and the missing quarter explicitly, and includes the exact-source Data view direction. The source/profile checks and projection gate run before Cairo rendering. A narrow or short surface uses the existing explicit fallback rather than cropping labels.

## Data view

The machine-readable renderer TSV contains all twelve calendar quarters, with separate status, source decimal, canonical coefficient/exponent, source row, qualified series ID, support IDs, and missing reason columns. These technical fields remain in the internal payload for validation and reconstruction; they are not all rendered as visible table columns.

Numeric quarters are bound to physical rows of the pinned RMD CSV. The 2025-Q2 coordinate is retained in the internal payload with status MISSING, empty numeric payload, exponent zero, no physical source row, five-file source support, and reason source_round_not_retained. The visible Data table shows the quarter and MISSING status without inventing a value or physical-row entry; the selected-observation details explain that the source round was not retained and this is not zero. The internal zero sentinel is never exposed as an observation.

## Qualification boundary

Native Cairo tests assert that the 480×348 embedded logical viewport does not fall back, the exact TSV has twelve quarterly entries, the source snapshot/path and row identities are preserved, and the line-path midpoint across the missing quarter stays unpainted. Scatter mode also has eleven markers and no invented missing marker.

The renderer and GTK Data-view components are now wired into the source-pinned Application route and schema-v5 History reconstruction by PRs #476–#479. Their integration remains limited to this exact C01 profile; it does not authorize imported-conversation expansion, arbitrary sources, generalized gap semantics or M4.
