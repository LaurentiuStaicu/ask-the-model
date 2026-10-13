# CHART-02 Energy Institute chart and History v2 proposal

**Status: implementation record, updated 2026-10-13. C2c conversation-store v4 and C2d native EI History reconstruction are integrated into `main`; C2e application integration is under development in a separate branch/PR. This stage adds source-bound live intent, rendering/Data view, durable chart recipe and History reconstruction, but does not change the release/version.** CHART-01 Gate B qualified a dormant source-bound Energy Institute series candidate; this record tracks the versioned chart/History contract and its gated implementation.

## Initial baseline findings (historical)

The following notes record the initial CHART-02 baseline, before C2a–C2d were implemented. They explain the original design decisions; they are not a description of the current implementation. Completed and pending gates are tracked below.

1. At the initial baseline, `AtmEnergyInstituteVerifiedSeries` was a source-specific opaque wrapper over an independently revalidated `AtmSeriesContract`. It reconstructed the pinned EWD processed snapshot, the 65-artifact Evidence collection, finalized SRA and 61-point series, but was not then connected to ChartSpec construction or presentation. C2a later introduced the source-bound ChartSpec v2 constructor.
2. At that baseline, `src/chart_spec.h` accepted only `AtmVerifiedSeries`, and `src/chart_spec.c` fixed chart identity to `atm-chart-spec/1`, domain `atm.chart-spec.v1`, and the GISTEMP series profile. Existing chart identity fixtures and persisted History relied on those IDs; C2a preserved v1 while adding a separate v2 contract.
3. The GISTEMP path in `src/chart_projection.c` implements `atm-display/gistemp-linear/1`: exact-decimal conversion is limited to exponents -4 through 0 and its fixed tick policy uses quarter-degree temperature increments. EI source values use more precise decimals (for example `149.6960972`) and `exajoules/year`, so reusing that GISTEMP projection would fail closed or apply an inappropriate scale. C2b therefore qualified a separate EI display profile.
4. At the initial baseline, `src/chart_history_reconstruct.c` exposed only GISTEMP live/History reconstruction functions, with an exact source path, admission profile, repository snapshot allowlist and reconstruction profile. C2d adds a separate native EI reconstruction entry point, documented below; application History dispatch remains out of scope.
5. At the initial baseline, conversation store v3 and its migration allowed only `atm-chart-spec/1`, `atm-chart-reconstruct/gistemp-complete-annual/1`, `atm-series/gistemp-complete-annual/1`, `atm-gistemp-pinned-admission/1` and the GISTEMP source path. Those were real allowlist barriers. C2c subsequently integrated the qualified schema-v4 migration and discriminated validator; changing only Vala metadata would still fail closed.

6. The two EWD snapshot candidates are distinct four-file bundles. The initial C2-0 review correctly found that the EI CSV and provenance share Git blob IDs across snapshots `d9e249339663015f6d1c05752338a955bf64ad0b` and `dc710d8e604c0fff713a3acfdb18ab47a1a902bb`, while `science/data/input_manifest.json` and `science/data/registry.csv` differ. C2c subsequently qualified both complete bundles under admission profile `/3`, with separate SHA-256 pins for all four files per snapshot. The current policy accepts those two exact bundles, not arbitrary combinations; C2d reconstruction rechecks the full bundle.

## Decision and current contract

**Keep the published GISTEMP chart contract byte-for-byte stable and introduce a separately versioned EI chart contract.** Do not widen v1's identity serialization, overwrite the pinned GISTEMP identity fixture, reinterpret temperature display rules, or make arbitrary `AtmSeriesContract` values chart-admissible.

The current contract permits only these reviewed source/profile tuples:

