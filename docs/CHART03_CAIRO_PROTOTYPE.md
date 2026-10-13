# CHART-03: headless monochrome prototype

**Status (2026-10-13):** the native Cairo renderer and GTK Chart/Data component now serve two explicitly qualified application paths: GISTEMP ChartSpec v1 and Energy Institute ChartSpec v2, integrated through C2e/PR #443. The prototype and component qualification history remains below; historic GISTEMP-only constraints still describe that source-specific path, not the whole current application.

## Current application integration

The application supports the explicit `/chart gistemp <grounded question>` and `/chart energy-institute <grounded question>` commands, plus natural-language visualization requests only when they identify exactly one supported source. Each route resolves eligible grounding against the selected pinned EWD snapshot, re-runs the source-specific admission and ChartSpec reconstruction, and then creates the source-specific GTK Chart/Data view. The saved conversation contains a reconstruction recipe, never authoritative numeric points; History re-reads the same pinned bundle and fails closed on source/profile/identity mismatches. The model does not author chart values, units, axes, domains or source metadata.

C2e is integrated through PR #443. Native/ASan/UBSan, GTK, resize, persistence, invariant-registry and Flatpak workflows all passed on its exact candidate head; post-merge Flatpak and Invariant checks passed too. However, there is no dedicated end-to-end GTK Application test that drives an EI request through archiving, close/reopen and restored History presentation. Component/native tests qualify their own layers and must not be represented as that full UI test.

## Contract and scientific boundary

`atm_chart_render_new` revalidates the supplied ChartSpec before owning a rebuilt
copy. It consumes the CHART-02 display projection for the closed, complete annual
GISTEMP profile. LINE connects adjacent observations without adding observations;
SCATTER draws every observation as a square. Arbitrary units, captions, values,
transforms, missing/event series, STEP and BAR remain outside this admission path.

An owned plain-text Data view buffer contains all 146 rows in source order, the
unchanged source decimal spelling, canonical coefficient/exponent, source row,
qualified series identity and five support IDs per row. Its metadata records the
unit, reference period, pinned snapshot/path and vintage. These fixed captions and
provenance fields apply only to the closed source profile; a new adapter requires
new qualified presentation metadata. The table contains no pixel-derived values.
The later GTK widget must use plain text, and retain access to this exact data
when drawing is unavailable. A TSV export is emitted by the preview test harness.

## Prototype visual policy

`atm-retro-mono/1`: opaque white canvas, black ink, monospace text, no raster
antialiasing, gradients, shadows, glow, scanlines, flicker, animation or curve
smoothing. Solid observation lines and square scatter markers are implemented.
Multiple-series dash distinctions are deferred because there is only one unique
qualified series. Horizontal dotted guides and a rectangular frame expose scale.
The title identifies the global annual temperature anomaly, reference period,
degrees Celsius, empirical observations and source vintage. Display bounds/ticks
remain CHART-02 policy; exact data extents remain untouched.

Coordinates use a bounded plot clip with a three-unit marker allowance to retain
endpoint symbols. Labels are outside the clip. X tick ink bounds and gaps, Y tick
widths and vertical separation, and header/footer widths are measured before a
chart is accepted. A compact canvas or incompatible font metrics produces an
explicit space fallback while keeping the complete Data view. This is not yet a
responsive GTK layout or translated/complex-script text renderer.

Logical canvas bounds are 240..2048 by 140..2048; device scale is the integer 1 or
2. Multiplication is bounded before creating an ARGB32 surface (at most 4096²
pixels / 64 MiB pixel storage). Cairo status is checked after allocation/drawing;
errors never yield a partially accepted image. Font-family resolution remains
platform dependent, so cross-platform pixel identity is not claimed. Cairo's
simple text API is used only for the fixed prototype labels. Application text
integration and accessibility will need the established GTK/Pango conventions.

## Qualification and visual review

