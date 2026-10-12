# CHART-01 Energy Institute F0-19 qualification proposal

**Status: approved and integrated for snapshot-level Evidence/SRA qualification (2026-10-12).** The canonical obligation is active in `ATM_EI_EVIDENCE_DESCRIPTOR` on `main` via PR #423. It does not authorize `VerifiedSeries` promotion, chart rendering, persistence, or raw-to-processed reproduction.

This record documents the explicit approval to qualify only faithful reconstruction of the byte-pinned EWD processed snapshot. It authorizes the generic source-bound SRA obligation under the semantic precedence below. It does not grant `VerifiedSeries`, `ChartSpec`, rendering, persistence, or model-output authority.

## Decision record

**Approved: snapshot-level reconstruction, not raw-to-processed reproduction.** The approved obligation is `atm-energy-institute-ewd-tes-pinned-snapshot-reconstruction/1`. The four-file EWD admission bundle supports the bounded snapshot claim; it does not contain the raw input bytes and pinned transformation environment needed to prove independent reproduction of Energy Institute's processing pipeline.

## Source facts already established

The candidate is pinned to EWD repository commit `d9e249339663015f6d1c05752338a955bf64ad0b` and these four EWD files:

- `science/data/processed/energy_institute_global_2026.csv`
- `science/data/processed/energy_institute_global_2026.provenance.json`
- `science/data/input_manifest.json`
- `science/data/registry.csv`

The provenance records the authority as Energy Institute, edition *Statistical Review of World Energy 2026*, the original input filename `Statistical Review of World Energy Narrow format.csv`, its SHA-256 `c19b4922cb08316b45d7024233e8cd9d35e14429ee8cf65ab767e647cefc1f95` and size 21,056,006 bytes, as well as coverage 1965–2025 and geography `Total World`. That raw input is identified by hash and size but is **not included as bytes in the current admitted four-file EWD bundle**.

