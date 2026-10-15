# CHART-03 BNR BLS C01 quarterly renderer and Data view

Status: the renderer and GTK Data-view qualification described below is part of an open, stacked review candidate. Later draft PRs #476–#479 add recipe persistence, History reconstruction, the Application intent route, and follow-up intent/provenance corrections. The full stack is not integrated into `main`, and this document is not a release declaration.

## Rendering policy

The renderer accepts BNR C01 only through ChartSpec v3 and its source-specific projection policy. It draws the twelve-quarter domain with eleven numeric observations. For a line chart it detects the non-consecutive projected source indices 9 and 11, strokes the first path, and starts a separate path at the latter point. It never joins a line across 2025-Q2 and never creates a marker for the missing quarter. Scatter mode draws only the eleven numeric observations.

The bounded chart labels the quantity as a net percentage for non-financial corporations, shows the period and the missing quarter explicitly, and includes the exact-source Data view direction. The source/profile checks and projection gate run before Cairo rendering. A narrow or short surface uses the existing explicit fallback rather than cropping labels.

## Data view

The renderer's TSV contains all twelve calendar quarters, with separate status, source decimal, canonical coefficient/exponent, source row, qualified series ID, support IDs, and missing reason columns.

Numeric quarters are bound to the physical rows of the pinned RMD CSV. The 2025-Q2 row is retained as a Data view entry with status MISSING, empty numeric payload, exponent zero, the five-file source support, and reason `source_round_not_retained`. Its internal typed source-row sentinel is zero because no physical CSV row exists; the visible Data TSV renders that source-row field as `none` so it cannot be mistaken for a physical row identifier. The table does not fabricate a CSV row or silently replace the absence with zero.

## Qualification boundary

Native Cairo tests assert that the 480×348 embedded logical viewport does not fall back, the exact TSV has twelve quarterly entries, the source snapshot/path and numeric row identities are preserved, and the line-path midpoint across the missing quarter stays unpainted. Scatter mode also has eleven markers and no invented missing marker. The missing quarter retains its explicit source reason, no numeric value, and no physical row.

The renderer/Data-view contract alone does not authorize a released Application or History route. The stacked PR chain separately implements those gates and must be reviewed and integrated in dependency order. Its live composer, recipe persistence, History reconstruction, source-tampering failure behavior, and intent corrections remain review-candidate behavior until that integration and exact-artifact acceptance are complete.
