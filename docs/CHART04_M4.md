# CHART-04: dormant M4 LINE reduction core

Status: dormant display-layer primitive, linked only into tests. It is not called
by the Cairo renderer, GTK component, Application, conversation store or History.

## Boundary

`atm-m4-line/1` consumes already projected finite geometry. It does not accept
scientific values, units, provenance or model-authored chart content and cannot
create an observation. The complete VerifiedSeries and ChartSpec remain the
scientific authority.

The current qualified complete-annual GISTEMP profile has 146 observations and
normally remains below the plot-width density threshold, so it stays on the full
render path when CHART-04 is later integrated. This slice does not widen the
existing profile to MISSING, breaks, STEP, BAR, additional sources or arbitrary
series.

## Selector

Reduction is admitted only when point count is greater than the supplied raster
column count. The caller supplies each point's pixel column using the exact
renderer mapping that has been separately qualified. The M4 core deliberately
does not choose floor/round/clamp semantics for continuous projected X. For every
occupied column the plan retains:

1. first source observation;
2. minimum projected Y;
3. maximum projected Y;
4. last source observation.

Role collisions are deduplicated and the retained observations are emitted in
original source order. Equal extrema retain the earlier observation. Empty columns
create no values. Projected X must be nondecreasing and source indexes strictly
increasing.

The plan has independent display-layer allocation ceilings of 1024 points and
8192 logical pixels, aligned with the current projection envelope. These are
resource bounds, not scientific coverage claims, and the primitive deliberately
does not depend on VerifiedSeries/ChartSpec headers. It is deterministic and
owns copied display geometry only.

## Qualification in this slice

Synthetic projected-geometry fixtures are contract tests, not scientific evidence.
They verify the non-dense identity path, first/min/max/last selection, role
deduplication, source order, global endpoints, positive/negative spikes, irregular
sparse pixel-column occupancy, determinism and fail-closed
arguments/order/finite/limit checks. The test runs under ASan/UBSan and in the
Flatpak Meson suite.

The selector is deliberately not connected to rendering yet. The next CHART-04
slice must bind it to qualified LINE projections, qualify the exact projected-X
to raster-column mapping against Cairo output, version the render-plan key and
compare full versus reduced Cairo output before M4 can affect pixels. Resize/cache
work follows only after that renderer gate.

No M4 output is persistence state. CHART-05 must reconstruct full qualified data
and recompute any display plan for the current viewport.
