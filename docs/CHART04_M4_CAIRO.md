# CHART-04: Cairo equivalence gate for M4

Status: test-only renderer characterization. No production renderer, GTK,
Application, conversation or History caller uses M4.

## Why this gate exists

The M4 paper groups observations using the visualization's horizontal transform
and proves its result in the context of line rasterization. The AtM renderer uses
Cairo image surfaces, fractional user-space coordinates, antialias NONE and a
1.5 logical-unit line width. Therefore a generic floor/round conversion from
projected X to a raster column is not assumed to inherit the paper's exactness.

The M4 core intentionally accepts a caller-supplied pixel column. This gate tests
candidate mappings against the actual Cairo policy before any mapping can become
production behavior.

## Qualified result

The deterministic integer-column fixture has eight observations per raster column.
first/min/max/last reduction removes observations while producing a byte-identical
Cairo image to the full sequence under the AtM line policy. This establishes only
an integer-column-aligned display-geometry qualification.

A deterministic dense fractional-X fixture is rendered twice for each obvious
candidate mapping:

- floor/clamp projected X to a column;
- nearest/clamp projected X to a column.

For both mappings, full and reduced Cairo surfaces differ. These mappings are
therefore explicitly NOT admitted for generic fractional AtM line geometry.

This is a fail-closed characterization, not a proof that no possible reduction
scheme can preserve every fractional Cairo line. AtM will not snap coordinates,
change line width, or alter scientific/display values merely to make M4 pass.

## Consequence for current AtM

The qualified GISTEMP series has 146 observations. Every non-fallback chart uses
a plot width of at least 320 logical units, so the current source does not require
downsampling. M4 remains dormant and the existing renderer is unchanged.

CHART-04 can proceed to resize/backing-surface optimization independently. A
future dense source may use M4 only after its actual projection/raster geometry
passes a dedicated equality gate.

References:
- M4 paper: https://datavis.cs.columbia.edu/files/papers/m4.pdf
- Cairo antialias API: https://www.cairographics.org/manual/cairo-cairo-t.html
