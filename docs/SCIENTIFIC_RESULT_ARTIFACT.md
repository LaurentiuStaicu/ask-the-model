# Scientific Result Artifact v1 and Gold Set v0

SCI-05 introduces the first deterministic Scientific Result Artifact (SRA) above canonical scientific artifacts.

It does not execute scientific formulas, run repository code, call an LLM, or create derived facts. Those capabilities begin only after the Operation Registry in SCI-06.

## ScientificResultContent v1

The semantic result contains:

- `answerability`: `ANSWERED`, `PARTIAL`, `NOT_ANSWERABLE`, or `BLOCKED`;
- `established_facts[]`;
- `derived_facts[]`;
- `constraint_results[]`;
- `conflicts[]`;
- `limitations[]`.

In SCI-05, `derived_facts[]` must be empty. The qualification envelope must also contain no operation claims. A non-empty derived or operation set fails closed.

### Established facts

Each established fact has:

- stable fact ID;
- fact type;
- subject ID;
- attribute;
- value;
- optional unit;
- optional dimension;
- optional qualifiers;
- one or more supporting canonical artifact IDs.

Support IDs do not enter `ScientificContentId`. They are provenance and therefore enter the qualification envelope only. The qualification identity commits both the global evidence/control sets and the exact per-record bindings `fact → support`, `constraint → support`, and `conflict → support`. Reassigning the same global sources to different claims therefore changes `QualifiedArtifactId` without changing scientific content. This also means the same scientific fact can keep the same scientific-content identity across a new snapshot while its qualified identity changes.

Optional qualifiers are validated as JSON and represented in semantic identity by their own canonical content ID, so whitespace or object-key order cannot perturb SRA identity.

### Constraints

Constraint results use `PASS`, `DENY`, or `NOT_EVALUATED` and always carry deterministic support plus a reason code.

RMD behavioural closure is the first required control constraint. When the pinned control says `active=false`, the corresponding result is `DENY` with reason `CONTROL_CONSTRAINT_DENIED`.

## QualificationEnvelope

The v1 qualification envelope contains:

- schema `atm-sra/1`;
- canonical obligations;
- pinned repository snapshots;
- evidence artifact IDs;
- control evidence artifact IDs;
- semantic profile IDs and versions;
- operation IDs/versions;
- numeric profile;
- optional temporal dependencies;
- optional stochastic dependencies;
- exact fact/constraint/conflict support bindings;
- result `ScientificContentId`;
- result `QualifiedArtifactId`.

SCI-05 requires `operations[]` to be empty. Evidence/control sets can be populated only by admitting a complete SCI-04 canonical artifact that passes `atm_scientific_artifact_validate()` and matches the expected evidence/control kind; arbitrary 64-hex strings cannot enter qualification. Every fact, constraint, or conflict support ID must then occur in those validated artifact sets.

Set-like arrays and result objects are sorted and deduplicated before finalization. Retrieval order therefore cannot affect either result identity.

## Answerability

`ANSWERED` and `PARTIAL` require at least one established fact.

`NOT_ANSWERABLE` and `BLOCKED` may contain no facts. They must still carry a complete qualification envelope. Unsupported semantics are represented explicitly rather than silently promoted from textual evidence.

## Gold Set v0

`qualification/scientific-gold-v0.json` pins seven offline cases to exact EWD/CBD/RMD commits.

1. SG-001 — EWD `food_per_capita` structural semantics.
2. SG-002 — exact EWD historical MAPE `7.005760432822278`.
3. SG-003 — EWD resources identifiability fraction `0.9244664056478901` and `weakly_identified=true`.
4. SG-004 — CBD EVSD/medium/160/160 recovery probability `0.830`, `passes_0_80=true`.
5. SG-005 — RMD behavioural closure control `false` → constraint `DENY`.
6. SG-006 — RMD `bilateral_financial_position` unit/status.
7. SG-007 — unsupported EWD `module` semantics → `NOT_ANSWERABLE / UNSUPPORTED_SEMANTICS`.

The Gold suite requires no LLM and no network. Supported cases are first converted into SCI-04 canonical artifacts and then used as typed support for SRA facts/constraints.

The current CSV index row key is not assumed unique for SG-004. The pinned row is identified by exact source line/logical ordinal and the full tuple in its payload.