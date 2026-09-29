# CHART-02: initial ChartSpec contract

Status: implementation plan after the scoped complete-annual gate in CHART-01.
No ChartSpec runtime or renderer is enabled by this document.

The entry point consumes validated, owned or safely retained VerifiedSeries
objects, not identity strings or model-authored arrays. The first allowed
profile is `atm-series/gistemp-complete-annual/1`. The model may express intent
and select qualified references; it cannot supply numeric values, units, scale
policy or axis domains.

## Initial admission rules

| Input or policy | First implementation |
| --- | --- |
| Schema | `atm-chart-spec/1` |
| Chart kinds | Line and scatter |
| Steps | Reject until qualified state/event semantics exist |
| Bars | Reject until explicit zero-baseline policy is implemented and tested |
| Series count | 1–4 distinct qualified series; reject duplicates; currently only one unique source exists |
| Series compatibility | Exact compatible units, dimensions, reference periods and coordinate/observation semantics |
| X coordinate | Calendar year, preserving source order |
| Axes | Linear only; exact data-derived extents |
| Missing policy | GAP; current complete-series profile rejects MISSING/break input before ChartSpec |
| Unsupported transformations | No ad-hoc conversions, dual/log axes, interpolation, extrapolation or smoothing |
| Authority | Revalidate every series and reconstruct ChartSpec identity; IDs alone do not qualify data |

The four-series ceiling is defensive policy, not evidence that four distinct
series have been scientifically qualified. With only one admitted source,
duplicate rejection intentionally prevents manufacturing a multi-series chart
by repeating it. A second source needs its own qualification and compatibility
tests before successful multi-series behavior can be claimed.

Line segments will represent visual connections between adjacent qualified
observations; they must not create new scientific data points. General missing
observations and discontinuities need a separate qualified adapter and tests.
The GAP policy does not grant permission to bypass the complete-source validator.

## Implementation order and unresolved display policy

1. Bounded intent and ownership API, series validation, kind/count/duplicate
   rejection and canonical ChartSpec identity.
2. Exact-decimal extrema and linear-axis contract. Define and test degenerate
   range handling before accepting constant/empty ranges; do not invent values
   or silently apply floating-point padding.
3. Reconstruction/tamper, incompatible-input and resource-boundary tests.
4. Hand off the qualified contract to CHART-03's Cairo renderer and Data view.

Scientific decimals remain exact. Binary64 display projection, tick selection,
visual padding and layout belong to separately tested display policy. The
future renderer uses `atm-retro-mono/1`; exact colors, widths and spacing remain
subject to visual prototype review. CHART-04 performance and CHART-05 History
persistence remain separate stages.

## First native implementation

`chart_spec.h/.c` provides an owned, source-revalidated specification for LINE
and SCATTER only. It accepts one to four inputs, rejects duplicate qualified
identities, and currently admits only the closed complete GISTEMP profile.
There is still only one unique qualified series; successful multi-series
compatibility is not claimed. STEP, BAR, missing/event data and other adapters
remain unsupported.

Exact coefficient/exponent comparisons derive the data extents without floating
point conversion. The retained source gives x=[1880,2025], y=[-49e-2,128e-2].
Empty or constant domains are rejected; ticks, display padding and binary64
projection are deferred to rendering. Linear axes and GAP policy are fixed in
the canonical ChartSpec identity alongside ordered qualified series IDs.
Validation reconstructs the specification and compares both domains and identity.
Input mutation cannot alter the owned series; corrupted materializations are
rejected before source rebuilding. Public pointers are borrowed read-only.

The native sanitizer and Meson tests cover deterministic identity, distinct kinds,
exact source extrema, input lifetime, duplicates, cardinality, unsupported kinds,
corrupted input, changed bounds/identity and changed owned series. Degenerate and
multi-source synthetic fixtures are not represented as qualified source data.
The module remains test-only, with no application caller or enabled chart UI.
