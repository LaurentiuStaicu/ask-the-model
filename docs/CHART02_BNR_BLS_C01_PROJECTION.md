# CHART-02 BNR BLS C01 quarterly display projection

Status: native projection policy in a review candidate. It is not yet called by Cairo rendering, Data view, Application intent handling, or History.

## Admission

The projector accepts only ChartSpec v3 for the source-specific BNR C01 profile at RMD snapshot e43eb4e899f123d3b1aa0f6e20b7c8ed35434170. It rechecks repository, snapshot, profile, quarter endpoints, ordered point indices, unit/dimension, support, exact source row mapping, and the sole authorized missing observation.

## Bounded axis policy

- X domain is the exact ordinal range 0..11. Each projected point retains its source point index, so quarter 2025-Q2 keeps its X position even though no numeric point is created.
- Y domain is fixed at 0..20 percent with ticks 20, 15, 10, 5, 0.
- X labels use a bounded six-tick set: Q4 2022, Q2 2023, Q4 2023, Q2 2024, Q4 2024, and Q3 2025.
- Display conversion uses bounded integer decimal scaling before conversion to plot coordinates. Precision outside two decimal places, values outside 0..20 percent, malformed decimals, unsupported rows, non-finite coordinates, or changed source/profile metadata fail closed.
- Exactly eleven points are projected. The point indices are 0 through 9 and 11; there is no projected value at index 10 / 2025-Q2.

## Gap contract

The missing quarter must remain source-authorized with reason source_round_not_retained, no numeric payload, exponent zero, source row zero, and the five-file support set. Its X ordinal remains between 2025-Q1 and 2025-Q3. A later line renderer must start a new path after a non-consecutive point index instead of connecting through the missing quarter. Scatter rendering may show the numeric points, but must not invent a marker for the absence.

## Validation and non-scope

Native tests check the bounded display domain, exact tick policy, all 11 projected source indices, the two-quarter X separation around the gap, resizing-independent normalized coordinates via projection validation, and fail-closed projection tampering.

This policy does not authorize arbitrary gaps, arbitrary source series, smoothing, interpolation, missing-value imputation, derived series, multi-series display, rendering, Data view, an Application route, or History persistence. M4 stays dormant.
