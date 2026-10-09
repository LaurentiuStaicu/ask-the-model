# CHART-01: source audit and VerifiedSeries boundary

Status: native complete-source qualification; no chart runtime. Tracks #343 after PRES-08.

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

## Qualification requirements established at M0

The `atm-verified-series/1` target requires a bounded native builder that consumes qualified
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

## Additional reviewed EWD snapshot for live reconstruction

The live chart admission also accepts EWD commit
`dc710d8e604c0fff713a3acfdb18ab47a1a902bb`. This is an explicit second
snapshot policy, not a moving-branch exception. It preserves the original
snapshot admission so previously persisted conversations remain reconstructable.

The GISTEMP CSV and provenance bytes are identical to the first pinned snapshot.
The current snapshot's exact SHA-256 values for the two changed metadata files
are:

- `science/data/input_manifest.json`: `ece72aed0d444484635e38171df703a775e5922df95b8783385cbfed7e2ffbd6`
- `science/data/registry.csv`: `6ff4e0bffe03cce368dcd278d6ca57b6e8f181aa241dea7206508499ec3fa3b8`

Admission remains fail-closed: repository identity, snapshot SHA, and all four
source-file digests must match one complete reviewed policy bundle. The newer
manifest must still bind the exact CSV and provenance hashes, and its registry
must retain the reviewed empirical annual Celsius-anomaly semantics and the
boundary against treating this observation as World3 persistent pollution.
Unknown snapshots, mixed metadata bundles, and modified files remain rejected.
The source audit fixture and report continue to document the original snapshot;
the runtime admission tests separately exercise the additional snapshot and
rebuild preservation.

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


## M1c4 general VerifiedSeries contract

The dormant native contract `atm-verified-series/1` is now pinned separately from the GISTEMP adapter. It is a semantic container/validator, not a source adapter and not chart authority. It supports exactly three X kinds: integer calendar year, canonical UTC instant (`YYYY-MM-DDTHH:MM:SSZ` with real calendar/time fields), and non-negative event ordinal representable as an unsigned 64-bit integer. X values are strictly increasing; duplicate X values reject and no automatic sorting occurs.

Y is explicitly `NUMERIC` or `MISSING`. Numeric values retain the source decimal token plus canonical coefficient/exponent; signed zero is preserved only as the exact `-0` representation. MISSING never carries a numeric value and requires an explicit bounded reason. A discontinuity is represented only by `break_before=true` with a bounded reason. A first-point break is rejected; a final break and an all-MISSING series are valid contract states because neither invents a value or silently joins observations.

The general contract is bounded at 10,000 points, 32 support IDs per point and 256-byte metadata/reason fields, with bounded X and numeric encodings. Support must be a lowercase SHA-256 identity present in a finalized and independently validated SRA qualification; matching a caller-provided identity string is insufficient. Scientific and qualified series identities are recomputed from canonical contract material and provenance.

This general contract deliberately remains dormant. It does not create an adapter, widen the admitted GISTEMP profile, change Application behavior, enable charts, or reinterpret model output. Source-specific adapters must establish their own semantics and qualification before constructing a VerifiedSeries.

## M1c3 first native VerifiedSeries

`verified_series.h/.c` implements `atm-verified-series/1` under the narrow
`atm-series/gistemp-complete-annual/1` profile. Its only factory accepts the
reviewed GISTEMP admission. It rebuilds that admission, constructs and validates
the source-bound SRA, and explicitly checks each year/value mapping before
materializing points. No arbitrary point-array or model-text constructor exists.
It remains linked only into tests; ChartSpec, rendering and persistence are not
enabled by the existence of this type.

The supported contract is deliberately complete annual observations only:

- Exactly 146 ordered points, 1880–2025, zero-based order and one-based CSV rows.
- X is a calendar-year coordinate (time dimension), not elapsed seconds or an
  assumption that every calendar year has an identical duration.
- Each Y is NUMERIC with exact canonical decimal coefficient/exponent and the
  separate original source token. No binary64 conversion or interpolation.
