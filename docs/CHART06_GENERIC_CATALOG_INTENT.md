# CHART-06: generic chart intent and eligible-series catalog

**Status, 2026-10-15: implementation contract proposal.** The catalog/intent architecture below is planned, not implemented, tested, integrated or released. This document does not widen chart eligibility or change the immutable v0.7.1 artifact.

## 1. Why a new boundary is needed

The current application has three admitted source-specific routes: GISTEMP, Energy Institute TES and BNR BLS C01. `ChartIntent` represents them as three booleans; `Application` dispatches to three source-specific live reconstruction functions; History dispatches schema versions 1/2/3 separately. The native `AtmSeriesContract` is a reusable contract, but that does not make the live application path generic. Its existence must not be described as generic charting already implemented.

A request such as “plot population over time” cannot become a chart merely because a retrieved CSV contains a column called `population`. The application needs an explicit, reviewed link between a human-readable series concept and a source adapter that independently qualifies exact bytes, coordinate semantics, units, missing-value policy and provenance.

This layer must also address the observed sparse-record problem. A source row containing a population value while other columns are blank is not evidence that the other variables are zero, nor proof that the file contains complete primary observations for each variable. Generic column discovery must not promote those columns to chartable series.

## 2. Non-negotiable authority rules

1. **The catalog is an eligibility allowlist, not a data registry scraper.** Only explicitly reviewed entries whose deterministic adapters exist may be offered for charting. Numeric-looking columns, repository presence, a registry row, or a successful parse do not independently grant chart authority.
2. **The adapter, not the LLM, constructs the series.** The adapter resolves the catalog entry to an exact repository identity and allowed snapshot policy, checks the complete required file bundle and digests, then re-derives Evidence/SRA/series semantics. Caller-provided identity strings are not proof.
3. **The model proposes only a bounded intent.** It may select one registered `series_id`, request an allowed chart kind and distinguish chart-only from interpretation/data-extract requests. It cannot provide paths, repository identities, snapshot SHAs, numeric points, X/Y labels, units, dimensions, missing-value policy, axis domains, transformations or provenance claims.
4. **Schema validation is not request grounding.** A syntactically valid proposal whose `series_id` is in the catalog proves only schema validity and catalog membership. It does not prove that the original user request is about that series or asks for a chart. The application must independently ground the selection in the original request using a reviewed deterministic command/alias match or a separately specified and qualified grounding gate. A model-proposed registered ID alone never authorizes rendering; a conflict, unsupported concept or unresolved ambiguity fails closed.
5. **Unknown and ambiguous requests fail closed.** An unregistered series does not silently fall through to a successful-looking generic chart or a pseudo-table. Multiple matching series require clarification. Unsupported kind/filter/transform requests are reported rather than silently ignored.
6. **Missing is never zero.** No implicit imputation, interpolation, extrapolation, row synthesis, smoothing or gap-bridging. Each adapter declares how blank source cells, authorized missing observations and discontinuities are interpreted. A source profile that rejects missing observations must keep rejecting them.
7. **Source identity and scientific meaning are separate from display.** Display conversions and labels never overwrite the canonical source decimal or enter SRA facts. Chart/Data, and the recipe retained by History, must preserve the qualified source identity and point-level provenance.
8. **Primary-source claims are explicit.** User-facing provenance must distinguish publisher/original source material from the pinned processed file actually reconstructed by AtM. A primary-source link does not prove AtM independently reproduced the publisher's raw-to-processed pipeline. If a chart uses a processed snapshot, the wording and evidence card must say so.

## 3. Eligible-series catalog contract

The first catalog is static and versioned (build-time data or equivalent typed declarations). It is not populated by scanning every checked-out CSV/TSV and guessing from headers. Proposed identity: `atm-chart-catalog/1`.

Each entry must define, at minimum:

