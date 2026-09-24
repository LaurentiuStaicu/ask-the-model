# Capacity v3 — no-refit out-of-sample holdout

This corpus supports OPT-C0P / #232.

Training is frozen by `benchmarks/capacity-v2/corpus.json` (18 SHAs: 6 per repository).
The v3 corpus contains 12 **disjoint holdout SHAs** (4 per repository) selected at
default-branch ranks 4/12/20/40 observed after C0-M4.

The M5 workflow remeasures both sets in one run but fits the transparent M4 hybrid
only on the 18 training records. Holdout records are never used to refit coefficients.

Purpose:
- test whether the M4 repository-specific hybrid generalizes beyond its training samples;
- quantify actual/prediction ratios without adding an automatic margin;
- preserve failures as evidence rather than tuning them away.

This is still qualification evidence. It does not select a production predictor,
margin, reserve, refusal threshold or Optimizations-ON runtime behavior.
