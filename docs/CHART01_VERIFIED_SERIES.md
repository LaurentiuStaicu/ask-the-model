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

## M1a native candidate boundary

`annual_series_candidate.h/.c` supplies an opaque, immutable parsed candidate,
not a VerifiedSeries. It is linked only into its Meson test. The parser performs
no file/network access, accepts source bytes rather than caller-supplied point
arrays, deep-copies those bytes, and preserves each decimal token and original
one-based CSV row. It also exposes the existing exact-decimal canonical
coefficient/exponent and a raw source SHA-256; that digest is not qualification.
Borrowed accessors remain valid until candidate destruction. Failure leaves a
NULL output unchanged; pre-populated output pointers are rejected.

The complete annual source dialect rejects gaps, duplicates, year zero, CRLF,
quoted/extra columns, embedded NUL, missing and nonfinite values. This narrow
parser does not implement general MISSING/event/categorical series. Exact
source `-0.0000` is retained; its canonical mathematical value is `0 × 10^0`,
matching Scientific Plane conventions. No conversion to binary64 occurs.

Resource ceilings are defensive allocation policy: 64 KiB source bytes (matching
M0's file ceiling), 256 points (above the pinned 146 annual observations), and
64 bytes per decimal token. Limits are checked before copying source bytes,
allocating each point or canonicalizing a token. These are not performance
benchmarks or a scientific time horizon. Tests exercise limit and limit+1 where
applicable, immutable ownership, malformed inputs and the exact pinned source.

M1b must bind the parsed candidate to qualified data and metadata through an
explicit profile admission. Only that later layer may construct VerifiedSeries.
The candidate API has no qualified-series ID, unit setter or rendering authority.

## M1b pinned source admission

`gistemp_admission.h/.c` introduces the independent policy
`atm-gistemp-pinned-admission/1`. It does not modify the existing EWD evidence
profile or create an `AtmScientificArtifact`, SRA result or VerifiedSeries.
It admits only the exact repository, snapshot and four byte-pinned files above.
The source order is fixed: CSV, provenance, input manifest, registry. A changed,
missing, swapped or oversized file fails closed; matching caller identity
strings cannot admit different bytes. New vintages need explicit policy review.

The builder owns deep copies, verifies their SHA-256 against compiled policy,
then parses the owned CSV and checks its 146-point, 1880–2025 coverage. All four
source sizes are checked before copying; each is bounded to 64 KiB, for at most
256 KiB of retained bundle bytes plus the candidate's bounded copy/points.
Returned candidate and metadata are borrowed read-only data. Failed admission
leaves output unchanged. This is an in-memory API with no network or file I/O.

The metadata mapping is a reviewed interpretation of the *exact* provenance
and registry bytes retained by M0: global annual empirical surface-temperature
anomaly, calendar years, Celsius anomaly, reference period 1951–1980 and source
access vintage 2026-08-31. There are no caller metadata setters. File paths and
hashes expose the complete supporting bundle; candidate row locators identify
numeric observations. The `ppol` registry alias never becomes the subject.
The access vintage is not a claim that measurements were made on that date.

This policy is a local trust anchor for one reviewed processed source. It does
not independently authenticate upstream Git membership, reconstruct raw NASA
data, generalize scientific admission to other files, or mint qualified support
IDs. Hash matching is used to bind reviewed metadata to reviewed bytes, not as
a substitute for scientific interpretation. M1c still needs canonical series
identities, qualified support/artifact integration, revalidation and a full
VerifiedSeries contract before any chart can be authorized.

Native tests compare all compiled pins with the M0 lock, reject mutation of each
source, missing/swapped sources, wrong repository/snapshot and byte ceiling
violations, and check caller-memory independence. Both native targets run under
ASan/UBSan and in Meson/Flatpak. This layer remains linked only into tests.

## M1c1 reconstructed scientific evidence

`gistemp_evidence.h/.c` defines the dormant source-specific adapter
`atm-profile/ewd-gistemp-pinned/v1`. Its only input is an admitted bundle; it
re-admits the owned source bytes and re-extracts points, rather than trusting
cached candidate fields. It does not accept arbitrary evidence atoms, X/Y
arrays, profile names, metadata or identity strings from callers. The existing
EWD v1 profile and runtime dispatch remain unchanged.

The adapter builds exactly 150 canonical scientific artifacts using existing
Scientific Plane identity functions: four whole-file bindings and 146 annual
observations. Each point exposes five qualified support IDs, in fixed order:
CSV, provenance, manifest, registry, then its own observation artifact. The
aggregate support set is all 150 artifacts. The file bindings attest to matching
the reviewed local policy; they do not independently authenticate upstream Git
membership or reproduce raw NASA processing.

Observation scientific payloads contain calendar year, NUMERIC status, exact
canonical decimal coefficient/exponent, subject, attribute, temperature-
difference dimension, unit, reference period, selection, access vintage,
empirical status, annual frequency and not-applicable scenario. The numeric
profile label describes the existing decimal primitive, not a new floating-point
operation. Source spelling remains preserved by the admitted CSV and its digest;
it is not substituted for canonical numeric identity. No binary64 conversion
occurs. Calendar years are labels here, not a declaration of fixed-length seconds.

Qualified artifact identity additionally binds the exact source path, row and
both column names, snapshot, logical observation ID and dedicated adapter
version. Repository version is `snapshot:<sha>` rather than an inferred mutable
release label. The canonical repository identifier is `ewd`; its mapping to the
full upstream repository name is fixed by the admission policy. Whole-file
bindings use `file:complete` and the appropriate source path.

Validation creates a fresh admitted bundle and fresh artifacts from owned bytes,
validates each stored artifact's identities, then compares its complete storage
digest with the corresponding reconstructed artifact. This rejects reordered
artifacts and even altered values, units, baselines, years, locators or source
receipts whose generic artifact hashes have been correctly recomputed. Generic
artifact validity alone is therefore insufficient for this adapter.

Construction and validation are bounded by the admitted four-file policy and
fixed 150-artifact count; payloads are generated only from bounded parsed tokens
and fixed metadata. The collection owns its sources and artifacts, including
after destruction of the original admission. Borrowed artifacts must not be
modified or freed by callers; corruption tests deliberately violate that
read-only contract to verify rejection. This is not a deserializer for arbitrary
pointers or JSON documents.

M1c1 does not create an SRA result, series-level scientific/qualified identity,
VerifiedSeries, general MISSING/discontinuity representation or chart authority.
Those remain M1c follow-up work. It only establishes the source-bound artifact
and per-point support foundation, linked exclusively into tests.

## M1c2 finalized source-bound SRA

`gistemp_sra.h/.c` builds an opaque owned SRA wrapper from an admitted bundle.
It owns reconstructed evidence and a finalized `AtmSraResult`; it accepts no
caller-authored facts or pre-finalized results. Its result has exactly 146
established facts and 150 qualified supporting artifacts, with no derived facts
or operations. The ANSWERED state means the narrow extraction is complete; it
is not an answerability claim for arbitrary user questions.

A fact is identified by its calendar year. Its exact value is the existing
canonical decimal coefficient followed by `e` and the integer exponent (for
example `-17e-2`), never a binary64 projection. Unit, dimension, subject and
attribute come from the reconstructed observation artifact. Its qualifiers
retain the complete observation payload, including year, baseline, empirical
status and vintage. This deliberate redundancy keeps the value and full context
together; source-bound validation checks both against reconstructed evidence.
Each fact retains the four source-binding support IDs plus its observation ID.
SRA finalization applies the existing canonical ordering and support de-duplication.

The qualification envelope binds `atm-sra/1`, the dedicated source reconstruction
obligation, exact repository snapshot, dedicated semantic profile and exact-
decimal numeric profile. Limitations explicitly retain the processed-data/raw-
reproduction boundary, distinguish temperature anomalies from World3 pollution,
and state that this is not VerifiedSeries or rendering authority.

Validation first verifies the finalized SRA's generic identities, then validates
and reconstructs its owned evidence and builds a fresh SRA. Both scientific and
qualified result identities must match. A changed value, unit, support set,
limitation or numeric profile is therefore rejected even after successful
re-finalization with the generic SRA API. The collection retains the bounded
M1c1 source adapter; this API does not deserialize arbitrary external SRA data.

Tests cover deterministic identities, complete fact/support counts, exact values,
caller-admission lifetime, re-finalized forgeries, output ownership and refusal
of unfinalized results. This target is linked only into native/Flatpak tests.
Series-level identity, explicit point ordering/missing/break contract and
VerifiedSeries remain subsequent work; an SRA identity is not silently reused
as a chart-series identity.
