# CHART-02 Energy Institute chart and History v2 proposal

**Status: design proposal only, 2026-10-12. No renderer, application route, persistence migration, or release change is authorized by this document.** CHART-01 Gate B has qualified a dormant source-bound Energy Institute series candidate. The remaining gap is the versioned, downstream chart/History contract.

## Findings confirmed on main

1. `AtmEnergyInstituteVerifiedSeries` is now a source-specific opaque wrapper over an independently revalidated `AtmSeriesContract`. It reconstructs the pinned EWD processed snapshot, the 65-artifact Evidence collection, the finalized SRA and the 61-point series. It is not connected to chart construction or presentation.
2. `src/chart_spec.h` accepts only `AtmVerifiedSeries`; `src/chart_spec.c` owns that GISTEMP-specific type and fixes the identity to `atm-chart-spec/1`, identity domain `atm.chart-spec.v1`, and the GISTEMP series profile. Existing chart identity fixtures and persisted History rely on these IDs.
3. `src/chart_projection.c` consumes `AtmVerifiedPoint` and implements `atm-display/gistemp-linear/1`: exact-decimal conversion is limited to exponents -4 through 0 and the fixed display tick policy is quarter-degree temperature increments. EI source values use more precise source decimals (for example `149.6960972`) and the physical unit is `exajoules/year`; passing them through the current GISTEMP display conversion would fail closed and the quarter-degree scale is scientifically inappropriate.
4. `src/chart_history_reconstruct.c` exposes GISTEMP-only live/History reconstruction functions with an exact source path, admission profile, repository snapshot allowlist and reconstruction profile. The current code reconstructs GISTEMP from pinned source bytes and compares identities; it does not trust stored plotted points.
5. Conversation store v3 and its migration explicitly allow only `atm-chart-spec/1`, `atm-chart-reconstruct/gistemp-complete-annual/1`, `atm-series/gistemp-complete-annual/1`, `atm-gistemp-pinned-admission/1` and the GISTEMP source path. The runtime validator in `src/conversation_store.c` independently hard-codes the same single-source tuple. These are real allowlist barriers, not merely UI limitations. Supporting a second chart contract therefore requires a qualified store migration; changing only Vala metadata would fail closed at persistence.

6. The two EWD snapshot candidates are not interchangeable bundles. Comparing file blobs at EI admission snapshot `d9e249339663015f6d1c05752338a955bf64ad0b` and snapshot `dc710d8e604c0fff713a3acfdb18ab47a1a902bb` (accepted as current by the existing GISTEMP History path) shows that the EI CSV and provenance file have identical Git blob IDs at both commits, but `science/data/input_manifest.json` and `science/data/registry.csv` have different blob IDs. The current EI admission policy pins all four files for `d9e249…`; it must not be presumed to accept the changed manifest/registry pair at `dc710d…`. Matching two of four files is not admission of the other two.

## Decision recommended

**Keep the published GISTEMP chart contract byte-for-byte stable and introduce a separately versioned EI chart contract.** Do not widen v1's identity serialization, overwrite the pinned GISTEMP identity fixture, reinterpret temperature display rules, or make arbitrary `AtmSeriesContract` values chart-admissible.

The proposed contract permits only this reviewed source/profile pair initially:

- Existing GISTEMP v1: `atm-chart-spec/1` + `atm-chart-reconstruct/gistemp-complete-annual/1` + `atm-series/gistemp-complete-annual/1` + `atm-gistemp-pinned-admission/1`. It retains every current identity, source allowlist, UI behavior and History reconstruction rule.
- Proposed EI v2: `atm-chart-spec/2` + `atm-chart-reconstruct/energy-institute-tes-complete-annual/1` + `atm-series/energy-institute-tes-complete-annual/1` + `atm-energy-institute-pinned-admission/2`. It binds only the exact EWD commit and four-file admission already qualified at CHART-01, the TES / Physical Energy Content semantic interpretation, the series qualified ID and the finalized SRA qualification. It does not assert upstream raw-to-processed reproduction.

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

## History and durable persistence

The store's current v3 schema and native validator are intentionally GISTEMP-only. The EI v2 feature needs a reviewed **conversation-store schema v4** rather than relaxing those checks in place.

Migration requirements:

1. Add an atomic v3-to-v4 migration and a matching fresh v4 schema. Rebuild the chart tables as necessary to change their versioned CHECK allowlists. Preserve all existing archived/unarchived conversations and GISTEMP chart rows exactly; rollback on any migration failure. Keep v1/v2/v3 schema fixtures and migration tests.
2. The new constraints admit only the two complete schema/profile tuples above, initially with exactly one series per chart. They must not admit arbitrary repository paths, sources or profile cross-products; source IDs/digests remain fixed by the appropriate source admission.
3. Update `chart_input_is_valid` and the persistence bridge with a discriminated v1/v2 tuple validator. GISTEMP values continue to use the existing v1 tuple; EI values use only the exact v2 tuple and existing repository snapshot pins. Chart storage continues to contain identity/provenance metadata, not plotted numeric points as authority.
4. Add a separate EI History reconstruction entry point (or a dispatcher with exact versioned handlers) that reads the four files from the conversation's pinned EWD snapshot, verifies repository/version/SHA and admission, rebuilds Evidence/SRA/series/ChartSpec v2, and compares every persisted identity/profile/source field. It must reject missing files, unsupported snapshots, byte or profile mutations and forged/re-hashed identifiers.
5. Existing GISTEMP History tests must retain their pinned identities and success/failure cases. New migration and EI round-trip tests must prove old rows still reconstruct identically and EI rows are reconstructible solely from the pinned source bundle. Stored display values, exported JSON and model output must not be treated as series authority.

## Application boundary

Do not add a user-facing EI chart intent or new live/History renderer caller in the ChartSpec/persistence foundation PR. First qualify native chart construction and the separate EI display projection, then the store-v4 round trip. Only a later application PR may connect the approved EI source route to GTK, after it proves that:

- live and History use the same reconstructed ChartSpec v2 identity;
- the chart and complete exact Data view agree point-for-point;
- resize/cache updates preserve previous valid state on failure;
- GISTEMP's pinned IDs, rendered behavior and History rows remain unchanged;
- no LLM-created numeric values, arbitrary source access, point persistence or unqualified transformations enter the path.

## Ordered implementation gates

- **C2a — native ChartSpec v2:** source-bound constructor, tagged identity, neutral point accessors and negative/reconstruction tests; no renderer or persistence change.
- **C2b — EI display profile:** deterministic bounded display conversion and axis policy with ASan/UBSan, precision/range/overflow and resize/fallback tests; preserve GISTEMP projection bit-for-bit.
- **C2c — conversation-store v4:** atomic migration, exact tuple allowlist and fail-closed compatibility; preserve all existing chart rows.
- **C2d — EI History reconstruction:** exact pinned-source rebuild and identity comparison; no stored points as authority.
- **C2e — application integration:** live intent, GTK chart and History round trip only after C2a–C2d are green and reviewed.

Each gate has its own exact-head CI and acceptance evidence. None implies the others. No release is inferred from native-series qualification or from this proposal. The immutable v0.6.3 artifact remains unchanged.
