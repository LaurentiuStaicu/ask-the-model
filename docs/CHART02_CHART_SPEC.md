# CHART-02: initial ChartSpec contract

Status: the initial ChartSpec contract is now used by the narrow GISTEMP application path described below.

Current application behavior accepts only an explicit `/chart gistemp <grounded question>` request. It uses a grounded citation to the pinned EWD GISTEMP source, re-reads and re-qualifies that snapshot, and builds the ChartSpec used by the live and History chart views. The model does not supply chart values, axes, units, domains, or chart intent. Other adapters, arbitrary series, STEP, BAR, and missing/event data remain unsupported.

The implementation-stage notes below preserve the original design and qualification history; statements that there was no application caller describe those earlier stages, not the current branch.

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

## Bounded display projection

`chart_projection.h/.c` adds `atm-display/gistemp-linear/1`, a transient display
policy for the closed complete annual GISTEMP adapter. It validates the supplied
ChartSpec before reconstructing an owned copy. It does not admit arbitrary data,
change the ChartSpec identity or modify scientific extents. Coordinates are
binary64 display approximations; exact source decimals remain accessible through
the owned specification for the later Data view.

The policy selects 25-year X ticks, including both exact domain endpoints, and
quarter-degree Y ticks. Y display bounds are the enclosing multiples of 0.25 °C;
for the retained source this yields [-0.50, 1.50], while scientific bounds remain
[-0.49, 1.28]. These ticks are scale labels, not additional observations. Positive
Y goes upward; the returned plot coordinates have origin at the upper left.

Conversion first forms exact signed integers in units of 1e-4 °C, with bounded
coefficient/exponent arithmetic (exponent -4 through 0, absolute scaled integer
at most 100,000,000). Integer differences are then divided to obtain display
coordinates. No locale-sensitive decimal parsing, nonfinite result, clamping,
interpolation or resampling is allowed. This narrow conversion policy is not a
generic arbitrary-precision projection claim; future adapters require new gates.

The factory accepts finite plot-area dimensions from 320×160 through 8192×8192
logical units, at most 17 ticks per axis, and a bounded point array. Dimensions
exclude text margins. Quarter-degree coverage exceeding 17 ticks is rejected;
there is no implicit change of scale. Tick labels use exact integer formatting,
an ASCII decimal point and no negative zero. Small-window fallback, font metrics,
label collision management, clipping and physical pixel scaling belong to the
renderer, which is not enabled here. The minimum size is a resource/API policy,
not visual layout qualification. General missing and event series remain rejected
upstream; LINE and SCATTER preserve every admitted point and its source index.

The reference fixture `projection-reference.json` was independently computed
from every retained CSV observation using Python Decimal at plot size 580×160:
`x=(year-1880)*4`, `y=(1.50-source_decimal)*80`. Native tests compare all 146
coordinates within 1e-10 logical units, all tick labels/positions, normalized
geometry across minimum/fractional/maximum dimensions, exact identity preservation,
input lifetime, invalid dimensions, altered specifications and tampered derived
geometry. Validation reconstructs domains, labels and coordinates from the owned
specification. No cached display data is scientific authority.

Current integration is implemented in `src/Application.vala` through `ChartIntent`, `ChartNative`, and the conversation persistence chart recipe. CHART-01/03, the PRES-09 close/reopen reconstruction test, and the Flatpak suite provide the current qualification evidence. The separate CHART-01 source-audit JSON remains a record of its original source-audit stage; its historical `runtime_adapter_enabled: false` field is not the current application capability status.
