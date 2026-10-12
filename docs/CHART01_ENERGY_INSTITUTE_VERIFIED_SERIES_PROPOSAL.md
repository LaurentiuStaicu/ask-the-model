# CHART-01 Energy Institute VerifiedSeries proposal

**Status: design proposal only, 2026-10-12. No production code, chart route, persistence format, or public-release change is authorized by this document.** The scope-level F0-19 decision has already approved Evidence/SRA reconstruction of the pinned processed snapshot only. This proposal requests a separate review of whether that same bounded source can support a narrowly defined output-series contract.

## Executive finding

The current generic source Evidence/SRA implementation is not an Energy Institute series adapter. Its deliberate fail-closed boundary is correct:

- `ATM_EI_EVIDENCE_DESCRIPTOR` declares the approved canonical reconstruction obligation and semantic Evidence profile, but leaves the reviewed output-series fields unset.
- `series_adapter.c` requires those output-series fields to be declared by the descriptor and match the caller's metadata before it mints a series identity. An Evidence/SRA semantic profile is not, by itself, approval of a plot-ready series.
- `verified_series.c` is still a GISTEMP-specific implementation: it owns `AtmGistempAdmission` and `AtmGistempSra`, rebuilds that source, and asserts 146 points for 1880–2025.
- `chart_spec.c` and `chart_history_reconstruct.c` also use GISTEMP-specific verified-series and reconstruction contracts. A metadata-only change to the EI descriptor would therefore neither produce a qualified EI `VerifiedSeries` nor safely enable a chart.

This proposal keeps those gates separate and leaves the current GISTEMP behavior and canonical identities unchanged.

## Proposed bounded claim

Proposed profile identifier, subject to review: `atm-series/energy-institute-tes-complete-annual/1`.

The only candidate claim is:

> AtM faithfully reconstructs the 61 ordered annual Total Energy Supply observations represented by the exact, admitted EWD processed Energy Institute 2026 snapshot, under the reviewed TES / Physical Energy Content semantic interpretation, retaining exact source decimal tokens and point-level source provenance. This is a claim about the pinned processed observations, not an independent reproduction of Energy Institute's raw-to-processed pipeline.

Proposed material semantics:

| Field | Required value or rule |
| --- | --- |
| Repository and snapshot | `LaurentiuStaicu/empirical-world3-dynamics` at `d9e249339663015f6d1c05752338a955bf64ad0b` |
| Source vintage | Energy Institute, *Statistical Review of World Energy 2026*, as represented by the pinned EWD snapshot |
| X | Consecutive integer calendar years, strictly ascending from 1965 through 2025 |
| Y meaning | Global Total Energy Supply (TES), Physical Energy Content interpretation |
| Source value column | `total_primary_energy_ej`, retained as an exact source locator despite its legacy name |
| Unit and dimension | `exajoules/year`; energy-flow rate |
| Subject and attribute | `global_total_energy_supply`; `total_energy_supply` |
| Frequency and geography | Annual; `Total World` |
| Epistemic status and scenario | Empirical observation; `not_applicable` |
| Output profile | Proposed `atm-series/energy-institute-tes-complete-annual/1` |
| Numeric encoding | `exact_decimal_coefficient_exponent/1`, preserving each original decimal token |
| Point status | NUMERIC only for this complete-source profile; no missing-value inference, interpolation, or discontinuity repair |

The semantic precedence already approved for F0-19 applies: the pinned transport mapping `tes_ej` and Energy Institute 2026 methodology define the metric interpretation; the legacy EWD registry/provenance labels remain preserved provenance, not the AtM metric identity. A later source edition, changed mapping, changed accounting method or changed admitted files would require a separately reviewed profile and obligation. The profile must not imply that all historical values were independently recalculated by AtM using the current methodology.

This is not a claim about a World3 resource stock, net energy, EROI, fossil-energy flow, calibration, or any derived transformation. It does not authorize plotting components together with the total. The material 2019–2023 component-reconciliation residual remains an audit/provenance observation; the adapter must not repair or redistribute it.

## Pinned input and provenance boundary

Admission currently binds the following four files to the reviewed EWD commit:

| Input | SHA-256 |
| --- | --- |
| `science/data/processed/energy_institute_global_2026.csv` | `46b30240372b392f7e7aabb7b63a0521afd9a9af2574ff044867ea84d0a1074c` |
| `science/data/processed/energy_institute_global_2026.provenance.json` | `1eca3a26375f7d56a0c4540eabccd8ea0d10c984dc1381757b2f4464f34e6a57` |
| `science/data/input_manifest.json` | `887bfec97515472e22084a6e28655b1d644d86949dc59b5a506801346a1fce8e` |
| `science/data/registry.csv` | `0637a1842706d2303750d6591851830987f3899de681e432c32b2be7b71adf4d` |

