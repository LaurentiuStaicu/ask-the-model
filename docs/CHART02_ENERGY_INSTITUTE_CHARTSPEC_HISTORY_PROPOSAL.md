# CHART-02 Energy Institute chart and History v2 proposal

**Status: staged design and implementation record, updated 2026-10-13. C2c's conversation-store v4 is integrated; the C2d native History reconstruction candidate is under review. No application History route, renderer, or release change is authorized by this document.** CHART-01 Gate B qualified a dormant source-bound Energy Institute series candidate; this record tracks the versioned chart/History contract and its gated implementation.

## Findings confirmed on main

1. `AtmEnergyInstituteVerifiedSeries` is now a source-specific opaque wrapper over an independently revalidated `AtmSeriesContract`. It reconstructs the pinned EWD processed snapshot, the 65-artifact Evidence collection, the finalized SRA and the 61-point series. It is not connected to chart construction or presentation.
2. `src/chart_spec.h` accepts only `AtmVerifiedSeries`; `src/chart_spec.c` owns that GISTEMP-specific type and fixes the identity to `atm-chart-spec/1`, identity domain `atm.chart-spec.v1`, and the GISTEMP series profile. Existing chart identity fixtures and persisted History rely on these IDs.
3. `src/chart_projection.c` consumes `AtmVerifiedPoint` and implements `atm-display/gistemp-linear/1`: exact-decimal conversion is limited to exponents -4 through 0 and the fixed display tick policy is quarter-degree temperature increments. EI source values use more precise source decimals (for example `149.6960972`) and the physical unit is `exajoules/year`; passing them through the current GISTEMP display conversion would fail closed and the quarter-degree scale is scientifically inappropriate.
4. `src/chart_history_reconstruct.c` exposes GISTEMP-only live/History reconstruction functions with an exact source path, admission profile, repository snapshot allowlist and reconstruction profile. The current code reconstructs GISTEMP from pinned source bytes and compares identities; it does not trust stored plotted points.
5. Conversation store v3 and its migration explicitly allow only `atm-chart-spec/1`, `atm-chart-reconstruct/gistemp-complete-annual/1`, `atm-series/gistemp-complete-annual/1`, `atm-gistemp-pinned-admission/1` and the GISTEMP source path. The runtime validator in `src/conversation_store.c` independently hard-codes the same single-source tuple. These are real allowlist barriers, not merely UI limitations. Supporting a second chart contract therefore requires a qualified store migration; changing only Vala metadata would fail closed at persistence.

6. The two EWD snapshot candidates are distinct four-file bundles. The initial C2-0 review correctly found that the EI CSV and provenance share Git blob IDs across snapshots `d9e249339663015f6d1c05752338a955bf64ad0b` and `dc710d8e604c0fff713a3acfdb18ab47a1a902bb`, while `science/data/input_manifest.json` and `science/data/registry.csv` differ. C2c subsequently qualified both complete bundles under admission profile `/3`, with separate SHA-256 pins for all four files per snapshot. The current policy accepts those two exact bundles, not arbitrary combinations; C2d reconstruction rechecks the full bundle.

## Decision recommended

**Keep the published GISTEMP chart contract byte-for-byte stable and introduce a separately versioned EI chart contract.** Do not widen v1's identity serialization, overwrite the pinned GISTEMP identity fixture, reinterpret temperature display rules, or make arbitrary `AtmSeriesContract` values chart-admissible.

The proposed contract permits only this reviewed source/profile pair initially:

- Existing GISTEMP v1: `atm-chart-spec/1` + `atm-chart-reconstruct/gistemp-complete-annual/1` + `atm-series/gistemp-complete-annual/1` + `atm-gistemp-pinned-admission/1`. It retains every current identity, source allowlist, UI behavior and History reconstruction rule.
- EI v2 current contract: `atm-chart-spec/2` + `atm-chart-reconstruct/energy-institute-tes-complete-annual/1` + `atm-series/energy-institute-tes-complete-annual/1` + `atm-energy-institute-pinned-admission/3`. The v4 store allowlist and admission policy support exactly snapshots `d9e249339663015f6d1c05752338a955bf64ad0b` and `dc710d8e604c0fff713a3acfdb18ab47a1a902bb`, each pinned to its own complete four-file SHA-256 bundle and qualified series identity. EI v2 binds the TES / Physical Energy Content interpretation and finalized SRA qualification; it does not assert upstream raw-to-processed reproduction.

