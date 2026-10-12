# CHART-01 Energy Institute source audit

## Scope

This audit documents the source-bound Energy Institute candidate and the approved, pinned-snapshot SRA qualification integrated on `main` through PR #423. It does not create a `VerifiedSeries`, `ChartSpec`, runtime chart path, persistence state, or model-output authority.

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

The transport code in that pinned commit selects the raw Energy Institute total using `Var == "tes_ej"`. Energy Institute's methodology says that from the 2025 edition onward, Total Energy Supply (TES) is calculated using the Physical Energy Content method; the former Primary Energy Consumption measure used the fossil-fuel-equivalent method and remains a separate measure. See the [Energy Institute methodology note](https://www.energyinst.org/statistical-review/about).

The EWD registry and processed CSV retain legacy identifiers such as `ei_primary_energy` and `total_primary_energy_ej`. AtM preserves those bytes and column names for source reproducibility, but does not propagate the legacy label into the semantic contract. The admitted AtM profile therefore labels this value as **Total Energy Supply (TES)** and binds `energy_accounting_method = physical_energy_content` into each observation payload. The method interpretation changes the semantic profile/payload version; the pinned CSV and its hashes are unchanged.

The pinned EWD registry concept and provenance `boundary_note` still describe the series as “Primary energy supply” / “gross primary-energy supply.” This is a real semantic metadata conflict, not just a filename difference. The project reviewer explicitly approved on 2026-10-12 that AtM v2 follows the pinned transport's `tes_ej` mapping and the official Energy Institute 2026 methodology, while preserving the old labels only as provenance. This precedence rule is recorded in the finalized SRA limitations.

The value is a gross annual energy-supply flow. It constrains the energy mix but does not directly observe EROI or net energy.

## Candidate series

The processed table contains 61 consecutive annual rows, 1965–2025.

The first qualification candidate is deliberately narrow:

- X: calendar year
- X unit: `calendar_year`
- X dimension: time
- Y metric: Total Energy Supply (TES), Physical Energy Content method
- pinned EWD column: `total_primary_energy_ej` (legacy column name, preserved byte-for-byte)
- Y unit: `exajoules/year`
- Y dimension: energy-flow rate
- subject: `global_total_energy_supply`
- attribute: `total_energy_supply`
- accounting method: `physical_energy_content`
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

These rows must not be silently repaired, redistributed, normalized, or recomputed by an AtM adapter. The candidate series therefore uses the pinned `total_primary_energy_ej` column directly as the source locator/value field, but interprets its metric according to the upstream `tes_ej` mapping as Total Energy Supply. The reconciliation column remains provenance/audit evidence, not a correction instruction.

The fossil/non-fossil component columns are not independent evidence for the total. A future multi-series adapter must explicitly qualify their semantic relationship before exposing them together.

## Admission boundary

This source is sufficiently specified for a source-audit slice but is **not yet a qualified VerifiedSeries**.

The approved source-bound SRA path now enforces the pinned four-file admission, bounded CSV parsing, 61 consecutive annual observations, exact decimal preservation, source locators and source-bound Evidence for all four inputs plus every point. The generic SRA is reconstructed and validated against the pinned evidence; each of the 61 facts binds its five support artifacts and deterministic qualification envelope. Registered tests cover source-byte mutation, malformed/nonconsecutive rows, all-point qualification, exact endpoint values, semantic metadata, anomaly preservation, finalized-result mutation and the NULL-obligation negative control.

No existing GISTEMP profile is widened by this audit. The Energy Institute source uses the versioned TES semantic profile `atm-profile/ewd-energy-institute-tes-physical-content/v2`. F0-19's scope decision is integrated on `main` via PR #423 for the bounded claim of reconstructing the pinned EWD processed snapshot, not independently reproducing Energy Institute's raw-to-processed pipeline. The active canonical obligation is `atm-energy-institute-ewd-tes-pinned-snapshot-reconstruction/1`. This authorizes the source-bound Evidence/SRA reconstruction only; it does not promote Energy Institute to `VerifiedSeries` or authorize a chart route. Source-bound series-profile admission and downstream chart/persistence gates remain separate.

The accepted decision, semantic-precedence rule, proof boundary and future requirements for any raw-to-processed claim are recorded in [`CHART01_ENERGY_INSTITUTE_F0-19_QUALIFICATION_PROPOSAL.md`](CHART01_ENERGY_INSTITUTE_F0-19_QUALIFICATION_PROPOSAL.md).

## Explicit non-claims

This audit does not claim that Total Energy Supply (TES) is:

- World3's resource-stock state;
- net energy;
- EROI;
- a direct observation of the `nrur` stock;
- interchangeable with fossil-energy flow;
- a calibrated World3 trajectory;
- sufficient evidence for a chart without the remaining CHART-01 and CHART-02 qualification gates.

The integrated Evidence/SRA path consumes the explicitly admitted, byte-pinned four-file source bundle. This does not authorize arbitrary runtime source access, a raw-source download, or execution of Energy Institute's upstream processing pipeline. The UI chart flow remains governed by separate source-profile and downstream qualification gates.

## M0 / F0-19 decision

**PASS on `main` via PR #423 for the approved source-snapshot Evidence/SRA qualification scope only.**

The source has an explicit authority, dataset vintage, geography, coverage, unit, frequency, processed-file identity and accepted TES semantic precedence. The 2019–2023 reconciliation anomaly remains uncorrected and explicitly recorded. The qualification claims faithful reconstruction of the pinned processed observations only.

`VerifiedSeries` promotion, chart rendering, persistence, transformations to EROI/net energy/World3 resource stock, and independent raw-to-processed reproduction remain out of scope and require separate qualification.
