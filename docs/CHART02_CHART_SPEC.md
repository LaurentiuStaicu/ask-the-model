# CHART-02: source-qualified ChartSpec contracts

*Historical design and initial v1 contract details follow; the current v1/v2 behavior is stated first.*

**Current status updated 2026-10-15:** the application admits three bounded source-specific routes: GISTEMP ChartSpec v1, Energy Institute TES v2, and BNR BLS C01 quarterly ChartSpec v3. The BNR route was integrated through PRs #471–#479 and uses the exact pinned RMD source bundle; live and History reconstruction requalify the source and preserve its one authorized `MISSING` coordinate at 2025-Q2 without assigning a numeric value or joining the line across the gap. The current routes are `/chart gistemp`, `/chart energy-institute` and `/chart bnr-bls-c01`, with narrow natural-language routing for their admitted source profiles. The generic catalog/intent proposal in draft PR #487 is not yet implemented: a generic request such as `population over time` must not be treated as an eligible chart merely because a repository file contains a column with that name. Live and History reconstruct from pinned source bytes; persisted recipes are not numeric authority. The model supplies neither scientific values nor units, axes, domains, transforms or chart metadata.

The implementation-stage notes below preserve the original design and qualification history. Statements that EI was `0-unqualified`, the renderer rejected EI, or no application caller existed refer to their original milestone gates and were superseded by C2c–C2e. Unsupported inputs still fail closed: no arbitrary source/series, model-authored values, STEP/BAR, general missing/event series, implicit conversions, smoothing or multi-source comparison UI is enabled.

## C2a: separate native Energy Institute ChartSpec v2

The native ChartSpec API now keeps the legacy GISTEMP v1 path and identity serializer separate from an EI v2 constructor. GISTEMP v1 continues to use schema `atm-chart-spec/1`, domain `atm.chart-spec.v1`, the same ordered qualified-series IDs and the same existing ownership/reconstruction checks. The new EI entry point accepts only `AtmEnergyInstituteVerifiedSeries`; it does not accept a generic `AtmSeriesContract`, point arrays, arbitrary unit metadata or identity strings.

EI v2 is a single-series, calendar-year TES contract only. Before owning input, the constructor independently rebuilds the typed source series, and therefore rechecks one of the two exact v3 EI source bundles and its source → Evidence → finalized SRA → series chain. Its new `atm-chart-spec/2` identity is separately namespaced and binds the kind, scientific/qualified series identities, profile and admission versions, repository snapshot, source path, semantic fields, exact-decimal extents, ordered series identity and point count. The qualified series ID binds the source-specific SRA identity and point support; ChartSpec does not treat stored points or source labels as authority.

The new source-neutral point view exposes calendar year, status, exact source-decimal token, canonical coefficient/exponent, discontinuity flag and borrowed support IDs for either admitted current source. EI uses EJ/year and energy-flow-rate dimension; it never passes through the GISTEMP temperature display conversion. Tests cover both admitted EI snapshots: the reconstructed SRA scientific fact content remains equivalent, but the generic series serialization includes each point's snapshot-qualified support IDs, so both series identities and the ChartSpec identities differ. Cross-snapshot mixes remain rejected by source admission.

**Historical C2a gate record:** at the C2a milestone, native ChartSpec v2 was not by itself a rendering qualification and its EI display marker was `0-unqualified`. C2b subsequently qualified display profile version `1`; C2c–C2e later integrated durable persistence, History reconstruction, and the application renderer. None of these development gates changes the immutable v0.6.3 release artifact.


The entry point consumes validated, owned or safely retained VerifiedSeries
objects, not identity strings or model-authored arrays. The first allowed
profile is `atm-series/gistemp-complete-annual/1`. The model may express intent
and select qualified references; it cannot supply numeric values, units, scale
policy or axis domains.

## C2b: deterministic Energy Institute TES display projection (historical gate record)

**C2b status, 2026-10-13: implemented, tested and integrated through PR #440.** At that stage the projection was native-only and not yet connected to the renderer/application; C2e later enabled the separately qualified EI presentation path. The existing `atm-display/gistemp-linear/1` policy and its pinned reference output remain unchanged. Energy Institute uses the separate `atm-display/energy-institute-tes-linear/1` policy; EI v2 binds its policy ID and version into the ChartSpec identity, with version `1` reserved for this separately qualified profile.

The source data remains the exact annual TES observations in EJ/year, physical-energy-content basis. The display-only conversion uses integer arithmetic and an explicit canonical-coefficient allowlist, accepts coefficient exponents `-7..0`, rounds half-up to micro-EJ/year, and rejects malformed coefficients, unsupported precision, overflow, negative values, or exact source values outside `0..1000` EJ/year. The source token, scientific coefficient/exponent, evidence support and exact extrema in ChartSpec are unchanged by projection. Quantized display micro-units must never replace source data or be used in calculations, transformations, SRA facts, export, or History.

For the currently admitted 1965–2025 series, the display Y domain is rounded outward to a 100-EJ/year grid (currently 100..700), while X remains 1965..2025. Year-tick spacing is deterministic from bounded plot width: 15 years below 480 logical pixels, 10 years from 480 to under 720, and 5 years at 720 or more. Both axes stay under the 17-tick cap; valid plot dimensions are 320..8192 by 160..8192. Every point must remain finite and within the projected domain. The calculation is repeatable across both exact admitted snapshots; the projected coordinates coincide because the observed values coincide, while the source-qualified ChartSpec IDs remain distinct.

The EI projection policy passed the native ASan/UBSan suite for decimal precision/range/overflow, both exact source snapshots, source-token/support preservation, axis ticks, normalized resize behavior, mutation detection and fail-closed outputs. GTK, resize, invariant registry and Flatpak/Meson also passed on the exact C2b PR head. The projection is not routed to `chart_render.c`: the existing renderer explicitly rejects EI v2 and remains restricted to the current GISTEMP v1 contract. No EI live chart intent, application rendering, History schema or persisted chart is enabled by C2b. C2c must qualify the atomic conversation-store v4 migration and C2d must qualify pinned-source History reconstruction before C2e can wire the EI chart into the application.

## Initial admission rules

| Input or policy | First implementation |
| --- | --- |
| Schema | `atm-chart-spec/1` |
| Chart kinds | Line and scatter |
| Steps | Reject until qualified state/event semantics exist |
| Bars | Reject until explicit zero-baseline policy is implemented and tested |
| Series count | Native API retains a defensive 1–4 input ceiling and rejects duplicates; the application routes one admitted source series at a time and does not expose generic multi-series comparison |
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
The preceding paragraph records the original native-only stage; later C2c–C2e
integrated the independently admitted EI v2 renderer and application route.

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

Current integration in `src/Application.vala` uses `ChartIntent` and `ChartNative` for three source-specific routes. GISTEMP uses schema-v1 recipes, Energy Institute TES uses schema-v2 recipes, and BNR BLS C01 uses ChartSpec v3 plus schema-v5 conversation persistence and `atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1`. PRs #471–#479 integrated the BNR live route and History reconstruction, with the exact pinned RMD bundle and a 12-coordinate quarter sequence (11 numeric points plus `MISSING` at 2025-Q2). The related exact-head checks include CHART-01 native ASan/UBSan, CHART-03 GTK, CHART-04 resize, CHART-05 persistence, PRES-09 Application E2E, Presentation Interaction, Invariant Registry and Flatpak/Meson. These bounded routes do not qualify arbitrary sources, generic multi-series charts, general missing/event/derived profiles or all possible coordinate/chart kinds; issue #343 remains open.