The parser already requires the expected header, fixed row shape, exact decimal syntax, 61 consecutive years and coverage 1965–2025. Admission verifies repository identity, snapshot identity and all four byte digests before parsing. The raw input is identified in provenance by SHA-256 `c19b4922cb08316b45d7024233e8cd9d35e14429ee8cf65ab767e647cefc1f95`, but those raw bytes and the upstream transformation environment are not part of AtM's current admitted bundle. The proposed `VerifiedSeries` must inherit, not overstate, that boundary.

Existing generic SRA tests already check the 61 established observations, their exact values, the approved semantic precedence and limits, and the five evidence-support IDs per observation. That is a prerequisite, not sufficient evidence for the new series layer.

## Required native qualification gate

Before the series profile is enabled in the production EI descriptor, add source-bound tests for all of the following:

1. **Reviewed-profile admission.** The exact output-series fields are declared in the EI descriptor only after the profile decision is reviewed. An unset, altered, unsupported or version-mismatched descriptor must fail closed. Caller metadata cannot approve a profile.
2. **Exact reconstruction.** Build a candidate series only from a freshly rebuilt EI admission and validated SRA. Compare all 61 years, source row numbers (header is row 1; observations are rows 2–62), original decimal tokens, canonical coefficients/exponents, values, source columns, units and semantic qualifiers against the pinned bytes.
3. **Provenance and support.** Each point binds the exact five support artifacts used by the existing SRA path. The aggregate support set equals the 65-artifact evidence collection. No caller-provided point arrays, support IDs, facts or identity strings may be accepted as an alternative input.
4. **Identity separation.** Scientific identity binds ordered values and meaning-changing semantics. Qualified identity additionally binds the exact snapshot, the output-series profile, source locators and spelling, the complete validated SRA qualified identity, aggregate support and point-level support. Preserve distinct identity domains; do not reuse the SRA ID as a series ID. Rebuilding identical pinned data must be deterministic; altered values or semantics must not be rescued by recomputed caller hashes.
5. **Adversarial reconstruction.** Test changed source bytes, swapped/missing bundle files, different snapshot, duplicate/reordered/gapped years, altered decimals, changed source rows/columns, unit or dimension mismatch, stale/recomputed identity forgeries, dropped/duplicated support, mutated qualifier JSON and profile-version mismatch. Null output and ownership behavior on failure must follow existing APIs.
6. **Narrow complete-series policy.** Reject incomplete coverage, nonconsecutive years, non-numeric/missing values and breaks for this profile. Do not generalize this to all future `VerifiedSeries` profiles; general missing/discontinuity semantics remain a separate CHART-01 gate.
7. **Rebuild parity.** Validation independently rebuilds admission, Evidence and SRA from owned source bytes and verifies the full series again. A self-consistent contract or a correctly rehashed altered value is not enough.

The qualification CI should include CHART-01 native ASan/UBSan with leak detection, Flatpak/Meson tests, Invariant Registry and applicable existing chart component/resize workflows. Passing these tests would qualify only this source/profile boundary; it would not certify the science generally or activate rendering.

## Downstream design dependency

The current `AtmVerifiedSeries` API and `AtmChartSpec` are structurally GISTEMP-specific. Before adding a renderer call, an explicit interface decision is required: introduce a source-neutral validated-series interface with separate source-bound constructors, or safely generalize the existing opaque type while preserving GISTEMP's canonical identities and reconstruction behavior. Do not use casts, accept caller-authored points, or let profile metadata alone bypass revalidation.

Only after native EI-series qualification is green should a separate chart integration slice:

- admit the new profile through `ChartSpec` with compatible units/dimensions and the existing bounded line/scatter and linear-axis policy;
- bind the chart specification and identity to the exact EI series qualification;
- implement exact History reconstruction from the pinned EWD snapshot, with no persistence of numeric points as authority;
- qualify the Data view, Cairo renderer, GTK integration, resizing/cache behavior and live/History parity;
- add a distinct user-intent route only if approved, and keep the GISTEMP route unchanged.

Any persisted schema or canonical identity compatibility impact must be explicit before a migration or release. The existing v0.6.3 artifact remains immutable; none of the development qualifications described here should be described as part of that release.

## Decision gates

- **Gate A — scientific/profile decision:** review the bounded claim, profile name/version, exact semantic fields, and the no-raw-pipeline/no-derivation limitations.
- **Gate B — native series qualification:** implement only the source-bound `VerifiedSeries` layer and its deterministic negative/reconstruction tests; no runtime chart or persistence changes.
- **Gate C — downstream chart contract:** explicitly review the source-neutral type/API and chart/history identity design; only then implement chart integration.
- **Gate D — application/release readiness:** end-to-end live and History verification, exact-head CI, documentation and release evidence. No release is inferred from earlier gates.

This proposal is intentionally not a code-change authorization. The current fail-closed boundary remains active until each applicable gate has been reviewed and qualified.
