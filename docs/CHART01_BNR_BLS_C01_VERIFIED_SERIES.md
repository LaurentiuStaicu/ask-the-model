# CHART-01 BNR BLS C01 VerifiedSeries

Status: implemented in a review candidate as a native, test-only qualification layer. It is not an application chart route and does not authorize ChartSpec, display projection, rendering, History, or release claims.

## Admitted profile

The wrapper accepts only the source-bound AtmBnrBlsSeriesContract reconstructed from the five-file RMD panel at snapshot e43eb4e899f123d3b1aa0f6e20b7c8ed35434170. The accepted semantic tuple is:

- subject: non_financial_corporations
- attribute: nfc_credit_standards
- x coordinate: calendar_quarter
- y unit and dimension: percent / survey_net_percentage
- semantics: empirical_quarterly_observations
- support interval: 2022-Q4..2025-Q3
- observation count: 12 calendar quarters, exactly ordered.

The wrapper has no arbitrary point constructor and takes no caller-supplied identity. It independently rebuilds and validates the underlying panel, finalized SRA and SeriesContract before retaining its own object.

## Missing observation

The only admitted absence is 2025-Q2, with the source-authorized reason source_round_not_retained. Its y payload remains absent: no decimal or coefficient is permitted, and the five source authorities remain in its support. The wrapper rejects a different missing coordinate, a numeric payload attached to the missing point, unexpected missing values, breaks, out-of-order quarters, or changes to the source/profile tuple.

This layer does not decide rendering policy. A later ChartSpec/display qualification must define the visual break across the absent quarter and keep the gap explicit in Data view and History.

## Tests and boundary

The Meson target and the CHART-01 native workflow compile and execute this module under AddressSanitizer and UndefinedBehaviorSanitizer. Tests cover the exact quarter order and numeric source spelling, the authorized absence, independent ownership/rebuild, and fail-closed missing-payload tampering.

The broader CHART-01 qualification in issue #343 remains open. No arbitrary-source, generalized missing/discontinuity, multi-series, derived-series, or event-series capability is implied.
