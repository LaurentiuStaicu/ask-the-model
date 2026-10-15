# CHART-02 BNR BLS C01 quarterly display projection

Status: the bounded projection policy was integrated by PR #473 and is consumed by the gap-aware renderer and Application route on development `main`. This page documents only the projection layer; it does not declare the feature released.

## Admission

The projector accepts only ChartSpec v3 for the source-specific BNR C01 profile at RMD snapshot e43eb4e899f123d3b1aa0f6e20b7c8ed35434170. It rechecks repository, snapshot, profile, quarter endpoints, ordered point indices, unit/dimension, support, exact source row mapping, and the sole authorized missing observation.

## Bounded axis policy

- X domain is the exact ordinal range 0..11. Each projected point retains its source point index, so quarter 2025-Q2 keeps its X position even though no numeric point is created.
- Y domain is fixed at 0..20 percent with ticks 20, 15, 10, 5, 0.
- X labels use a bounded six-tick set: Q4 2022, Q2 2023, Q4 2023, Q2 2024, Q4 2024, and Q3 2025.
- Display conversion uses bounded integer decimal scaling before conversion to plot coordinates. Precision outside two decimal places, values outside 0..20 percent, malformed decimals, unsupported rows, non-finite coordinates, or changed source/profile metadata fail closed.
- Exactly eleven points are projected. The point indices are 0 through 9 and 11; there is no projected value at index 10 / 2025-Q2.

## Gap contract

The missing quarter must remain source-authorized with reason source_round_not_retained, no numeric payload, exponent zero, internal source-row sentinel zero, and the five-file support set. Its X ordinal remains between 2025-Q1 and 2025-Q3. The integrated line renderer starts a new path after the non-consecutive projected source indices 9 and 11 instead of connecting through the missing quarter. Scatter rendering shows only the eleven numeric points and does not invent a marker for the absence.

## Validation and non-scope

Native tests check the bounded display domain, exact tick policy, all 11 projected source indices, the two-quarter X separation around the gap, resizing-independent normalized coordinates via projection validation, and fail-closed projection tampering.

This projection policy remains specific to the pinned BNR C01 profile. The separately qualified renderer, Data view, Application route and History layers are integrated on development `main`; this does not authorize arbitrary sources, generalized missing/discontinuity semantics, smoothing, interpolation, derived or multi-series charts. M4 stays dormant.
