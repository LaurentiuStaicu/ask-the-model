# CHART-05: qualified History chart reconstruction

Status: slice 4 integrated on the chart-05-history-reconstruction branch; chart runtime remains disabled for new/live turns.

## Reconstruction authority

A persisted chart is a reconstruction recipe, not a stored numeric dataset.

When an archived conversation is reopened, AtM:

1. resolves the chart series against the conversation's pinned repository id, version and snapshot SHA;
2. resolves the immutable local snapshot directory;
3. re-reads the four audited GISTEMP source files from that snapshot;
4. re-runs atm_gistemp_admission_new();
5. rebuilds AtmVerifiedSeries;
6. rebuilds AtmChartSpec;
7. compares the rebuilt scientific, qualified and ChartSpec identities with the persisted identities;
8. only after all checks pass creates the existing GTK Chart/Data component.

The persisted database never supplies numeric points, projected coordinates, pixel state, M4 output or layout state.

## Fail-closed behavior

The first admitted profile remains:

- repository: LaurentiuStaicu/empirical-world3-dynamics;
- snapshot: d9e249339663015f6d1c05752338a955bf64ad0b;
- series profile: atm-series/gistemp-complete-annual/1;
- admission profile: atm-gistemp-pinned-admission/1;
- reconstruction profile: atm-chart-reconstruct/gistemp-complete-annual/1;
- source path: science/data/processed/nasa_gistemp_global_2026.csv;
- chart kinds: persisted line/scatter, mapped to the qualified native LINE/SCATTER kinds.

Repository pin drift, missing source bytes, malformed recipes and identity mismatch do not fall back to persisted chart values. History shows a compact chart-unavailable status instead.

## Presentation boundary

The reconstructed AtmChartView is inserted as a Presentation child anchor. It is not transcript text and therefore does not become provider history or semantic clipboard text.

The existing Chart/Data component remains authoritative for exact displayed source values and provenance after successful reconstruction.

## Qualification

chart-history-reconstruct proves:

- successful source re-admission and identity reconstruction;
- persisted scientific/qualified/spec identity equality;
- wrong snapshot rejection;
- missing snapshot rejection.

The existing ChartSpec, VerifiedSeries, GTK, Flatpak and Invariant Registry gates remain non-regression requirements.