| Field | Contract |
| --- | --- |
| `series_id` | Stable, unique, lowercase namespaced ID. Never derived from display wording at runtime. |
| `display_name`, `description`, `aliases` | Human-facing name and bounded English/Romanian aliases. Alias collisions are rejected or explicitly marked ambiguous. |
| `adapter_id` | Closed compile-time identifier selecting a reviewed deterministic source adapter. No dynamic library or path lookup from model output. |
| `series_profile_id/version` | Exact series qualification profile emitted by the adapter. |
| `admission_profile_id/version` | Exact source/snapshot admission rule; all accepted snapshots are explicit. |
| `reconstruction_profile_id/version` | Exact live/History reconstruction behavior for this series. |
| `repository_id`, `snapshot_policy`, `source_bundle` | Repository identity, allowed immutable snapshots and exact required artifact paths/digests or manifest relations. A branch name is not a snapshot policy. |
| `x` | Coordinate kind, canonical encoding, frequency/aggregation, order/duplicate/gap policy and semantic scope rules. |
| `y` | Subject, attribute, unit, dimension, observation meaning and relevant qualifiers (scenario, epistemic status, observed/simulated, reference period). |
| `missing_policy` | Explicit `REJECT` or a qualified point-level `MISSING/GAP` contract, including when a blank cell is authoritative and whether a discontinuity breaks a line. |
| `allowed_chart_kinds` | Only the kinds independently supported by the adapter, ChartSpec and renderer. The first generic slice is one series and LINE only. |
| `source_provenance` | Publisher, original/primary-source URL where available, pinned artifact URL/path, citation/provenance artifacts, and a truthful scope statement about processing/reproduction. |
| `display_profile_id/version` | Pre-reviewed display conversion and axis/tick policy. The model cannot choose or modify it. |
| `history_policy` | Exact recipe schema/adapter version and source identity needed to rebuild the same chart after the original conversation is archived. |

Catalog startup validation must reject duplicate IDs, ambiguous aliases without an ambiguity marker, nonexistent adapter/profile identifiers, unsupported coordinate/kind combinations, empty provenance essentials, invalid missing policies and conflicting metadata. Catalog metadata is a selector and consistency assertion; the adapter must independently reconstruct and validate the actual series. It cannot turn metadata alone into evidence.

### Eligibility stages

An entry is selectable only when all stages pass:

1. The original user request unambiguously supports the requested chart and selected catalog series; a model-proposed ID alone is not evidence of grounding.
2. Catalog entry is present and internally consistent.
3. The requested repository is in scope for this turn and its exact snapshot is admitted.
4. The source adapter validates every required artifact and provenance/hash binding.
5. The adapter constructs a qualified `AtmSeriesContract` (or a narrower typed qualified wrapper) from the finalized SRA/source facts and checks all X/Y semantics and point support.
6. The resulting qualified series' profile, scope, units/dimensions, source identity and missing semantics match the catalog contract exactly.
7. The new generic ChartSpec constructor validates that qualified result and the single allowed chart kind.
8. The registered display profile can project the exact values within all limits.
9. The native Chart/Data widget can render from the reconstructed spec; errors yield a clear unavailable status and never substitute model-authored values.
10. If saved to History, the recipe records only identities/profile versions and pinned source provenance. Reopening must re-run stages 3–9 and compare reconstructed identities.

A failure at any stage prevents that chart from being displayed. Diagnostics should identify the failed eligibility stage without exposing internal identity strings as user-facing scientific claims.

## 4. Bounded intent schema

Proposed JSON Schema identity: `atm-chart-intent/1`. The implementation may ask the configured local model to produce this structured proposal when natural-language intent cannot be decided deterministically. It must not assume JSON-schema output works on every installed Ollama/model combination; compatibility and invalid-output behavior require tests against the actual provider. Explicit slash commands continue through the deterministic parser.

Logical shape:

```json
{
  "schema": "atm-chart-intent/1",
  "intent": "chart | interpretation | data_extract | not_chart | ambiguous",
  "series_id": "registered-series-id or null",
  "chart_kind": "line or null",
  "response_mode": "chart_only | chart_with_interpretation | data_extract | none",
  "language": "en | ro | other"
}
```

This block is illustrative, not a ready-to-use literal schema: the actual schema must use JSON Schema enums for the current catalog IDs and must not permit explanatory prose, additional properties or arbitrary strings where enums are possible.

