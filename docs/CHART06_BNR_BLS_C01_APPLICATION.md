# CHART-06 BNR BLS C01 Application route

Status: bounded user-facing route implemented in a review candidate. The exact-head integration checks must remain green; this document is not a v0.8.0 release declaration.

## Explicit entry points

The application admits the command `/chart bnr-bls-c01` and unambiguous visualization requests that identify BNR BLS C01 directly or name the credit-standards subject. Generic measure terms such as “net percentage” alone do not identify this series and must not dispatch it. Comparative or multi-series requests, including recognized additional-indicator cues such as GDP/PIB, unemployment, or inflation, are left unqualified rather than silently rendering only C01. Requests for derived profiles such as standard deviation, moving average, forecast/projection, or correlation are also unqualified; the route does not calculate derived series. The route is source-specific, not a promise of general charts for RMD or arbitrary RMD datasets.

## Source admission

RMD must be included in the conversation's repository scope and the active repository pin must be the exact source snapshot `e43eb4e899f123d3b1aa0f6e20b7c8ed35434170` from `LaurentiuStaicu/romanian-monetary-dynamics`. The application rebuilds the five-file source panel, finalized SRA, SeriesContract, VerifiedSeries and ChartSpec v3 before displaying the chart. A different snapshot, missing repository, missing source file, invalid identity or changed source seal fails closed.

## Chart and Data semantics

The route produces only the qualified quarterly C01 profile: twelve ordered quarter coordinates from `2022-Q4` to `2025-Q3`, eleven numeric observations, and the authorized `2025-Q2` MISSING coordinate. The native chart uses the separately qualified bounded projection and renderer, keeps the missing quarter's X position, starts a new line segment across the gap, and does not invent a point or marker for absence. The Data view preserves exact source decimals, numeric physical CSV rows, qualified identities, support IDs, source snapshot/path and the exact `source_round_not_retained` missing reason. The UI explicitly states that the missing observation is not zero.

## Durability and History

Conversation-store schema v5 persists the qualified ChartSpec v3 reconstruction recipe only; it does not persist numeric point state, projected coordinates or Cairo pixels. History reloads the pinned RMD source bytes and independently rebuilds the source-bound chain, checking the stored chart, scientific-content and qualified-series identities. When the source bundle or its seal is altered, History refuses the chart and does not show Chart/Data as valid.

## Qualification and remaining release gates

The current PRES-09 scenario drives the live composer through Energy Institute v2, GISTEMP v1 and BNR C01 v3, verifies all three Data views and the BNR missing-quarter provenance, archives the conversation, restarts the app process, reconstructs all recipes, then tampers with the qualified EWD/RMD source files and confirms that Chart/Data become unavailable. Native sanitizer, GTK component, resize, persistence, invariant-registry and Flatpak build checks must pass on the latest head.

Before any v0.8.0 publication, the stacked PR chain must be integrated in dependency order and the exact published artifact must still receive explicit on-device acceptance. The separate pending acceptance of the published v0.7.1 artifact and GitHub main-branch protection remain governance tasks; neither is represented as complete by this PR.
