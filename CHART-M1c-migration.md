# CHART-M1c: generic source-bound series chain

Migration package for the change on branch `chart-m1c-generic-evidence-sra`.
This file is the source of truth for the branch; the PR body is a summary of it.

## What this change does

Extracts the source-specific evidence and SRA modules into shared,
descriptor-driven layers, then adds the adapter that bridges them to the general
series contract.

Before:

```text
gistemp_admission          energy_institute_admission
gistemp_evidence  (150)    energy_institute_evidence  (65)
gistemp_sra                (absent)
verified_series  (GISTEMP-typed)
chart_spec / chart_history_reconstruct  (GISTEMP-typed)
```

After:

```textnatm_series_evidence        generic, descriptor-driven
atm_series_sra             generic, descriptor-driven
atm_series_adapter         generic, descriptor-driven
the four descriptors       GISTEMP, Energy Institute
gistemp_admission          energy_institute_admission   (unchanged)
```

Both sources reach the same finalized SRA and can materialise the same general
contract. No source name appears in the shared construction path.

## Files on the branch

| File | Replaces | Note |
| --- | --- | --- |
| `src/series_evidence.h` / `.c` | `gistemp_evidence.c`, `energy_institute_evidence.c` bodies | one construction path |
| `src/series_evidence_adapters.h` / `.c` | the two modules' constants and payload builders | the only source-aware file |
| `src/series_sra.h` / `.c` | `gistemp_sra.c` | generic `build_result` |
| `src/series_adapter.h` / `.c` | — | evidence to general contract bridge |
| `tests/series_evidence_equivalence_test.c` | — | 7 tests |
| `tests/series_sra_equivalence_test.c` | — | 4 tests |
| `tests/series_adapter_test.c` | — | 5 tests |

Nothing existing is modified or deleted. `gistemp_evidence.c`,
`energy_institute_evidence.c` and `gistemp_sra.c` must stay in the build so the
equivalence tests have a legacy side to compare against. Removing them is a
follow-up commit, made only after those tests pass.

## Design decisions for review

**One open value.** `ATM_EI_RECONSTRUCTION_OBLIGATION` is declared once in
`series_evidence_adapters.h` under `TODO(F0-19)` with the symmetric value. A
wrong obligation produces an SRA that passes generic validation, because its
envelope is internally consistent. It is merely different, not detectably wrong.

**Limitations are per source, not shared.** GISTEMP carries three; Energy
Institute carries four, including the M0 decision that the 2019-2023 component
reconciliation residual is retained as provenance and not repaired. Sharing
them would have silently dropped a scientific decision.

**Support arity is now a checked contract.** Both legacy modules encoded
"four source bindings plus the point's own observation" as `point + 4` against a
literal `5`. The descriptor requires `support_per_point == source_count + 1` and
rejects one that disagrees, at construction rather than at first use.

**Coverage is checked before any payload is built.** The generic layer requires
the extracted point count to equal `evidence_count - source_count`, so a
wrong-count descriptor fails early instead of mid-construction.

**No source header in the shared layer.** `series_evidence.c` reaches points
through the existing `AtmAnnualSeriesCandidate` interface, which both admitted
sources already implement. The descriptor carries admission callbacks, so the
shared layer never sees either concrete admission type.

**The adapter reads structure from the artifact, not from a caller.** Subject,
attribute, unit and dimension come from the observation payload, which is the
material the artifact identity was minted over. Repository, version and
snapshot likewise. The contract's descriptive fields therefore cannot drift
from the admitted source.

## Identity consequence, stated plainly

The general contract serialises differently from `verified_series.c`, so the
adapter produces identities that differ from the two pinned in
`tests/fixtures/chart01/verified-series-identity.json`:

```text
scientific_id : 4cfff6bb991c2d771228c09e81ca860a5b3e195e4f2390d059a3c8aacc65c48c
qualified_id  : 29827400d3e250f6c935eefa33d2a8c4f99c4198721676231e777d16ee1f6b24
```

This is the intended shape of the migration and not an accident: the contract
stays clean, and `verified_series.c` remains the holder of the persisted
identities. Downstream consumers move to the contract; stored charts stay valid.

If the pinned identities must instead be reproduced by the contract, the
general serialisation has to carry `reference_period`, `selection`,
`access_vintage` and `completeness`, plus the same value types. That is a
deliberate widening of the general contract and belongs in its own review.

## Test evidence

The three test files together establish:

- every artifact of both sources is byte-identical between the legacy module and
  the generic layer, index by index, including per-point support;
- both finalized result identities, the whole qualification envelope, every
  established fact with its qualifiers and support order, and the limitation set
  are identical between `gistemp_sra.c` and `series_sra.c`;
- the extraction produces no derived facts, constraints or conflicts;
- admission through the adapter validates against the finalized SRA and is
  deterministic;
- every point carries its admitted year, and the coefficient/exponent the
  contract recomputed from the source decimal agrees with what the adapter
  supplied;
- different reviewed metadata produces a different identity, which is how the
  test detects metadata that is bound into the contract rather than decorative;
- argument and descriptor guards hold, and a pre-populated output is left
  untouched on every failure path.

All three resolve fixtures as `ATM_CHART01_FIXTURE` plus the nested `ewd/`
paths, and assert the four source files against
`tests/fixtures/chart01/source-lock.json` before any comparison runs.

## Not verified

The build has not been run. No toolchain was available. The signatures were
written against the published headers, and the first `meson` run is the real
check. The most likely compile errors are:

1. the Energy Institute admission and evidence API, whose symbol names came from
the header index rather than a compiled reference;
2. `atm_series_contract_new`, called with 17 positional arguments read from
   `series_contract.h` — a wrong order is a compile error, not a silent wrong
   behaviour;
3. `series_adapter.c` uses `descriptor->source_count` as the offset to the first
   observation artifact, which assumes `series_evidence.c` keeps source bindings
   on the leading indices. That holds today, but an explicit
   `atm_series_evidence_first_point_index()` accessor would make the dependency
   visible instead of implied.

## Squash note

Two early commits on this branch contain the literal string `PLACEHOLDER` in the
eight files, written while probing whether the GitHub write path was open. Real
content was pushed afterwards and the tree is correct. Squash or reset before
review, and delete `PLACEHOLDER_NOTE.md` at the same time.