Validation rules:

- Exactly one of the registered `series_id` values, or null where the intent does not select a series; no free-form source name is accepted as identity.
- The only kind in the first generic slice is `line`. The catalog and validated application path must both authorize it.
- `intent`, `response_mode` and `series_id` must be mutually consistent. An ambiguous/malformed/incompatible proposal cannot build a chart.
- The parser rejects unknown keys, missing required keys, wrong types, invalid enum values and truncated/invalid JSON. It does not try to repair a malformed numeric/source payload because no such payload belongs in the schema.
- After schema validation, application policy independently checks the original user request, series grounding, repository scope, catalog membership, source qualification and chart-only versus interpretation/data-extract semantics. Membership in the catalog does not prove relevance to the request.
- A proposal that selects a registered series for a different or unsupported concept must be rejected before source reconstruction or widget creation. For example, an unsupported `population over time` request must not become a GISTEMP/EI/BNR chart just because the model returns one of their IDs.
- If the model cannot satisfy the schema or its selection cannot be grounded, an explicit slash command remains usable; the natural-language request must fail closed or ask for clarification rather than falling back to any eligible series.
- English/Romanian aliases, explicit chart commands, source ambiguity, extra interpretation requests, unregistered concepts with a valid-but-unrelated proposed ID, and explicit table/year/value requests require deterministic regression cases. Intent accuracy must be evaluated on a frozen request set, not claimed from one successful prompt.

Structured generation is a parsing aid only. It does not replace source qualification or authorization checks. The raw model response is not scientific evidence and must not be persisted as chart data.

## 5. Generic ChartSpec and compatibility boundary

The initial generic route should accept exactly one independently qualified series and LINE. It should not expose multi-series comparison, SCATTER, STEP, BAR, derived series, arbitrary filters or transformations. A later capability requires its own contract and qualification matrix.

Use a new ChartSpec schema/identity namespace (provisionally `atm-chart-spec/4`) for the generic route. Do not rewrite v1/v2/v3 serialization, IDs or historic recipes. The generic constructor must require a qualified result tied to the catalog/adapter admission path; it must not accept caller-created point arrays or treat every syntactically valid `AtmSeriesContract` as chart-eligible. If an opaque source-qualified wrapper is needed to prove that construction path, add it instead of weakening the contract.

The ChartSpec identity must bind the catalog/series profile versions, source/admission/reconstruction policy, exact pinned snapshot and source-bundle identity, qualified series identity, chart kind, coordinate/observation semantics, units/dimensions, missing policy and display profile/version. All materialized fields must be revalidated on rebuild. Existing GISTEMP v1, Energy Institute v2 and BNR v3 routes/identities stay intact until a separately tested migration is approved; the generic constructor must not redirect old History rows to new behavior.

The first generic chart should have an explicit renderer/Data projection policy for its series. Reusing a renderer is allowed only after proving its admitted axes, ticks, bounds, gaps and exact-decimal-to-display projection fit that series. Source-specific special cases may remain behind their qualified adapters; the Application must not grow a new hardcoded `if (series_id == X)` chain for each future dataset.

## 6. Sparse records and source-provenance requirements

A CSV/TSV row is not automatically a complete multi-variable observation. For each registered series the adapter must define which source column is authoritative, what an empty token means in that particular source, whether the source is primary, processed, simulated or derived, and what coverage is actually supported.

Specifically, the EWD `empirical_model_inputs_2026-08-28.csv` row containing `year=1960`, a numeric `population` value and empty `food_production_index`, `fossil_co2_proxy` and `industrial_output` cells must not be promoted wholesale into four complete series. The empty fields must not become zeros or be filled from the model's prose. Nor does the presence of a population value alone prove that this processed/model-input file is the primary source for a population trajectory. The proposed series must first be traced to a specific publisher/source and an independently admitted numeric artifact with defensible units, coordinate semantics, completeness and provenance. Until that qualification exists, those columns remain ineligible.

For every admitted series, the Chart/Data view should make the following distinction clear where applicable:

