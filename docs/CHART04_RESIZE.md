# CHART-04: resize characterization

Status: measurement harness only. No production behavior is changed by this
slice, and no performance threshold is used as a correctness gate.

## Question

The dormant GTK chart component currently rebuilds a complete renderer
synchronously from the DrawingArea resize callback. Source inspection shows that
a normal non-fallback render rebuilds ChartSpec once in `atm_chart_render_new`
and again inside `atm_chart_projection_new`. It also regenerates the exact Data
buffer although source data and provenance are invariant across viewport sizes.

Before changing ownership or adding caching, this harness measures the current
optimized native path on the same pinned GISTEMP fixture.

## Measurements

The dedicated `CHART-04 resize characterization` workflow compiles an `-O2`
binary and records median/p90 timing for:

- ChartSpec rebuild;
- projection construction at the current 860x500 chart plot size;
- small fallback render;
- normal 860x500 render at device scale 1;
- the same render at device scale 2;
- a deterministic 24-step resize sequence spanning admitted chart sizes.

The workflow uploads both timing output and runner CPU/kernel context. Timings are
characterization evidence, not portable performance promises. ASan/UBSan and
Flatpak remain separate correctness gates.

## Decision rule

Use the measurements together with the static ownership path to choose the
smallest safe refactor. The target invariants are already fixed:

- scientific/source qualification happens at component/session creation, not for
  every viewport size;
- resize invalidates only viewport-dependent projection/surface state;
- the exact Data buffer and qualified scientific identity do not change on resize;
- draw remains paint-only;
- at most the current prepared surface plus bounded pending state is retained;
- no persistent pixel/M4 cache is introduced.

Measured values and the selected implementation will be recorded after the first
qualified workflow run.
