# Capacity historical amplification v1

This corpus extends C0-M1 from one pinned SHA per repository to a frozen
historical sample used only for capacity-prediction research.

## Corpus

Each of EWD, CBD and RMD contributes six exact SHAs:

- recent commit ranks 1, 8, 15, 22 and 30 from the default-branch commit
  history observed when C0-M4 was designed;
- the exact `pinned-m1` SHA already used by the C0-M1/R5 baseline.

All 18 SHAs were prechecked to expose the AtM ingest surface
`.atm/repository.json` + `CITATION.cff`. The real ingest/index measurement
runner is still authoritative: if a frozen SHA cannot actually pass current
ingest/source/index qualification, the M4 workflow fails.

## Why this exists

C0-M1 showed that one global archive-size multiplier is not credible:
EWD is index-dominant while RMD is snapshot/repair-dominant.

C0-M4 therefore asks a narrower question: within each repository, can
observable post-download inputs support a stable conservative prediction
family for a future SHA?

Measured features include:

- archive allocated bytes;
- snapshot logical regular-file bytes;
- snapshot entry count;
- fresh snapshot additional allocated peak;
- retrieval-index additional allocated peak.

The aggregate artifact derives per-repository envelopes and performs
leave-one-out checks for simple transparent families:

- snapshot logical bytes + maximum observed overhead per materialized entry;
- index peak / snapshot logical-byte ratio;
- index peak / materialized entry.

Leave-one-out means one SHA is held out while the envelope is learned from
the other five. Any underprediction is reported explicitly.

M4 also evaluates a transparent hybrid candidate for snapshot and index
prediction. The hybrid takes the maximum of the relevant observable formulas
and the maximum absolute peak seen in the five training SHAs. This is useful
because a repository can have a relatively stable absolute index floor even
when logical bytes or entry counts change. Hybrid coverage is measured only;
the benchmark does not select it as production policy.

## Reviewed M4 result

The successful hybrid run is frozen in `evidence.json`.

Across the 18-SHA corpus, both hybrid candidates covered all leave-one-out
cases:

- snapshot hybrid: 18/18;
- index hybrid: 18/18.

This is materially stronger than the individual formulas, each of which
covered only 5/6 cases per repository. However, the EWD index hybrid has a
limiting held-out case with actual/prediction = exactly 1.0. The reviewed
sample therefore demonstrates coverage but does not itself provide a positive
future-SHA safety margin.

The historical absolute floor is also inherently backward-looking: a future
SHA can exceed every sampled peak. These limitations are deliberately
machine-validated so later policy cannot reinterpret 18/18 historical coverage
as a mathematical upper bound.

## Non-policy boundary

No production predictor, multiplier, safety margin or refusal threshold is
selected by this benchmark.

If the historical envelopes are unstable, the correct result is to keep an
unknown SHA unqualified for proactive byte prediction and rely on exact
profiles plus already-qualified fail-closed ENOSPC recovery. Measurement
must not be converted into a guarantee that the data do not support.
