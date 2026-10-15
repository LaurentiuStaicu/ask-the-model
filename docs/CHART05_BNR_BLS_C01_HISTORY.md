# CHART-05 BNR BLS C01 persisted recipe and History reconstruction

Status: schema-v5 persistence and source-requalified History reconstruction were integrated by PR #476; the bounded Application dispatch was added in #477 and its safety corrections in #478–#479. This is development `main` capability, not part of public v0.7.1.

## Conversation-store schema v5

The v5 schema admits one additional exact pair: ChartSpec schema `atm-chart-spec/3` with reconstruction profile `atm-chart-reconstruct/bnr-bls-c01-calendar-quarter/1`. Its series tuple is limited to:
- repository key `rmd`;
- source snapshot `e43eb4e899f123d3b1aa0f6e20b7c8ed35434170`;
- source path `data/processed/bnr_bls_realised_rounds.csv`;
- profile `atm-series/bnr-bls-c01-calendar-quarter/1`;
- admission profile `atm-bnr-bls-canonical-panel-admission/1`;
- repository version `snapshot:e43eb4e899f123d3b1aa0f6e20b7c8ed35434170`.

As with existing GISTEMP and Energy Institute records, the store persists only the reconstruction recipe and identity/provenance tuple. It does not persist observation decimals, projected coordinates, Cairo pixels or a serialized plot.

## Migration

The v4-to-v5 migration rebuilds the constrained chart-recipe tables inside the existing transactional migration coordinator. It copies existing chart and series rows before replacing tables, recreates tuple guards and indexes, changes the installation identity/user version to 5, and then validates the whole store before commit. A failure must roll back to the valid v4 schema. Existing v1/v2/v3 stores move through the previously qualified migration steps and then v4-to-v5.

## History reconstruction

History accepts only the exact v3 C01 recipe and rebuilds it from the five source files in the immutable RMD snapshot:
- `data/processed/bnr_bls_realised_rounds.csv`;
- `data/provenance/processed_data_inventory_registry.json`;
- `model/calibration_validation/bnr_bls_canonical_panel_promotion_review.json`;
- `model/calibration_validation/bnr_bls_cross_round_mapping_audit.json`;
- `model/calibration_validation/bnr_bls_missing_round_semantic_mapping_audit.json`.

The admission panel, finalized SRA, SeriesContract, source-specific VerifiedSeries and ChartSpec v3 identities are all rebuilt. Stored IDs must match the rebuilt scientific content ID, qualified series ID and chart identity. The qualified 2025-Q2 absence remains MISSING, with its exact reason and no numeric payload or physical source row. Missing or altered source files fail closed.

## Component boundary and release status

The History component alone does not authorize Application dispatch; the complete, separately qualified route is now integrated through PRs #471–#479 on development `main`. The public v0.7.1 artifact remains unchanged and does not include BNR C01 charting. Before v0.8.0 publication, the final metadata-bearing Flatpak must pass the release gate and receive explicit on-device acceptance. Existing GISTEMP v1 and Energy Institute v2 reconstruction profiles remain unchanged.
