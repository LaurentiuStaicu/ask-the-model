# CHART-01 Energy Institute F0-19 qualification proposal

**Status: proposal for review; not approved and not active in code.**

This record narrows the unresolved F0-19 question to a concrete choice. It does not grant SRA or `VerifiedSeries` qualification. The Energy Institute descriptor must keep `canonical_obligation = NULL` until the source-reconstruction scope below is explicitly accepted by the project reviewer.

## Decision requested

Should the generic SRA obligation for Energy Institute certify reconstruction of the exact, byte-pinned **EWD processed snapshot**, or is the required claim stronger and intended to certify independent reproduction of Energy Institute's raw-to-processed calculation?

Those are different contracts. The current four-file admission bundle supports the first claim; it does not contain the full raw input bytes needed to establish the second.

## Source facts already established

The candidate is pinned to EWD repository commit `d9e249339663015f6d1c05752338a955bf64ad0b` and these four EWD files:

- `science/data/processed/energy_institute_global_2026.csv`
- `science/data/processed/energy_institute_global_2026.provenance.json`
- `science/data/input_manifest.json`
- `science/data/registry.csv`

The provenance records the authority as Energy Institute, edition *Statistical Review of World Energy 2026*, the original input filename `Statistical Review of World Energy Narrow format.csv`, its SHA-256 `c19b4922cb08316b45d7024233e8cd9d35e14429ee8cf65ab767e647cefc1f95` and size 21,056,006 bytes, as well as coverage 1965–2025 and geography `Total World`. That raw input is identified by hash and size but is **not included as bytes in the current admitted four-file EWD bundle**.

