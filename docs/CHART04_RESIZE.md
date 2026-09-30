# CHART-04: resize characterization and qualified viewport state

Status: dormant chart/GTK optimization. No Application, Conversations, History or
persistence caller is enabled by this slice.

## Baseline characterization

PR #361 recorded the current optimized (-O2) GISTEMP LINE path on an Ubuntu
24.04 GitHub runner (Linux 6.17 Azure, 4 vCPU AMD EPYC 7763). The retained
artifact reported:

- ChartSpec rebuild: median 285511 us, p90 291299 us;
- public projection construction: median 283878 us, p90 284828 us;
- fallback 240x140 render: median 285738 us;
- normal 860x500 render: median 567233 us;
- 2x 860x500 render: median 568501 us;
- 24-step repeated-new-render resize sequence: median 13700663 us,
  approximately 570861 us per step.

These are characterization measurements, not portable performance promises. They
show that the dominant resize cost is repeated scientific/source reconstruction:
the renderer rebuilt ChartSpec, and public projection construction rebuilt it
again. Exact Data was also regenerated despite being invariant across viewport
sizes.

## Selected ownership boundary

The public trust boundary remains unchanged. atm_chart_projection_new() still
rebuilds and validates its supplied ChartSpec before owning a projection.

A separate hidden/internal projection entry may borrow only a ChartSpec already
reconstructed and owned by AtmChartRender. That borrowed specification must
outlive the projection and is never exposed as an Application-facing bypass.

atm_chart_render_new() reconstructs the supplied ChartSpec once, builds exact
Data once, and then prepares viewport state from that owned qualified spec.

atm_chart_render_resize() is viewport-only. It prepares a new projection and
Cairo surface in temporary state, using the renderer-owned qualified spec. Only
after successful preparation are the old projection/surface replaced. Failure
leaves scientific identity, Data and the previous valid viewport untouched.

This is not a scientific cache: there is one renderer-owned qualified
specification and one current viewport projection/surface.

## GTK resize policy

GtkDrawingArea resize/device-scale notifications record only the latest requested
width/height/scale. At most one default-idle GSource is pending. A burst of resize
events therefore performs one viewport update for the latest request.

The widget stores the GSource object itself, not only a numeric source ID. Dispose
destroys and unreferences pending work before disconnecting the DrawingArea. The
draw callback remains paint-only.

Unsupported transient dimensions/scales keep the last valid render and exact Data.
A later valid request replaces viewport state once. The existing real X11/Xvfb
test continues to exercise actual window resize, keyboard navigation and visual
snapshot generation.

## Qualification

Native tests require:

- public projection validation/reconstruction behavior remains intact;
- renderer resize preserves the same owned ChartSpec and exact Data;
- valid viewport resize updates dimensions/device scale;
- invalid resize leaves the previous surface byte-identical;
- fallback resize remains available without regenerating Data;
- three queued GTK resize requests coalesce into one apply operation and the
  latest request wins;
- unsupported GTK requests retain the last valid viewport;
- a pending GSource is cancelled when the widget is destroyed;
- test-only GTK resize hooks are absent from normal compilation;
- all existing chart, Scientific Plane, GTK, Flatpak and invariant gates stay
  green.

The characterization workflow now retains both the repeated-new-render sequence
and a true 24-step atm_chart_render_resize() sequence. This provides direct
before/after evidence without turning timing into a correctness threshold.

## Boundaries

M4 remains dormant under its separate Cairo equality qualification. No shared or
unbounded pixel cache is added. No viewport/M4 state is persisted. CHART-05 must
persist/reconstruct complete qualified scientific state and derive current
viewport state again.

References:
- https://docs.gtk.org/gtk4/signal.DrawingArea.resize.html
- https://docs.gtk.org/gtk4/method.DrawingArea.set_draw_func.html
- https://docs.gtk.org/glib/func.idle_add_full.html