- Existing GISTEMP v1: `atm-chart-spec/1` + `atm-chart-reconstruct/gistemp-complete-annual/1` + `atm-series/gistemp-complete-annual/1` + `atm-gistemp-pinned-admission/1`. It retains every current identity, source allowlist, UI behavior and History reconstruction rule.
- EI v2 current contract: `atm-chart-spec/2` + `atm-chart-reconstruct/energy-institute-tes-complete-annual/1` + `atm-series/energy-institute-tes-complete-annual/1` + `atm-energy-institute-pinned-admission/3`. The v4 store allowlist and admission policy support exactly snapshots `d9e249339663015f6d1c05752338a955bf64ad0b` and `dc710d8e604c0fff713a3acfdb18ab47a1a902bb`, each pinned to its own complete four-file SHA-256 bundle and qualified series identity. EI v2 binds the TES / Physical Energy Content interpretation and finalized SRA qualification; it does not assert upstream raw-to-processed reproduction.

These are an exact allowlist of compatible tuples, not freely combinable metadata. Unknown combinations, schema versions, sources, profiles, series counts and reconstruction handlers must fail closed. The initial EI chart is one series; multi-series composition is not part of this gate.

## ChartSpec and shared point-access contract (C2a)

C2a preserves the existing `atm_chart_spec_new()` behavior for legacy GISTEMP callers and persisted `atm-chart-spec/1` objects, while adding an explicit, source-bound EI v2 constructor that accepts `AtmEnergyInstituteVerifiedSeries`, not a caller-authored contract or point array.

A tagged opaque `AtmChartSpec` may carry the GISTEMP-v1 or EI-v2 source owner internally. Both paths expose read-only source-neutral chart accessors for X, canonical Y coefficient/exponent, point status, and exact qualified-series identity. Existing GISTEMP accessors may remain as compatibility APIs for v1, but the generic projection path must not cast an EI object to `AtmVerifiedSeries`. Construction must independently rebuild the appropriate source series before it owns the inputs, and validation must reconstruct the expected spec and compare all fields and IDs.

Identity rules:

- GISTEMP v1 follows the existing serializer, field order/types, domain and pinned fixtures unchanged.
- EI v2 uses a new schema and content-ID domain. Bind chart kind; source-specific series qualified ID and profile; X kind/unit/dimension and coverage; Y unit/dimension and semantics; missing policy; display-profile ID/version; exact data-derived extents; and the chart's series ordering. The series qualified ID must already bind the full EI SRA qualified identity and source support. A chart ID or an SRA ID alone is not evidence of a reconstructed chart.
- Keep the exact source decimal/canonical decimal in the owned spec; pixel projection is a display approximation, never a scientific transformation or replacement for source values.

The chart type remains deny-by-default. The source-specific EI constructor is the only production route to an EI-v2 spec in this first slice; the dormant generic contract constructor is not a chart admission API.

## Separate Energy Institute display contract (C2b)

C2b keeps EI values out of `atm-display/gistemp-linear/1` and qualifies a separate display profile. The profile specifies, in bounded integer/canonical-decimal terms:

- accepted decimal exponents and magnitude/overflow limits for this exact TES series profile;
- a deterministic display-only quantization/conversion rule adequate for source tokens whose canonical exponents extend beyond -4;
- linear Y-domain padding and readable EJ/year tick spacing for the bounded 1965–2025 values;
- X tick density and layout caps, maximum points/size, finite coordinates and resize transaction behavior;
- deterministic behavior for unsupported values and fallback rendering without corrupting the complete exact Data view.

This is presentation policy only. It must not claim that rounded pixel values are source values, affect SRA facts, change the stored exact decimals or authorize transformations to net energy, EROI, fossil share or World3 stocks. Missing/discontinuity semantics remain a separate CHART-01 gate; EI v1 profile accepts its complete numeric source only and does not silently interpolate or connect unknown gaps.

## Historical admission decision

The initial design draft recommended admission profile `/2` for `d9e249…` and withheld `dc710d…` until its manifest and registry were separately qualified. C2c's v4 contract superseded that draft by introducing `atm-energy-institute-pinned-admission/3`, which pins and admits the complete four-file bundle for each of the two listed snapshots. Profile `/2` is historical design context, not the current persistence or C2d contract.