- **Original/publisher source:** the primary source material or official series page and what it actually publishes.
- **Pinned artifact used by AtM:** exact processed or extracted file/bundle from the admitted snapshot, with digest-bound provenance.
- **Reproduction scope:** whether AtM faithfully reconstructs that pinned artifact, and whether it has or has not independently reproduced upstream processing.
- **Observation meaning:** subject, measure, unit/dimension, period/frequency, missingness/discontinuity rules and whether the observations are empirical, simulated, inputs or derived.

A primary-source URL cannot be represented as proof of a pipeline audit. If source semantics or lineage are unresolved, do not register the candidate merely to make a natural-language request appear to work.

## 7. Required test matrix

### Catalog

- Every entry has a stable ID and a matching deterministic adapter; duplicate IDs, duplicate aliases, invalid profile names, invalid schema versions and conflicting metadata fail.
- Unknown/unreviewed series, source paths and snapshots cannot be selected, even if the model returns a plausible-looking ID or the CSV contains numeric cells.
- Altered, missing, oversized, swapped or mixed source files reject before series construction; identity-string matches cannot override byte mismatches.
- Catalog-to-adapter mismatch in units, subject/attribute, X kind, frequency, coverage, missing policy, source role or profile version fails closed.
- Provenance presents both the actual pinned artifact and original/publisher source without claiming unperformed raw-to-processed reproduction.

### Intent and routing

- Existing `/chart gistemp`, `/chart energy-institute` and `/chart bnr-bls-c01` remain backwards-compatible.
- Natural-language intent recognizes a chart request independently from choosing its series; unknown or ambiguous series do not silently become a chart.
- English and Romanian cases cover supported aliases, a single series, aliases shared by two candidates, requests for interpretation, explicit values/table requests, unsupported kinds and unsupported transformations.
- False-positive tests send an unsupported concept (including `population over time`) with each valid catalog ID as the model proposal; every unrelated proposal must be rejected without constructing a chart.
- A request mentioning a registered series without actually asking for a chart must not be converted to a chart-only response.
- Invalid/truncated/non-JSON provider output, schema mismatch, unknown keys/IDs and unavailable structured-output support fail safely.
- No model-provided coordinates, values, units, axes, paths or provenance can enter ChartSpec or Data.
- Chart-only responses are deterministic and do not contain partial retrieval tables, escaped/internal serialized intent or duplicate chart-format output.

### Series, ChartSpec, rendering and History

- Source identity, source-decimal tokens, canonical numeric representation, point support and authorized missing points survive the full adapter → ChartSpec → Data path unchanged.
- MISSING is displayed as missing, not a hidden numeric sentinel or zero; lines break exactly where the registered profile requires.
- ChartSpec v1/v2/v3 IDs and archived recipes remain byte-for-byte compatible under regression fixtures.
- Generic v4 reconstruction rejects altered recipe identity, catalog/profile mismatch, unavailable pinned bytes, tampering and unsupported display/coordinate policy.
- Save-to-History and fresh-process reconstruction use the same approved series identity, ChartSpec identity, exact Data and selected-row provenance; recipes contain no stored point arrays or pixel output.
- Transcript response, chart widget, failure status and Data view appear once in the right places. Capture request classification, raw provider response, normalized visible answer, widget admission/render result and History outcome for the regression harness; the outstanding escaped/special-format symptom remains unverified until reproduced and asserted.
- Existing source-specific routes, GISTEMP/EI/Bnr tests, ASan/UBSan, GTK E2E, invariant registry, Flatpak/Meson and durability/reclamation suites stay green.

## 8. Proposed implementation and release sequence

This is a dependency proposal, not release metadata and not evidence that any gate has passed. Keep the release labels `planned`, `implemented`, `tested`, `qualified`, `integrated`, `released` and `verified-in-use` separate.

**Release-number conflict requiring reconciliation:** current issue #481 and draft metadata PR #483 still designate the next release as v0.8.0 containing the bounded BNR route. The roadmap proposal below moves that already qualified route/provenance scope to v0.7.2 and reserves v0.8.0 for the first generic charting capability. This proposal has not been reflected in release metadata or issue #481/#483 and must not be treated as a completed release decision. Keep #483 draft and unmerged; do not change version metadata until the exact main artifact has passed on-device acceptance, administrator-side branch protection #135 is verified, and the release sequence is reconciled with the maintainer's reviewed decision.