The pinned transport code in [`shanewhi/world-energy-data@9fc01fc0ae5aea3955968f920e5cd1394fe5ad34`](https://github.com/shanewhi/world-energy-data/blob/9fc01fc0ae5aea3955968f920e5cd1394fe5ad34/collate.py) selects the total using `Var == "tes_ej"`. The official [Energy Institute methodology](https://www.energyinst.org/statistical-review/about) states that, from the 2025 edition, Total Energy Supply (TES) is calculated using the Physical Energy Content method; the earlier Primary Energy Consumption series used a fossil-fuel-equivalent method.

The processed table contains 61 consecutive annual observations, 1965–2025, in the legacy-named column `total_primary_energy_ej`. AtM profile v2 interprets that column as Total Energy Supply and records `energy_accounting_method = physical_energy_content`; it does not modify the pinned CSV, its hashes, decimal tokens or row values. The 2019–2023 component-reconciliation residual remains observational provenance and must not be corrected or distributed by the adapter.

### Accepted semantic precedence

The pinned EWD bundle still describes the series as `ei_primary_energy` / “Primary energy supply and fuel mix” in `registry.csv`, and its provenance `boundary_note` says “gross primary-energy supply observations.” These legacy labels are not propagated as the AtM metric identity.

The project reviewer explicitly accepted this precedence on 2026-10-12: AtM preserves the EWD metadata bytes for provenance and source identity, while the semantic contract follows the pinned transport mapping `tes_ej` and the Energy Institute 2026 methodology. This decision is recorded verbatim as a limitation in every finalized SRA. Hash agreement and green CI alone do not make this scientific decision; a later EWD metadata correction or source edition requires a separately reviewed profile/obligation change.

### Independent EWD source-equivalence evidence

EWD's R-FDC-6 primary-source audit records Energy Institute 2026 as `L4 / PASS` for its declared diagnostic/candidate role. It compares the pinned EWD mirror with an archived official EI 2026 CSV obtained through the OWID ETL public snapshot. The complete files are not byte-identical (21,056,006 versus 21,056,126 bytes), and the audit records one unrelated difference outside the EWD-used rows. For the declared EWD role, however, all 427 `Total World` observations for 1965–2025 across `tes_ej`, oil, gas, coal, nuclear, hydro and renewables TES match exactly: zero missing rows, zero mismatches, and maximum absolute difference zero. See the [EWD R-FDC-6 record at its September 28, 2026 closure commit](https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/dc710d8e604c0fff713a3acfdb18ab47a1a902bb/science/data/provenance/primary_source_audit_2026-09-22.json) and the [EWD primary-source audit report](https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/dc710d8e604c0fff713a3acfdb18ab47a1a902bb/science/docs/primary-source-provenance-audit-2026-09-22.md).

This is strong independent provenance for the selected EWD observations and `tes_ej` mapping. It is not proof that AtM's present Evidence/SRA builder downloads the archive or re-executes EWD's ingest path: AtM's admission input remains the four pinned EWD files listed above. The approved obligation states that boundary and treats EWD's R-FDC-6 record as supporting provenance, not as a claim that AtM executes that external ingestion path within its own qualification tests.

## Approved qualification scope

**Canonical obligation:** `atm-energy-institute-ewd-tes-pinned-snapshot-reconstruction/1`

> Given only the admitted EWD snapshot and its byte-pinned files, AtM reconstructs the same source-bound Evidence and SRA facts for the 61 annual Total Energy Supply observations, retaining exact source decimal tokens, year and source-row/column locators, the reviewed TES/Physical Energy Content interpretation, and the exact evidence support set. The result claims faithful reconstruction of this pinned processed snapshot, not independent reproduction of Energy Institute's raw-to-processed pipeline.

The obligation binds to the semantic profile `atm-profile/ewd-energy-institute-tes-physical-content/v2` and numeric profile `exact_decimal_coefficient_exponent/1`. A new source edition, input bundle, semantic method or output vocabulary requires a separately reviewed profile/obligation update; it must not be admitted merely because the CSV shape looks compatible.

### Qualification controls and registered regression tests

1. **Pinned source bundle:** admission verifies the exact EWD repository/snapshot and four source files, including manifest relationships and digests, before parsing or constructing artifacts.
2. **Full extraction:** the candidate has exactly 61 consecutive years, 1965–2025, `Total World`, and every value is the exact source token from `total_primary_energy_ej`; no float round-trip, sorting, imputation, or silent repair is allowed.
3. **Semantic binding and conflict decision:** each point binds year, value, unit `exajoules/year`, energy-flow-rate dimension, annual frequency, empirical status, `scenario = not_applicable`, TES and `physical_energy_content` to the pinned source evidence and current v2 payload. The qualification record must explicitly accept how this AtM interpretation supersedes the legacy “primary energy” description in the pinned EWD registry/provenance; if that decision is not approved, qualification stays blocked.
4. **SRA binding:** each of the 61 established observation facts is derived from the reconstructed admitted Evidence, with its exact support set. No caller-authored facts, derived facts, constraints, conflicts or transformations to EROI, net energy, fossil share or the World3 resource stock are authorized by this obligation.
5. **Anomaly preservation:** the 2019–2023 reconciliation values remain in the source evidence and audit trail, but are not used to rewrite the total or any components.
6. **Independent reconstruction and mutation resistance:** the Evidence/SRA output is deterministic from the pinned bytes; mutations to any admitted file, row order, calendar year, exact decimal token, locator, scenario, unit, TES method, source identity or support must be rejected by reconstruction/revalidation tests. A forged identity or rehashed artifact must not override source reconstruction.
7. **Fail-closed policy:** when the descriptor obligation is absent, generic SRA qualification remains blocked; constructing byte-bound Evidence alone remains permitted. The reviewed snapshot-level obligation is now set on `main`, and the registered negative-control test verifies that a descriptor with a NULL obligation still fails closed.
8. **Separate downstream gates:** this obligation would qualify only the source-bound SRA for this pinned snapshot. It does not automatically qualify a `VerifiedSeries`, authorize chart rendering, change persistence, or calibrate a World3 trajectory.

The registered generic SRA test now checks the approved obligation and semantic profile, all 61 fact rows, five evidence-support IDs per point, exact endpoint decimal values, semantic-precedence and anomaly limitations, finalized-result mutation rejection, and the negative control that a descriptor with a NULL obligation remains fail-closed. Existing admission tests reject byte mutations to each of the four pinned source files, and parser tests reject malformed or nonconsecutive rows. These tests establish the stated snapshot reconstruction contract; they do not establish upstream raw-to-processed reproduction or downstream chart qualification.

## Why this does not claim raw-source reproduction

The raw bytes are available through a separately audited EWD/OWID transport path, but they are not inputs to the current four-file AtM admission. Reproducing the upstream calculation *inside AtM's own qualification boundary* would require a second contract: include or explicitly admit the immutable raw-source bytes, pin the transformation code and its dependencies/configuration, specify the extraction and calculation environment, and test raw-to-processed reproduction. EWD's R-FDC-6 audit is valuable external provenance, but the current AtM SRA builder does not execute that pipeline. Until AtM explicitly adds and tests the stronger contract, the defensible AtM claim is snapshot-level reconstruction supported by independent EWD provenance.

This distinction is consistent with the existing GISTEMP boundary: its qualification reconstructs the reviewed, byte-pinned processed source and explicitly does not claim to reproduce NASA's raw-to-processed calculation. The Energy Institute obligation should be equally explicit, but source-specific.

## Future scope changes

The approved obligation remains deliberately narrow. If the intended claim changes to independent reproduction of the raw Energy Institute calculation, F0-19 must be reopened: AtM would need to admit immutable raw-source bytes, pin transformation code and dependencies/configuration, specify the execution environment and add raw-to-processed tests. Until that separate contract exists, no such claim may be made.

The active descriptor on `main` sets `ATM_EI_EVIDENCE_DESCRIPTOR.canonical_obligation` to the approved identifier through PR #423. This qualifies the pinned processed-snapshot Evidence/SRA chain only. Energy Institute remains unpromoted to `VerifiedSeries`; source-bound series-profile admission, chart rendering, persistence and World3 calibration remain separate gates.