## C2c integrated into main — atomic conversation-store v4

C2a and C2b remain integrated as dormant native contracts. C2c was merged into `main` on 2026-10-13 (merge commit `9c50ffac42dc4b2d1d36111451423aac4d5770d1`) and provides the persistence compatibility boundary without connecting an EI renderer or History handler.

The v4 fresh schema and v3-to-v4 migration allow exactly two chart tuples: GISTEMP ChartSpec v1 with its existing reconstruction/series/admission profiles, and EI ChartSpec v2 with `atm-chart-reconstruct/energy-institute-tes-complete-annual/1`, `atm-series/energy-institute-tes-complete-annual/1`, and `atm-energy-institute-pinned-admission/3`. The EI series row is pinned to its exact processed CSV path and one of snapshots `d9e249339663015f6d1c05752338a955bf64ad0b` or `dc710d8e604c0fff713a3acfdb18ab47a1a902bb`, with `repository_version=snapshot:<snapshot_sha>`. SQL CHECKs and tuple triggers block cross-pairings; the native API and semantic validator independently enforce the same allowlist.

The migration stages and copies only `message_charts` and `chart_series`, then swaps the tables and upgrades the installation marker inside one `BEGIN IMMEDIATE` transaction. Every existing GISTEMP row is copied verbatim; SQLite foreign-key and semantic validation run before commit. Failure rolls the transaction back to schema v3. New stores start directly at v4; v1/v2 stores first use the existing separately atomic migrations to v3 and then migrate v3 to v4. v1/v2/v3 schema fixtures remain available for migration tests.

C2c stores identity/provenance recipes only. It does not verify source truth at write time, add EI point values to SQLite, or enable EI History reconstruction. C2d remains responsible for rebuilding the source → Evidence → SRA → VerifiedSeries → ChartSpec v2 chain from the conversation's pinned snapshot and comparing every persisted field; C2e remains gated behind C2d.

## History and durable persistence — implementation status

The schema-v4 persistence requirements were implemented by C2c and integrated into `main` in PR #441 (`9c50ffac42dc4b2d1d36111451423aac4d5770d1`). C2c provides the atomic v3-to-v4 migration, matching fresh-v4 schema, exact GISTEMP/EI tuple allowlist, discriminated native validation, rollback behavior and migration fixtures. The store persists identity/provenance recipes, not authoritative chart points.

1. **C2c integrated:** the v3-to-v4 migration and fresh-v4 schema preserve existing GISTEMP rows and fail closed/roll back on migration failure; v1/v2/v3 migration fixtures remain covered.
2. **C2c integrated:** the v4 constraints admit only the two complete schema/profile tuples with one series per chart, and block arbitrary path/profile cross-products.
3. **C2c integrated:** the native validator enforces the discriminated v1/v2 tuple; the two EI snapshot versions bind to `snapshot:<snapshot_sha>` and the exact processed source path.
4. **C2d candidate:** the new native History reconstruction entry point rereads all four pinned files, checks repository/version/SHA/admission, rebuilds Evidence/SRA/series/ChartSpec v2 and compares the persisted identities. It rejects missing files, unsupported snapshots, tampered bundles and mismatched recipe metadata.
5. **Compatibility and reconstruction tests:** existing GISTEMP identity/reconstruction tests are retained; migration coverage is in C2c, while C2d covers both admitted EI bundles and negative recipe/source mutations. Stored display values, exported JSON and model output remain non-authoritative.


## Application boundary

Do not add a user-facing EI chart intent or live/History renderer caller in C2d. C2a/C2b's native contracts and C2c's v4 persistence boundary are already integrated; C2d supplies the source-reconstruction candidate. Only a separate C2e application PR may connect the approved EI route to GTK, after it proves that:

- live and History use the same reconstructed ChartSpec v2 identity;
- the chart and complete exact Data view agree point-for-point;
- resize/cache updates preserve previous valid state on failure;
- GISTEMP's pinned IDs, rendered behavior and History rows remain unchanged;
- no LLM-created numeric values, arbitrary source access, point persistence or unqualified transformations enter the path.

