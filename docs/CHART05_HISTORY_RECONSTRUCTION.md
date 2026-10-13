# CHART-05: qualified History chart reconstruction

**Current status (2026-10-13):** saved chart recipes are reconstructed from their admitted source snapshots. GISTEMP uses schema v1; Energy Institute uses schema v2 via PRs #442/#443. PRES-09's GTK Application live-to-History qualification is integrated via PR #447. Stored numeric points, projected coordinates and pixel/layout state are never chart authority.

## Reconstruction authority

When an archived conversation is reopened, the application resolves its persisted recipe against the conversation's pinned repository identity and snapshot SHA, resolves the immutable local snapshot directory, re-reads the pinned source files, reruns source admission and the source-specific Evidence/SRA/series reconstruction, rebuilds the appropriate ChartSpec, compares the rebuilt scientific, qualified and ChartSpec identities with the persisted recipe, and creates the GTK Chart/Data view only after every gate succeeds.

Malformed recipes, missing bytes, unsupported snapshots, cross-bundle file mixes or identity mismatch fail closed. AtM does not fall back to persisted values or ask the model to supply replacements.

## GISTEMP v1 reconstruction

The legacy route retains the existing schema and identity contracts:

- schema: `atm-chart-spec/1`;
- series profile: `atm-series/gistemp-complete-annual/1`;
- admission profile: `atm-gistemp-pinned-admission/1`;
- reconstruction profile: `atm-chart-reconstruct/gistemp-complete-annual/1`;
- source path: `science/data/processed/nasa_gistemp_global_2026.csv`;
- source semantics: complete annual global temperature anomaly, 1880–2025.

GISTEMP's existing source-bound reconstruction and presentation strings are preserved; its recipe still contains identities and provenance rather than numeric chart points.

## Energy Institute v2 reconstruction

C2d/C2e uses an independent, source-specific tuple:

- schema: `atm-chart-spec/2`;
- reconstruction profile: `atm-chart-reconstruct/energy-institute-tes-complete-annual/1`;
- series profile: `atm-series/energy-institute-tes-complete-annual/1`;
- admission profile: `atm-energy-institute-pinned-admission/3`;
- source path: `science/data/processed/energy_institute_global_2026.csv`;
- metric: global Total Energy Supply (TES), Physical Energy Content interpretation;
- coverage: 61 consecutive annual observations, 1965–2025;
- unit: exajoules/year; source data rows: 2–62.

Admission permits exactly two complete four-file EWD snapshots: `d9e249339663015f6d1c05752338a955bf64ad0b` and `dc710d8e604c0fff713a3acfdb18ab47a1a902bb`. The CSV alone does not authorize a bundle; the corresponding provenance JSON, input manifest and registry are also re-read and checked. Unknown snapshots and cross-snapshot mixtures fail closed.

The durable conversation repository pin retains the EWD release version `0.1.0` from `CITATION.cff`. The EI v2 series recipe separately records `repository_version=snapshot:<snapshot_sha>` because reconstruction binds the immutable four-file source bundle. These identities are deliberately different: store validation keeps exact release-version equality for GISTEMP v1 and requires the snapshot recipe version plus matching repository ID/SHA for EI v2.

History recomputes Evidence/SRA, the source-qualified EI series and ChartSpec v2; the stored recipe only supplies the expected identities/profiles. Data view preserves source decimals and point provenance. This remains a claim about faithful reconstruction of the admitted EWD processed snapshot, **not** independent reproduction of Energy Institute's raw-to-processed pipeline. No EROI/net-energy transform, per-capita conversion or World3 resource-stock interpretation is authorized.

## Presentation and verification boundary

The Chart/Data view is a Presentation child, not transcript/provider history and not semantic clipboard text. On successful reconstruction it provides the exact source observations and provenance; failed reconstruction produces an unavailable-chart status instead of substituting stored or model-generated values.

Qualification includes the native History reconstruction tests for both admitted EI snapshots, identity/tamper rejection, conversation-store attachment/semantic validation, GTK Data/provenance tests, resize tests, ASan/UBSan, Invariant Registry and Flatpak. The exact C2e candidate head `a82003b78042b129797f6f2851c79e3de282ee5b` passed all nine workflows; post-merge Flatpak and Invariant checks also passed for PR #443.

**PRES-09 end-to-end status:** the missing GTK Application gate is now qualified by PR #447. The test drives the actual live EI v2 and GISTEMP v1 composer/send paths, invokes Save to History, reopens the archived conversation in a separate process, compares reconstructed ChartSpec identities, exact Data and selected-row provenance, and verifies fail-closed behavior after source-manifest tampering. All 12 exact-head workflow runs passed on candidate `f4c5c879fed905556bd7bc77c67c4a93735770b6`.

This does not imply new-release approval or broader chart semantics. The immutable v0.6.3 artifact remains unchanged. General missing/discontinuity/event semantics, multi-series composition, arbitrary adapters and independent Energy Institute raw-to-processed reproduction remain outside the qualified path; the wider CHART-01 issue #343 stays open.