| Step | Proposed scope | Exit gate |
| --- | --- | --- |
| Before the next publication | Keep PR #483 unmerged. Accept the latest main-only bundle on elementary OS 8, verify branch protection #135, then reconcile issue #481 and metadata PR #483 with the approved release sequence. Do not change release metadata while acceptance/protection gates remain open. | Exact artifact is manually checked; branch protections are demonstrably active; candidate is rebased/re-scoped to the reviewed release and all required exact-head CI is green. |
| v0.7.2 proposal (not yet synchronized to metadata) | Publish only the already qualified source-specific BNR C01 route and truthful Chart/Data provenance. Do not advertise generic charting. | Exact metadata-bearing bundle passes release workflows; live/History tests cover 2025-Q2 as MISSING; primary-source and pinned processed-artifact labels are manually checked. This scope may only be assigned v0.7.2 after explicit release-plan reconciliation. |
| v0.7.3 proposal | Implement and test the static eligible-series catalog, initially mapping the existing three qualified adapters. | No Application behavior widens yet; catalog integrity, denied unknowns, alias ambiguity, adapter mapping and source provenance tests pass. |
| v0.7.4 proposal | Add the bounded intent schema and catalog-driven routing decision, preserving explicit commands. | Frozen EN/RO intent set, invalid-output, ambiguity, unsupported-series and backward-compatibility tests pass on the configured local Ollama/provider path. |
| v0.7.5 proposal | Add generic source-qualified `AtmSeriesContract` → ChartSpec v4 construction for one admitted LINE series. | Constructor rejects caller arrays/unregistered contracts; identities, exact numeric support and v1/v2/v3 compatibility tests pass. |
| v0.7.6 proposal | Route a catalog-selected series through the generic native Chart/Data integration rather than a growing source-specific Application dispatch list. | GTK route, fail-closed status, exact Data/provenance, resize and current chart-only UX regressions pass. |
| v0.7.7 proposal | Add generic recipe persistence and catalog-bound History requalification. | Store migration stays atomic; previous recipes reconstruct unchanged; generic recipe rebuilds from pinned source in a fresh process and rejects tampering. |
| v0.8.0 proposal | First release that may claim generic charting, limited to registered/qualified single-series LINE profiles. | Complete release gate on the final metadata commit, published asset/tag verification and manual elementary OS 8 acceptance of that exact artifact. |
| v0.8.x proposals | Add one capability at a time: coordinate kinds, qualified missing/event semantics, additional chart kinds or registered transforms. | Each capability has a written contract, source profile, identity/version policy, positive/negative tests and no regression of existing chart/history identities. |
| v0.9.0 proposal | Freeze the approved capability matrix and perform full hardening and end-to-end acceptance. | Every row of the capability matrix is explicitly qualified or excluded, full test matrix passes and manual installed-artifact checks are recorded. |
| v1.0.0 proposal | Stable public contract only after the v0.9.0 gates pass. | No open release-blocking correctness, persistence, provenance, security, or acceptance issue; immutable artifact and documented API behavior agree. |

Do not merge this proposal by interpreting a green docs-only CI run as implementation of catalog, intent, generic ChartSpec, rendering or History. Those remain separate code slices and need their own exact-head evidence. The release proposal must be reconciled with the final artifact/acceptance state before changing any version metadata.

## 9. Current non-goals

- No automatic charting of every repository CSV/TSV column.
- No arbitrary filesystem path, URL, repository or snapshot supplied by the model.
- No general multi-series comparisons, derived numeric series, interpolation, smoothing, silent unit conversion, dual/log axes, STEP/BAR, categorical or event plots in the first generic slice.
- No closing of CHART-01/#343 merely because the catalog and a first generic route exist.
- No reopening the optimization program through D1; it remains independent, session-only and default OFF.
- No claim that a bundle was manually accepted on elementary OS 8 unless the exact artifact was launched and checked there.
