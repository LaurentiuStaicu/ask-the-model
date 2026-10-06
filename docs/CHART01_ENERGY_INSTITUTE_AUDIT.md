# CHART-01 Energy Institute source audit

## Scope

This audit establishes a dormant source-bound candidate for a second real scientific series under CHART-01. It does not create a VerifiedSeries, ChartSpec, runtime chart path, persistence state, or model-output authority.

The audited source is the exact EWD snapshot:

- repository: `LaurentiuStaicu/empirical-world3-dynamics`
- snapshot: `d9e249339663015f6d1c05752338a955bf64ad0b`
- processed source: `science/data/processed/energy_institute_global_2026.csv`
- processed-source SHA-256: `46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c`
- provenance: `science/data/processed/energy_institute_global_2026.provenance.json`
- provenance SHA-256: `1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57`
- registry: `science/data/registry.csv`
- input manifest: `science/data/input_manifest.json`

The source provenance identifies the authority as **Energy Institute**, dataset **Statistical Review of World Energy 2026**, geography **Total World**, coverage **1965–2025**, and the original input file as `Statistical Review of World Energy Narrow format.csv`.

The upstream transport receipt is retained as provenance only:

- transport repository: `shanewhi/world-energy-data`
- transport commit: `9fc01fc0ae5aea3955968f920e5cd1394fe5ad34`
- input SHA-256: `c19b4922cb08316b45d7024233e8cd9d35e14429ee8cf65ab767e647cefc1f95`

The EWD registry identifies `ei_primary_energy` as an empirical annual series in **exajoules/year**, with the explicit conceptual boundary that these are gross primary-energy supply observations. They constrain the energy mix but do not directly observe EROI or net energy.

## Candidate series

The processed table contains 61 consecutive annual rows, 1965–2025.

The first qualification candidate is deliberately narrow:

- X: calendar year
- X unit: `calendar_year`
- X dimension: time
- Y: `total_primary_energy_ej`
- Y unit: `exajoules/year`
- Y dimension: energy-flow rate
- subject: global primary-energy supply
- attribute: total primary energy
- frequency: annual
- geography: Total World
- epistemic status: empirical observation
- scenario: not applicable

No conversion to per-capita energy, fossil share, EROI, net energy, or World3 resource stock is permitted by this audit. Such transformations would require separate scientific qualification.

## Reconciliation finding

The processed table also contains `component_reconciliation_error_ej`.

For 1965–2018 and 2024–2025 the residual is at a small numerical-reconciliation scale. In 2019–2023 it is materially larger:

- 2019: 0.37386361 EJ
- 2020: 0.35018161 EJ
- 2021: 0.42394016 EJ
- 2022: 0.45502217 EJ
- 2023: 0.41971255 EJ

These rows must not be silently repaired, redistributed, normalized, or recomputed by an AtM adapter. The candidate series therefore uses the authoritative `total_primary_energy_ej` column directly and treats the reconciliation column as provenance/audit evidence, not as a correction instruction.

The fossil/non-fossil component columns are not independent evidence for the total. A future multi-series adapter must explicitly qualify their semantic relationship before exposing them together.

## Admission boundary

This source is sufficiently specified for a source-audit slice but is **not yet a qualified VerifiedSeries**.

Before qualification, a dedicated adapter must establish:

1. exact CSV dialect and bounded parser behavior;
2. complete 61-point extraction with consecutive calendar years;
3. exact decimal preservation;
4. source-row and source-column locators;
5. source-bound evidence artifacts for the processed file and provenance/manifest/registry metadata;
6. finalized SRA support for every point;
7. explicit treatment of the 2019–2023 reconciliation anomaly without altering observations;
8. deterministic scientific and qualified series identities;
9. independent reconstruction from the pinned bytes;
10. tests for mutation, reordered rows, changed values, changed source spelling, provenance changes, and identity forgery.

No existing GISTEMP profile is widened by this audit. The Energy Institute source must receive its own profile/version.

## Explicit non-claims

This audit does not claim that primary-energy supply is:

- World3's resource-stock state;
- net energy;
- EROI;
- a direct observation of the `nrur` stock;
- interchangeable with fossil-energy flow;
- a calibrated World3 trajectory;
- sufficient evidence for a chart without the remaining CHART-01 and CHART-02 qualification gates.

It also does not authorize runtime access to the source file. The future adapter must consume an explicitly admitted, byte-pinned source bundle, following the existing GISTEMP qualification architecture.

## M0 decision

**PASS for source-audit continuation.**

The source has an explicit authority, dataset vintage, geography, coverage, unit, frequency, processed-file identity and semantic boundary. The reconciliation anomaly is observable and can be preserved without inventing a correction.

The next slice is a dormant source-specific admission/parser for the exact pinned Energy Institute table. Runtime chart activation remains out of scope.
