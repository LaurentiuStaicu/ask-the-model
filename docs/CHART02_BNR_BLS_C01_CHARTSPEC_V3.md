# CHART-02 BNR BLS C01 ChartSpec v3

Status: the source-bound ChartSpec v3 layer was integrated by PR #472 and is consumed by the bounded BNR C01 renderer and application route on development `main`. This page isolates the specification contract and is not a release declaration.

## Scope and admission

The source-specific constructor accepts only the opaque BNR C01 VerifiedSeries from the exact RMD snapshot e43eb4e899f123d3b1aa0f6e20b7c8ed35434170. It reconstructs that object and rechecks the semantic tuple before creating an owned specification.

The only admitted profile is:
- repository: rmd, pinned snapshot e43eb4e899f123d3b1aa0f6e20b7c8ed35434170;
- source path: data/processed/bnr_bls_realised_rounds.csv;
- subject and attribute: non_financial_corporations / nfc_credit_standards;
- x coordinate: calendar_quarter, 12 ordered coordinates 2022-Q4 through 2025-Q3;
- y unit/dimension: percent / survey_net_percentage;
- semantics: empirical_quarterly_observations;
- one source-authorized missing coordinate: 2025-Q2, reason source_round_not_retained.

The v3 identity uses the independent namespace atm.chart-spec.v3 and serializes the source/profile tuple, source snapshot/path, scientific and qualified series identities, x/y semantics, display-profile marker, numeric point extent and quarter-label extent. Existing GISTEMP v1 and Energy Institute v2 identity serializers are unchanged.

## Coordinates and extents

AtmChartPointView.calendar_quarter is populated only for BNR C01 v3; calendar_year remains zero for this profile, avoiding a silent coercion of quarter labels into years. The legacy numeric x_min/x_max accessors retain ordered point ordinals (0..11) for this profile. The explicit atm_chart_spec_calendar_quarter_min/max accessors return the authoritative endpoint labels.

The exact source decimal, coefficient/exponent, ordered support and missing reason remain owned by the reconstructed series. No arbitrary points or identities can be supplied by a caller.

## Missing-value rules

Only quarter 2025-Q2 may be missing. It must carry no decimal or coefficient payload, have exponent zero, preserve the exact reason and five-file support set, and retain its position in the twelve-quarter sequence. The other eleven quarters must be numeric. This ChartSpec stage does not render the gap yet; it records the qualified missing-policy identity for the subsequent separately qualified projection and renderer.

No interpolation, zero imputation, smoothing, arbitrary gaps, generalized discontinuities, multi-series composition, derived-series capability or event-series capability is authorized.

## Validation and next gates

The source-specific constructor owns an independent reconstruction. Validation/rebuild recomputes the canonical identity and extents. Native tests cover source-bound identity stability, line/scatter identity separation, explicit quarter coordinates and extents, exact numeric source spellings, the one missing point and fail-closed tampering.

This ChartSpec component does not by itself implement the renderer, Data view, Application route or persistence. The separately qualified downstream layers are now integrated through PRs #473–#479. The route remains limited to the exact pinned C01 profile, and v0.8.0 is not a public release until final release and on-device acceptance gates pass.