## Ordered implementation gates

- **C2-0 — EI snapshot/admission compatibility: resolved for the two supported snapshots.** Admission `/3` pins all four files per snapshot; native tests verify both identities and reject unsupported/mutated bundles.
- **C2a — native ChartSpec v2: integrated as a dormant native contract.** The source-bound constructor and identity tests exist; no EI renderer is enabled.
- **C2b — EI display profile: integrated as a dormant native contract.** The bounded display conversion/axis behavior is covered by existing native, sanitizer and resize checks; GISTEMP behavior remains separately qualified.
- **C2c — conversation-store v4: integrated into `main` via PR #441.** Atomic migration, exact tuple allowlist and fail-closed compatibility are implemented.
- **C2d — EI History reconstruction: integrated into `main` via PR #442**, merge commit `cd7351cc3d8de3f868e5a7ebb07233a9e5f8099a`. Exact-head CI passed on the pre-merge candidate; stored points remain non-authoritative.
- **C2e — application integration: implementation candidate under review.** Adds `/chart energy-institute`, source-bound live reconstruction, EI-specific chart/Data presentation and a v2 History dispatch that re-runs C2d reconstruction. The application persists the true repository release version (`0.1.0` from `CITATION.cff`) on the conversation pin while the chart recipe separately uses `snapshot:<snapshot_sha>`. The C2e branch adds a conditional binding rule so v1 continues to match the repository release version and v2 must match the exact snapshot recipe. Exact-head CI and History/GTK/store tests remain the acceptance gate.

Each gate has its own exact-head CI and acceptance evidence. None implies the others. No release is inferred from native-series qualification or from this proposal. The immutable v0.6.3 artifact remains unchanged.


## C2d integrated; C2e application integration candidate

C2d was merged into `main` on 2026-10-13 via PR #442, merge commit
`cd7351cc3d8de3f868e5a7ebb07233a9e5f8099a`. Its native History reconstruction
re-reads and admits the complete four-file bundle, rebuilds Evidence/SRA/the
61-point EI series/ChartSpec v2, and compares the result with persisted identities.
C2d does not trust stored values and does not by itself enable the renderer.

The current C2e candidate connects this contract to the application. It introduces
an explicit `/chart energy-institute` intent and only admits natural-language
requests when the query uniquely identifies either GISTEMP or Energy Institute.
The live route reconstructs from the pinned EWD snapshot using the same source
admission and ChartSpec v2 identity path as History. The EI renderer and Data view
present the 1965–2025 TES series in exajoules/year under the Physical Energy
Content interpretation; the exact source decimals, source rows, qualified series
identity and support identities remain available in Data. Invalid resize attempts
must retain the prior valid render and exact Data payload.

### Repository release version versus chart snapshot version

EWD's two EI-admitted `CITATION.cff` snapshots report repository release version
`0.1.0`. That value remains on the durable conversation repository pin and its
grounding citations. A ChartSpec v2 series row instead persists
`repository_version=snapshot:<snapshot_sha>`, because its reconstruction
recipe is bound to the immutable four-file SHA set. These are different
identities and must not be collapsed into one string. The C2e store attachment
and integrity validator therefore compare the release version for schema v1,
but for schema v2 require the recipe's exact admitted snapshot SHA while still
requiring the matching `ewd` repository pin. The store allowlist, snapshot
admission, profile tuple and History reconstruction remain fail-closed.

C2e acceptance requires the exact-head native/sanitizer, GTK, resize, invariant
registry and Flatpak workflows to pass; tests additionally cover both accepted
snapshot identities, exact Data/provenance rows, live-versus-History ChartSpec
identity, repository-version separation, and refusal of malformed intent,
unsupported snapshots, mutated bundles and invalid recipe metadata. C2e remains
unreleased until this review and acceptance are complete.
