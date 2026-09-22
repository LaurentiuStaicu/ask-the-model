# AtM system invariant registry

AtM keeps release-relevant system invariants in a machine-readable registry:

`qualification/invariants-v1.json`

The registry is a qualification contract, not a substitute for tests or implementation evidence. An invariant marked `enforced` must name both the code paths that enforce it and the automated tests that exercise it.

## Severity

- **S0** — a violated invariant makes the affected AtM state or scientific result inadmissible. S0 is release-blocking.
- **S1** — a required v1 integrity/reproducibility invariant. S1 is release-blocking unless the declared release scope explicitly excludes it.

There is no readiness score. Blocking invariants are pass/fail claims.

## Lifecycle

- `enforced` — implementation and automated test references exist in the current tree.
- `planned` — accepted for a future implementation PR but not yet claimed as implemented.
- `retired` — preserved for traceability but no longer part of the active qualification contract.

The initial QUAL-01 registry contains only invariants already enforced by the current AtM baseline. SCI, STATE, TRUST, AUDIT, RECOVERY, LIFECYCLE, RESOURCE and RELEASE invariants are added by the PR that introduces their enforcement.

## References

`enforced_by` contains repository-relative implementation paths.

`tested_by` currently uses `meson:<test-name>` references. The validator checks that every referenced implementation file exists and that every referenced Meson test name is present in `meson.build`.

## Validation

Run:

```sh
python3 scripts/validate_invariant_registry.py
```

The `Invariant Registry` GitHub Actions workflow runs the same validation on pull requests and on pushes to `main`.

## Change rule

A PR that creates, changes, weakens, retires or newly enforces an S0/S1 invariant must update the registry in the same change. A future qualification workflow can therefore fail closed when a release-blocking invariant lacks implementation or test evidence.

The registry records claims about the current source tree. It must never mark a planned safeguard as `enforced` merely because it is part of the v1 architecture plan.