The native test covers repeat-render equality in one environment, source lifetime
isolation, opaque black/white pixels, exact correspondence of all Data rows and
support IDs, all scatter markers including endpoints, 1×/2× device scale, explicit
small-canvas fallback, invalid dimensions/output and corrupt input even when the
canvas is too small. Cairo/Fontconfig process caches are released after all test
objects are destroyed; sanitizer leak detection remains on without suppressions.

CI produces line, scatter and small-canvas PNGs plus the complete Data TSV using
the native renderer itself. `chart03_preview_log.py` emits bounded, checksummed
base64 diagnostic copies for text-only review clients; Actions also retains the
original files as a preview artifact. Neither transport is application code.
Visual inspection of these exact native PNGs is required before accepting the
prototype's layout. User visual review precedes application activation.

The component is integrated into the application for the source-specific GISTEMP v1 and Energy Institute v2 paths. C2c's conversation-store v4, C2d's source-bound EI History reconstruction and C2e's application dispatch are integrated. The end-to-end GTK Application test for a live EI request followed by archive/close/reopen and restored History presentation remains unqualified. The original prototype's statements about separate downstream gates are historical; general missing/discontinuity/event semantics, generic multi-series comparison, and other scientific adapters remain in CHART-01's qualification ledger.

Primary implementation references:
- https://www.cairographics.org/manual/cairo-Image-Surfaces.html
- https://www.cairographics.org/manual/cairo-text.html
- https://www.cairographics.org/manual/cairo-Error-handling.html

## GTK Chart/Data component qualification

`chart_view.h/.c` adds a reusable GTK4 Box subclass with Chart/Data stack pages,
a native ColumnView in source order (year, exact anomaly, source row), and a
read-only selectable provenance expander for the selected observation. Its
metadata includes the exact canonical decimal, pinned source path/snapshot,
qualified series identity and support IDs. All cells use plain text, never markup.
`ChartNative.vapi` exposes the owned component to Vala; an independent Vala test
checks ownership, errors, page switching, selected row and exact data access.

The component revalidates and owns its scientific input. Strings in the table
model are owned copies; freeing the factory input or replacing the rendered
surface cannot alter Data. Render output is replaced on size/device-scale changes;
a draw callback only paints the prepared surface. CHART-04 qualifies the renderer
once at construction, then rebuilds only viewport-dependent projection/surface
state from that owned specification. GTK resize/device-scale bursts are coalesced
to one idle viewport update for the latest request; unsupported transient requests
retain the last valid surface and exact Data. No shared cache or production
downsampling caller is introduced.

Chart/Data switcher buttons use GTK keyboard behavior. Alt+1 and Alt+2 switch
pages and move focus. GTK 4.12's `gtk_column_view_scroll_to` focuses the selected
Data row explicitly. The Data table supports native arrow-key row selection;
selection updates provenance. The component, chart, table and provenance have
accessible names/descriptions and GTK table roles. This is a semantic/keyboard
baseline, not an end-to-end screen-reader or localization qualification.

`CHART-03 GTK component` CI uses a real X11 GTK window under Xvfb and XTest keys
(xdotool) to check Alt+2, Down and Alt+1, including selected-row provenance and
Data preservation after resize. GTK warnings are fatal; the C component runs
with UBSan. Both C and Vala paths also run in the Elementary Flatpak suite. The
XTest case is explicitly skipped there because the dedicated workflow owns that
external-keyboard qualification. The existing headless renderer/source suite
retains its separate ASan/UBSan and leak checks.

CI captures actual GTK Chart/Data/small-canvas PNGs through the widget snapshot
and GSK renderer for visual inspection. No fake rows or synthesized observations
are introduced. The Application now supplies the live and History callers and
persists only the qualified reconstruction recipe, never numeric chart points.
Clipboard/export controls, translation, full screen-reader testing, and the
end-to-end GTK Application interaction described above remain outside the current
qualification claim.

GTK references:
- https://docs.gtk.org/gtk4/class.ColumnView.html
- https://docs.gtk.org/gtk4/class.StackSwitcher.html
- https://docs.gtk.org/gtk4/method.DrawingArea.set_draw_func.html
- https://docs.gtk.org/gtk4/ctor.SingleSelection.new.html
