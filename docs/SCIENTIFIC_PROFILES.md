# Scientific repository profiles

AtM scientific profiles are application-owned allowlists that map retrieval structure to narrow typed **record classes**.

They are not repository instructions and they are not executable model code. A repository cannot widen a profile by adding a new object, field, relation type or similarly named dataset.

## v1 profile scope

### EWD

The EWD v1 profile recognizes:

- structured `variable` records
- `candidate_interface` records
- `promotion_gate` records
- rows from exactly `data/scenarios/fit_diagnostics.csv`
- rows from exactly `data/scenarios/parameter_identifiability.csv`

Other EWD structured records remain unsupported until an AtM change explicitly adds them.

### CBD

The CBD v1 profile recognizes:

- structured `variable` records
- relations with the current declared link types `CAUSAL`, `MODERATING`, `INFORMATION_FLOW`, and `MEASUREMENT`
- the generic `relation` structural type used by current contract relations that expose `source`/`target` but use a payload-level `kind`
- rows from exactly `model/benchmarks/results/m1_e4_candidate_recovery_authoritative_2026-09-16.csv`

Profile recognition does not make an `EXECUTABLE` payload status an AtM execution permission.

### RMD

The RMD v1 profile recognizes only structured:

- `stock`
- `flow`
- `auxiliary`
- `equation`

records from retrieval.

The RMD `behavioural_closure.active` control is deliberately outside this profile because the current contract object has no indexed `id`. It is handled by deterministic control evidence rather than by retrieval inference.

## Meaning of typed validation at this layer

`TYPED_VALIDATED` means that:

1. provenance is complete;
2. the evidence belongs to the profile's repository;
3. its structural selector is explicitly allowed by the frozen AtM profile;
4. profile-declared generic requirements are present.

The semantic type is deliberately named as a record class, such as `rmd.stock_record`.

This layer does **not** yet claim that every payload field (unit, scientific status, maturity, epistemic level, numeric value, or gate result) has been scientifically validated. Profile-specific extraction and deterministic control evidence perform those later checks before a scientific result may rely on them.
