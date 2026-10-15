# CHART-06 BNR BLS C01 Application route

Status: the bounded user-facing route is integrated in development `main` by PR chain #471–#479; final merge commit #479 is `91093e7c96be56703052a3eb30565d1c84c351e5`. It is not part of public v0.7.1 and does not declare v0.8.0 released.

## Explicit entry points

The application admits the command `/chart bnr-bls-c01` and unambiguous visualization requests that identify BNR BLS C01 directly or name the credit-standards subject. Generic measure terms such as “net percentage” alone do not identify this series and must not dispatch it. Comparative or multi-series requests, including recognized additional-indicator cues such as GDP/PIB, unemployment, or inflation, are left unqualified rather than silently rendering only C01. Requests for derived profiles such as standard deviation, moving average, forecast/projection, or correlation are also unqualified; the route does not calculate derived series. The route is source-specific, not a promise of general charts for RMD or arbitrary RMD datasets.

## Source admission

RMD must be included in the conversation's repository scope and the active repository pin must be the exact source snapshot `e43eb4e899f123d3b1aa0f6e20b7c8ed35434170` from `LaurentiuStaicu/romanian-monetary-dynamics`. The application rebuilds the five-file source panel, finalized SRA, SeriesContract, VerifiedSeries and ChartSpec v3 before displaying the chart. A different snapshot, missing repository, missing source file, invalid identity or changed source seal fails closed.

## Chart and Data semantics

The route produces only the qualified quarterly C01 profile: twelve ordered quarter coordinates from `2022-Q4` to `2025-Q3`, eleven numeric observations, and the authorized `2025-Q2` MISSING coordinate. The native chart uses the separately qualified bounded projection and renderer, keeps the missing quarter's X position, starts a new line segment across the gap, and does not invent a point or marker for absence. The machine-readable chart Data payload preserves exact source decimals, numeric physical CSV rows, qualified identities, support IDs, source snapshot/path and the exact `source_round_not_retained` reason for validation and reconstruction. The visible Data table is a concise projection of quarter, status and net percentage, without a physical-row column. Selected-observation details show the readable source/repository and explain that 2025-Q2 was not retained by the source round, has no numeric value, and is not zero; internal IDs, hashes, paths and support identities are not shown by default.

## Durability and History

Conversation-store schema v5 persists the qualified ChartSpec v3 reconstruction recipe only; it does not persist numeric point state, projected coordinates or Cairo pixels. History reloads the pinned RMD source bytes and independently rebuilds the source-bound chain, checking the stored chart, scientific-content and qualified-series identities. When the source bundle or its seal is altered, History refuses the chart and does not show Chart/Data as valid.

## Qualification and remaining release gates

The PRES-09 scenario on the exact PR heads drives the live composer through Energy Institute v2, GISTEMP v1 and BNR C01 v3; verifies all three Data views and the BNR missing-quarter provenance; archives the conversation; restarts the app process; reconstructs all recipes; and tampers with qualified EWD/RMD source files to confirm that Chart/Data become unavailable. All exact-head workflow runs inspected for #471–#479 passed. The final metadata-bearing release commit must still pass the full release gate.

Before any v0.8.0 publication, verify the exact generated Flatpak, tag and release against the repository's release contract and manually accept that exact artifact on elementary OS 8. Test live BNR rendering and its 2025-Q2 gap, existing GISTEMP/Energy Institute routes, Data provenance, Archive/save, separate-process History reconstruction and fail-closed behavior after source tampering. The public v0.7.1 release remains unchanged until a new release is explicitly prepared and published.
