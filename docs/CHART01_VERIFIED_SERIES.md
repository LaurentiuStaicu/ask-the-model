# CHART-01: source audit and VerifiedSeries boundary

Status: measurement/contract work; no chart runtime. Tracks #343 after PRES-08.

## First pinned source

EWD commit `d9e249339663015f6d1c05752338a955bf64ad0b`, path
`science/data/processed/nasa_gistemp_global_2026.csv`, is the first real
source-adapter candidate. Retained fixtures preserve upstream bytes, including
the absence of a final newline in the provenance JSON. `source-lock.json`
pins the commit and all four file hashes; the upstream input manifest separately
confirms the processed CSV and provenance hashes.

The source has 146 annual observations, 1880–2025, with decimal values written to
four places. Its subject is global surface temperature anomaly relative to
1951–1980, in degrees Celsius. It is empirical climate evidence, not a World3
persistent-pollution trajectory. The registry's `model_variable=ppol` is not
authority to relabel this observation or activate a model feedback.

The audit verifies retained processed data and metadata. It does not claim to
reproduce NASA's raw-to-processed calculation or qualify a native numeric-series
adapter. No current AtM EWD profile admits this path as a numeric series.

Run offline:

```sh
python3 scripts/validate_chart01_source.py --check
python3 tests/chart01_source_audit_test.py
```

## Rules fixed for this first adapter candidate

- X is an integer calendar year, ordered strictly ascending; repeated or missing
  years reject this particular complete annual source. No automatic sorting.
- Y retains its exact source decimal token; no binary floating-point round trip.
- Empty/nonfinite values and changed decimal syntax reject this pinned adapter.
  This is not a general ban on missing values in VerifiedSeries.
- Unit, reference period, global scope, annual aggregation, empirical status and
  source vintage are constitutive metadata. Scenario is not applicable here.
- Hash, coverage, source-set or metadata disagreement rejects the audit.
- This audit emits no scientific/qualified series ID and cannot authorize drawing.

## Native contract still to implement

`atm-verified-series/1` needs a bounded native builder that consumes qualified
facts through an explicitly admitted adapter. It must validate the complete SRA
and source artifacts, not accept identity strings as proof. Existing seams:

- `atm_sra_result_validate()` recomputes finalized SRA identities and checks support;
- `atm_scientific_artifact_validate()` validates source identity and profile;
- `atm_scientific_decimal_parse()` provides canonical decimal coefficients/exponents;
- `atm_scientific_binary64_bits()` preserves finite binary64 identity for derived
  results under the registered operation's numeric profile.

These primitives do not by themselves verify that a selected fact means a
particular X/Y coordinate. The adapter must bind values and all axis/context
metadata to the qualified source. Do not promote a variable declaration into a
trajectory, parse numbers from assistant prose, or treat hashing as scientific
validation.

Before native implementation, specify and test the ownership/lifetime model,
point/support/byte bounds, canonical series identity, general NUMERIC/MISSING
representation, explicit discontinuity rules, decimal-to-render-coordinate
conversion and signed-zero policy. Derived series additionally need registered
operation binding and exact binary64 replay. Event/step and categorical series
are later adapters; do not silently apply annual-series ordering to them.

CHART-02 consumes qualified series and owns compatible axes and GAP policy;
CHART-03 adds Cairo and the `atm-retro-mono/1` visual prototype; CHART-04 owns
M4/resize/cache; CHART-05 owns exact History persistence. No persistence or
Presentation behavior changes in this source-audit slice.