The pinned transport code in [`shanewhi/world-energy-data@9fc01fc0ae5aea3955968f920e5cd1394fe5ad34`](https://github.com/shanewhi/world-energy-data/blob/9fc01fc0ae5aea3955968f920e5cd1394fe5ad34/collate.py) selects the total using `Var == "tes_ej"`. The official [Energy Institute methodology](https://www.energyinst.org/statistical-review/about) states that, from the 2025 edition, Total Energy Supply (TES) is calculated using the Physical Energy Content method; the earlier Primary Energy Consumption series used a fossil-fuel-equivalent method.

The processed table contains 61 consecutive annual observations, 1965–2025, in the legacy-named column `total_primary_energy_ej`. AtM profile v2 interprets that column as Total Energy Supply and records `energy_accounting_method = physical_energy_content`; it does not modify the pinned CSV, its hashes, decimal tokens or row values. The 2019–2023 component-reconciliation residual remains observational provenance and must not be corrected or distributed by the adapter.

### Semantic metadata conflict requiring explicit acceptance

The same pinned EWD bundle still describes the series as `ei_primary_energy` / “Primary energy supply and fuel mix” in `registry.csv`, and the provenance `boundary_note` says “gross primary-energy supply observations.” These are not semantically identical to the TES label used by AtM v2. The pinned transport mapping to `tes_ej` and Energy Institute's current methodology support the TES interpretation, but the bundle contains this legacy/conflicting wording.

The snapshot obligation must therefore include an explicit reviewed precedence rule: AtM preserves the EWD metadata bytes for provenance and source identity, while the approved semantic contract follows the pinned `tes_ej` mapping and the official Energy Institute methodology for the 2026 edition. The reviewer must accept that interpretation explicitly. It must not be treated as an automatic consequence of a hash match or CI success. If that precedence is not accepted, keep the semantic change and F0-19 qualification gate unresolved until EWD's metadata are corrected and re-pinned or the source contract models the discrepancy directly.

### Independent EWD source-equivalence evidence

EWD's R-FDC-6 primary-source audit records Energy Institute 2026 as `L4 / PASS` for its declared diagnostic/candidate role. It compares the pinned EWD mirror with an archived official EI 2026 CSV obtained through the OWID ETL public snapshot. The complete files are not byte-identical (21,056,006 versus 21,056,126 bytes), and the audit records one unrelated difference outside the EWD-used rows. For the declared EWD role, however, all 427 `Total World` observations for 1965–2025 across `tes_ej`, oil, gas, coal, nuclear, hydro and renewables TES match exactly: zero missing rows, zero mismatches, and maximum absolute difference zero. See the [EWD R-FDC-6 record at its September 28, 2026 closure commit](https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/dc710d8e604c0fff713a3acfdb18ab47a1a902bb/science/data/provenance/primary_source_audit_2026-09-22.json) and the [EWD primary-source audit report](https://github.com/LaurentiuStaicu/empirical-world3-dynamics/blob/dc710d8e604c0fff713a3acfdb18ab47a1a902bb/science/docs/primary-source-provenance-audit-2026-09-22.md).

This is strong independent provenance for the selected EWD observations and `tes_ej` mapping. It is not proof that AtM's present Evidence/SRA builder downloads the archive or re-executes EWD's ingest path: AtM's admission input remains the four pinned EWD files listed above. The proposed obligation must state that boundary, while treating EWD's R-FDC-6 record as supporting provenance rather than silently folding that external execution into AtM's own qualification tests.

## Recommended scope if the source-snapshot contract is accepted

**Proposed identifier only; not an approved canonical obligation:**

`atm-energy-institute-ewd-tes-pinned-snapshot-reconstruction/1`

The obligation would mean:

> Given only the admitted EWD snapshot and its byte-pinned files, AtM reconstructs the same source-bound Evidence and SRA facts for the 61 annual Total Energy Supply observations, retaining exact source decimal tokens, year and source-row/column locators, the reviewed TES/Physical Energy Content interpretation, and the exact evidence support set. The result claims faithful reconstruction of this pinned processed snapshot, not independent reproduction of Energy Institute's raw-to-processed pipeline.

The obligation would bind to the current semantic profile `atm-profile/ewd-energy-institute-tes-physical-content/v2` and exact-decimal numeric profile `exact_decimal_coefficient_exponent/1`. A new source edition, input bundle, semantic method or output vocabulary would require a separately reviewed profile/obligation update; it must not be admitted merely because the CSV shape looks compatible.

### Required proof before activation

1. **Pinned source bundle:** admission verifies the exact EWD repository/snapshot and four source files, including manifest relationships and digests, before parsing or constructing artifacts.
2. **Full extraction:** the candidate has exactly 61 consecutive years, 1965–2025, `Total World`, and every value is the exact source token from `total_primary_energy_ej`; no float round-trip, sorting, imputation, or silent repair is allowed.
3. **Semantic binding and conflict decision:** each point binds year, value, unit `exajoules/year`, energy-flow-rate dimension, annual frequency, empirical status, `scenario = not_applicable`, TES and `physical_energy_content` to the pinned source evidence and current v2 payload. The qualification record must explicitly accept how this AtM interpretation supersedes the legacy “primary energy” description in the pinned EWD registry/provenance; if that decision is not approved, qualification stays blocked.
4. **SRA binding:** each of the 61 established observation facts is derived from the reconstructed admitted Evidence, with its exact support set. No caller-authored facts, derived facts, constraints, conflicts or transformations to EROI, net energy, fossil share or the World3 resource stock are authorized by this obligation.
5. **Anomaly preservation:** the 2019–2023 reconciliation values remain in the source evidence and audit trail, but are not used to rewrite the total or any components.
6. **Independent reconstruction and mutation resistance:** the Evidence/SRA output is deterministic from the pinned bytes; mutations to any admitted file, row order, calendar year, exact decimal token, locator, scenario, unit, TES method, source identity or support must be rejected by reconstruction/revalidation tests. A forged identity or rehashed artifact must not override source reconstruction.
7. **Fail-closed policy:** when the descriptor obligation is absent, generic SRA qualification remains blocked; constructing byte-bound Evidence alone remains permitted. The obligation must be set only after the scope and its tests are reviewed and approved.
8. **Separate downstream gates:** this obligation would qualify only the source-bound SRA for this pinned snapshot. It does not automatically qualify a `VerifiedSeries`, authorize chart rendering, change persistence, or calibrate a World3 trajectory.

Existing admission, Evidence, descriptor-equivalence, and fail-closed tests cover parts of this contract. Before activation, reviewers should confirm that the complete checklist above is represented by registered native tests, rather than treating an internally consistent identity or a green Flatpak build as scientific qualification.

## Why this does not claim raw-source reproduction

The raw bytes are available through a separately audited EWD/OWID transport path, but they are not inputs to the current four-file AtM admission. Reproducing the upstream calculation *inside AtM's own qualification boundary* would require a second contract: include or explicitly admit the immutable raw-source bytes, pin the transformation code and its dependencies/configuration, specify the extraction and calculation environment, and test raw-to-processed reproduction. EWD's R-FDC-6 audit is valuable external provenance, but the current AtM SRA builder does not execute that pipeline. Until AtM explicitly adds and tests the stronger contract, the defensible AtM claim is snapshot-level reconstruction supported by independent EWD provenance.

This distinction is consistent with the existing GISTEMP boundary: its qualification reconstructs the reviewed, byte-pinned processed source and explicitly does not claim to reproduce NASA's raw-to-processed calculation. The Energy Institute obligation should be equally explicit, but source-specific.

## Decision rule

- **Approve the proposed snapshot obligation** only if the intended CHART-01 claim is faithful reconstruction of the pinned EWD processed TES observations. Approval must be explicit and followed by a code/test change that sets the exact reviewed identifier.
- **Reject this proposal and keep F0-19 closed** if the required claim is independent reproduction of the raw Energy Institute calculation. In that case, first expand the admitted bundle and implement/test the raw-to-processed reconstruction contract.

No code in this proposal sets the candidate identifier in `ATM_EI_EVIDENCE_DESCRIPTOR.canonical_obligation`; the current fail-closed gate remains authoritative.