- `break_before=false`, `missing_reason=null`, `break_reason=null` for every
  point, including the first (which has no predecessor to disconnect).
- MISSING has an explicit enum value but is rejected by this profile. Gaps,
  events, categorical coordinates and all-missing series are unsupported, not
  converted into zero, interpolated or joined. Broader semantics require an
  independently specified and qualified adapter/profile.
- Each point owns five sorted qualified support IDs. Their union equals the
  sorted 150-artifact SRA evidence set; row and both column locators remain bound.

Identity uses the existing canonical JSON hashing primitive with separate
`atm.verified-series.v1` and `atm.verified-series.qualification.v1` domains.
Scientific identity includes ordered points, exact numeric encoding, point
status/break fields and meaning-changing subject/attribute/axis/context metadata.
Qualified identity binds that scientific identity to the exact snapshot,
admission and series profiles, finalized SRA qualification, aggregate and
per-point support, source path/columns/rows and original decimal spelling.
It is neither the SRA identity nor the source-file SHA-256. Changing original
spelling without changing mathematical value is still a qualification change.
Changes to this canonical contract require explicit version/compatibility review.

Validation checks complete-source shape and bounded tokens/support IDs, validates
the owned source-bound SRA, recomputes both series IDs from materialized points,
and builds a fresh series from owned source bytes to compare both IDs again.
This checks both internal identity consistency and source agreement. Limits
remain the reviewed four-file admission ceilings, fixed 146 points, five support
IDs per point and 150 aggregate support IDs; there is no external JSON importer.
The retained M0 source-audit report describes that earlier audit's output scope;
it is not a capability flag for this new native API.

Tests exercise all source values and support membership, deterministic distinct
identities, lifetime independence, corrupted order/year/row/value/exponent,
MISSING/break fields, changed source spelling, support/identity corruption and
invalid API arguments. General missing/discontinuity qualification and downstream
chart gates are not claimed by this complete-source implementation.

## Complete-annual qualification closeout

This is a scoped gate for the one admitted complete annual profile. It does not
close the broader #343 requirements or qualify other repositories, vintages,
events, missing observations or derived numeric series. ChartSpec work may use
this profile once the closeout CI passes; the following exclusions remain gates.

| Requirement | Evidence and scope |
| --- | --- |
| Source identity and meaning | Four byte-pinned files, M0 audit, native admission, exact snapshot and reviewed metadata |
| Exact numeric values and order | All 146 observations, exact decimal coefficients/exponents, consecutive years, row/column locators |
| Qualified facts and support | Reconstructed evidence, finalized source-bound SRA, five supports per point, 150-artifact aggregate |
| Stable series identities | Retained reference IDs in `tests/fixtures/chart01/verified-series-identity.json`, checked by native test |
| Source agreement beyond hashes | Rehashed artifact, re-finalized SRA and rehashed series forgeries must be rejected |
| Scientific/provenance separation | Changed mathematical value changes both IDs; equal-value source spelling or support changes qualification only |
| Ownership and limits | Owned source bytes and data, fixed complete-source counts, checked byte/token/support ceilings |
| Missing/discontinuity boundary | First, last and all-MISSING cases reject; no zero filling, interpolation or joined gaps |
| Generic missing/event/derived data | Not qualified; remains open in #343 and cannot enter this profile |
| Runtime chart display | Not enabled; requires ChartSpec and renderer qualification |

The closeout fault-injection helper is compiled only with
`ATM_VERIFIED_SERIES_TESTING`, only in the VerifiedSeries test target, and has no
public header/API declaration. It recomputes internally consistent identities
for deliberately altered materialized points, allowing the test to prove that
fresh source reconstruction still rejects them. It is not an alternate factory
or an application feature. The native CI also checks a compilation without that
define to ensure the helper is absent from the production object.

The identity reference records values observed from merged PR #350's qualified
native run, not values generated afresh by the test being checked. Updating the
reference requires review of the canonical-contract compatibility consequences.