These are an exact allowlist of compatible tuples, not freely combinable metadata. Unknown combinations, schema versions, sources, profiles, series counts and reconstruction handlers must fail closed. The initial EI chart is one series; multi-series composition is not part of this gate.

## ChartSpec and shared point-access design

Maintain the current `atm_chart_spec_new()` behavior for legacy GISTEMP callers and persisted `atm-chart-spec/1` objects. Add an explicit, source-bound EI v2 constructor that accepts `AtmEnergyInstituteVerifiedSeries`, not a caller-authored contract or point array.

A tagged opaque `AtmChartSpec` may carry the GISTEMP-v1 or EI-v2 source owner internally. Both paths expose read-only source-neutral chart accessors for X, canonical Y coefficient/exponent, point status, and exact qualified-series identity. Existing GISTEMP accessors may remain as compatibility APIs for v1, but the generic projection path must not cast an EI object to `AtmVerifiedSeries`. Construction must independently rebuild the appropriate source series before it owns the inputs, and validation must reconstruct the expected spec and compare all fields and IDs.

Identity rules:

- GISTEMP v1 follows the existing serializer, field order/types, domain and pinned fixtures unchanged.
- EI v2 uses a new schema and content-ID domain. Bind chart kind; source-specific series qualified ID and profile; X kind/unit/dimension and coverage; Y unit/dimension and semantics; missing policy; display-profile ID/version; exact data-derived extents; and the chart's series ordering. The series qualified ID must already bind the full EI SRA qualified identity and source support. A chart ID or an SRA ID alone is not evidence of a reconstructed chart.
- Keep the exact source decimal/canonical decimal in the owned spec; pixel projection is a display approximation, never a scientific transformation or replacement for source values.

The chart type remains deny-by-default. The source-specific EI constructor is the only production route to an EI-v2 spec in this first slice; the dormant generic contract constructor is not a chart admission API.

## Separate Energy Institute display qualification

Do not route EI values through `atm-display/gistemp-linear/1`. Introduce a separate display profile only after a deterministic conversion and axis policy is reviewed. It must specify, in bounded integer/canonical-decimal terms:

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
- **C2d — EI History reconstruction: implemented candidate, exact-head CI passed for code/test head `66791e3be921fa721357e2eebbc2c0f1f5304efd`.** Review and integration remain pending; no stored points are treated as authority.
- **C2e — application integration: gated.** Live intent, GTK chart and History round trip remain out of scope until C2d is reviewed and merged.

Each gate has its own exact-head CI and acceptance evidence. None implies the others. No release is inferred from native-series qualification or from this proposal. The immutable v0.6.3 artifact remains unchanged.


## C2d implementation candidate — native History reconstruction only

The branch `chart02/ei-history-reconstruction`, rebased onto `main` after
C2c integration, adds a separate
`atm_chart_history_reconstruct_energy_institute()` entry point and native
regression coverage for both already admitted four-file snapshot fixtures.
The entry point requires the stored v2 chart/schema, reconstruction, series and
admission profiles; the exact `snapshot:<snapshot_sha>` repository version;
the fixed EI processed CSV path; the source-derived scientific and qualified
series IDs; and the rebuilt ChartSpec v2 ID. It rereads and re-admits all four
source files before rebuilding the EI Evidence/SRA/series/ChartSpec chain.
The test covers both `d9e249…` and `dc710d…` bundles, confirms that the
two qualified identities remain distinct, and rejects forged/mixed recipe
metadata and an unsupported snapshot.

This is a native reconstruction candidate only. It does not dispatch application
History rows to the new entry point, add a live EI intent, persist point values,
change GTK/rendering, or enable C2e. The branch includes the v4 persistence
boundary from `main`. All five required workflows passed on the exact code-and-test
head `66791e3be921fa721357e2eebbc2c0f1f5304efd` (CHART-01 native/ASan+UBSan,
CHART-03 GTK, CHART-04 resize, Invariant Registry and Flatpak). This documentation
update changes the head and therefore needs its own exact-head CI; review/integration
remains separate, and C2e remains gated.