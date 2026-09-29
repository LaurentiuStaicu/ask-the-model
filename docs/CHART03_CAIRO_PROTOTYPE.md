# CHART-03: headless monochrome prototype

Status: dormant native prototype, linked only into tests. No application caller,
GTK Data view widget, persistence or chart prompt is enabled by this slice.

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

Next: user-facing GTK chart/Data view integration, keyboard/accessibility and
same-component live/History rendering tests. CHART-04 cache/downsampling and
CHART-05 persistence v3 remain separate gates. Broader scientific adapters stay
in CHART-01's qualification ledger.

Primary implementation references:
- https://www.cairographics.org/manual/cairo-Image-Surfaces.html
- https://www.cairographics.org/manual/cairo-text.html
- https://www.cairographics.org/manual/cairo-Error-handling.html
